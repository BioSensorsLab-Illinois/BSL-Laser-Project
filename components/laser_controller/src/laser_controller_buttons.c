#include "laser_controller_buttons.h"

#include <stdatomic.h>
#include <stddef.h>
#include <string.h>

#include "laser_controller_board.h"
#include "laser_controller_pinmap.h"

/* MCP23017 register map (IOCON.BANK=0 addressing). */
#define MCP23017_REG_IODIRA   0x00U
#define MCP23017_REG_IODIRB   0x01U
#define MCP23017_REG_IPOLA    0x02U
#define MCP23017_REG_GPINTENA 0x04U
#define MCP23017_REG_DEFVALA  0x06U
#define MCP23017_REG_INTCONA  0x08U
#define MCP23017_REG_IOCON    0x0AU
#define MCP23017_REG_GPPUA    0x0CU
#define MCP23017_REG_INTFA    0x0EU
#define MCP23017_REG_INTCAPA  0x10U
#define MCP23017_REG_GPIOA    0x12U
#define MCP23017_REG_OLATA    0x14U

/*
 * IOCON bits (BANK=0 form):
 *   bit7 = BANK    (0: standard register addressing)
 *   bit6 = MIRROR  (1: INTA and INTB tied together; either port triggers INTA)
 *   bit5 = SEQOP   (1: disable auto-increment so single-byte writes are deterministic)
 *   bit4 = DISSLW  (0: slew rate enabled — we are slow-poll anyway)
 *   bit3 = HAEN    (0: hardware address-enable only matters on MCP23S17 SPI variant)
 *   bit2 = ODR     (1: INT output is open-drain; ESP32 side provides pull-up)
 *   bit1 = INTPOL  (ignored when ODR=1)
 *   bit0 = —       (reserved, 0)
 */
#define MCP23017_IOCON_VALUE  ((1U << 6) | (1U << 5) | (1U << 2))

/*
 * GPA0..GPA3 are the four buttons we care about. A4..A7 are reserved and
 * left as input (idle-high) with internal pull-up so unused board pins
 * don't float. GPIOA[n] goes LOW when button n is pressed (active-low).
 */
#define MCP23017_BUTTON_MASK  0x0FU
#define MCP23017_STAGE1_BIT   (1U << 0)
#define MCP23017_STAGE2_BIT   (1U << 1)
#define MCP23017_SIDE1_BIT    (1U << 2)
#define MCP23017_SIDE2_BIT    (1U << 3)

/*
 * When a read fails, we allow this many consecutive failures before we
 * stop publishing the stale "pressed" state. After this, every button bit
 * reads as not-pressed to prevent a stale press latching the laser on in
 * the rare case of I2C bus corruption. The consumer (app.c control task)
 * separately raises LASER_CONTROLLER_FAULT_BUTTON_BOARD_I2C_LOST after a
 * longer failure window, but the fail-safe posture begins immediately on
 * the first lost read.
 */
#define LASER_CONTROLLER_BUTTONS_MAX_CONSECUTIVE_FAILURES 3U

static atomic_uint s_isr_fire_count = 0;

static esp_err_t laser_controller_buttons_write_reg(uint8_t reg, uint8_t value)
{
    const uint8_t tx[2] = { reg, value };
    return laser_controller_board_i2c_write(
        LASER_CONTROLLER_BUTTON_MCP23017_I2C_ADDR,
        tx,
        sizeof(tx));
}

static esp_err_t laser_controller_buttons_read_reg(uint8_t reg, uint8_t *value)
{
    const uint8_t tx[1] = { reg };
    return laser_controller_board_i2c_write_read(
        LASER_CONTROLLER_BUTTON_MCP23017_I2C_ADDR,
        tx,
        sizeof(tx),
        value,
        1U);
}

esp_err_t laser_controller_buttons_init(
    laser_controller_button_board_readback_t *readback)
{
    esp_err_t err;

    if (readback != NULL) {
        memset(readback, 0, sizeof(*readback));
    }

    err = laser_controller_board_i2c_probe(
        LASER_CONTROLLER_BUTTON_MCP23017_I2C_ADDR);
    if (err != ESP_OK) {
        if (readback != NULL) {
            readback->reachable = false;
            readback->configured = false;
            readback->last_error = (int32_t)err;
        }
        return err;
    }
    if (readback != NULL) {
        readback->reachable = true;
    }

    /*
     * Program the IOCON register FIRST so that subsequent register writes
     * assume the correct addressing mode (BANK=0) and INT electrical
     * behavior (open-drain, mirrored). Writing IOCON twice — the MCP23017
     * mirrors IOCONA and IOCONB at 0x0A and 0x0B respectively, both safe.
     */
    err = laser_controller_buttons_write_reg(
        MCP23017_REG_IOCON,
        MCP23017_IOCON_VALUE);
    if (err != ESP_OK) {
        goto config_failed;
    }

    /* All pins as inputs. We never drive from this chip. */
    err = laser_controller_buttons_write_reg(MCP23017_REG_IODIRA, 0xFFU);
    if (err != ESP_OK) {
        goto config_failed;
    }
    err = laser_controller_buttons_write_reg(MCP23017_REG_IODIRB, 0xFFU);
    if (err != ESP_OK) {
        goto config_failed;
    }

    /*
     * No hardware polarity inversion. The datasheet recommends leaving IPOL
     * at 0 and inverting in firmware so the "pressed" semantics are clear
     * at every call site.
     */
    err = laser_controller_buttons_write_reg(MCP23017_REG_IPOLA, 0x00U);
    if (err != ESP_OK) {
        goto config_failed;
    }

    /* Internal pull-up on the four button pins. Redundant with the board
     * 3V3 pull, but guards against an unseated cable where the on-board
     * pull network is not connected. */
    err = laser_controller_buttons_write_reg(
        MCP23017_REG_GPPUA,
        MCP23017_BUTTON_MASK);
    if (err != ESP_OK) {
        goto config_failed;
    }

    /*
     * Interrupt-on-change for A0..A3. INTCONA=0 means "interrupt on any
     * change from previous pin state" (not "compare against DEFVAL"); this
     * is edge-triggered in practice because the MCP latches the captured
     * value at the instant of the change. DEFVAL is still written to a
     * sensible value (0xFF = idle-high) for determinism.
     */
    err = laser_controller_buttons_write_reg(
        MCP23017_REG_DEFVALA,
        0xFFU);
    if (err != ESP_OK) {
        goto config_failed;
    }
    err = laser_controller_buttons_write_reg(
        MCP23017_REG_INTCONA,
        0x00U);
    if (err != ESP_OK) {
        goto config_failed;
    }
    err = laser_controller_buttons_write_reg(
        MCP23017_REG_GPINTENA,
        MCP23017_BUTTON_MASK);
    if (err != ESP_OK) {
        goto config_failed;
    }

    /*
     * Pre-read INTCAPA / GPIOA once to clear any pending interrupt that
     * might have latched during configuration (writes to GPINTEN can cause
     * a spurious edge if GPIOA != DEFVAL at the moment of enabling).
     */
    uint8_t discard = 0U;
    (void)laser_controller_buttons_read_reg(MCP23017_REG_INTCAPA, &discard);
    (void)laser_controller_buttons_read_reg(MCP23017_REG_GPIOA, &discard);

    if (readback != NULL) {
        readback->configured = true;
        readback->last_error = (int32_t)ESP_OK;
    }
    return ESP_OK;

config_failed:
    if (readback != NULL) {
        readback->configured = false;
        readback->last_error = (int32_t)err;
    }
    return err;
}

esp_err_t laser_controller_buttons_refresh(
    laser_controller_button_state_t *out,
    const laser_controller_button_state_t *prev,
    laser_controller_button_board_readback_t *readback)
{
    uint8_t gpioa = 0xFFU;
    uint8_t intcapa = 0xFFU;
    esp_err_t err;

    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * Reading INTCAPA clears any pending interrupt on the MCP side. It
     * returns the port value LATCHED at the instant the interrupt fired.
     * Reading GPIOA returns the CURRENT port value and also clears the
     * interrupt. We do both for diagnostics — but the source of truth for
     * the control-task decision is GPIOA (current state), not INTCAPA.
     */
    err = laser_controller_buttons_read_reg(MCP23017_REG_INTCAPA, &intcapa);
    if (err != ESP_OK) {
        goto read_failed;
    }
    err = laser_controller_buttons_read_reg(MCP23017_REG_GPIOA, &gpioa);
    if (err != ESP_OK) {
        goto read_failed;
    }

    /* Clear the failure counter on a successful round-trip. */
    if (readback != NULL) {
        readback->reachable = true;
        readback->configured = true;
        readback->last_gpioa = gpioa;
        readback->last_intcapa = intcapa;
        readback->last_error = (int32_t)ESP_OK;
        readback->consecutive_read_failures = 0U;
    }

    /*
     * Buttons are active-LOW. Invert the masked bits so "pressed" is true
     * when the physical switch is closed. Bits outside the mask are
     * ignored so stray floating behavior on unused A4..A7 does not leak.
     */
    const uint8_t pressed_bits = (uint8_t)(~gpioa) & MCP23017_BUTTON_MASK;
    const bool stage1 = (pressed_bits & MCP23017_STAGE1_BIT) != 0U;
    const bool stage2 = (pressed_bits & MCP23017_STAGE2_BIT) != 0U;
    const bool side1 = (pressed_bits & MCP23017_SIDE1_BIT) != 0U;
    const bool side2 = (pressed_bits & MCP23017_SIDE2_BIT) != 0U;

    out->stage1_pressed = stage1;
    out->stage2_pressed = stage2;
    out->side1_pressed = side1;
    out->side2_pressed = side2;
    out->stage1_edge = prev != NULL && stage1 && !prev->stage1_pressed;
    out->stage2_edge = prev != NULL && stage2 && !prev->stage2_pressed;
    out->side1_edge = prev != NULL && side1 && !prev->side1_pressed;
    out->side2_edge = prev != NULL && side2 && !prev->side2_pressed;
    out->board_reachable = true;
    out->isr_fire_count = laser_controller_buttons_get_isr_fire_count();
    return ESP_OK;

read_failed:
    /*
     * Fail-safe: on ANY bus failure, clear all pressed state immediately so
     * a stuck-bus condition cannot fire the laser. The host sees
     * board_reachable=false and the Integrate panel flags it; the app.c
     * control task will raise FAULT_BUTTON_BOARD_I2C_LOST after the
     * consecutive-failure threshold.
     */
    if (readback != NULL) {
        readback->last_error = (int32_t)err;
        readback->last_gpioa = 0xFFU;
        readback->last_intcapa = 0xFFU;
        if (readback->consecutive_read_failures <
                LASER_CONTROLLER_BUTTONS_MAX_CONSECUTIVE_FAILURES) {
            readback->consecutive_read_failures++;
        }
        if (readback->consecutive_read_failures >=
                LASER_CONTROLLER_BUTTONS_MAX_CONSECUTIVE_FAILURES) {
            readback->reachable = false;
        }
    }
    memset(out, 0, sizeof(*out));
    out->board_reachable = false;
    out->isr_fire_count = laser_controller_buttons_get_isr_fire_count();
    return err;
}

void laser_controller_buttons_on_isr_fired(void)
{
    atomic_fetch_add(&s_isr_fire_count, 1U);
}

uint32_t laser_controller_buttons_get_isr_fire_count(void)
{
    return (uint32_t)atomic_load(&s_isr_fire_count);
}
