#include "laser_controller_rgb_led.h"

#include <stddef.h>
#include <string.h>

#include "laser_controller_board.h"
#include "laser_controller_pinmap.h"

/*
 * TLC59116 register map (datasheet Rev. C, Table 9).
 * We use the control byte's auto-increment bit sparingly — most writes
 * here are single-register so AI is left off.
 */
#define TLC59116_REG_MODE1    0x00U
#define TLC59116_REG_MODE2    0x01U
#define TLC59116_REG_PWM0     0x02U
#define TLC59116_REG_PWM1     0x03U
#define TLC59116_REG_PWM2     0x04U
#define TLC59116_REG_GRPPWM   0x12U
#define TLC59116_REG_GRPFREQ  0x13U
#define TLC59116_REG_LEDOUT0  0x14U
#define TLC59116_REG_LEDOUT1  0x15U
#define TLC59116_REG_LEDOUT2  0x16U
#define TLC59116_REG_LEDOUT3  0x17U

/*
 * MODE1 bit layout (TLC59116 datasheet SLDS157E Table 4):
 *   bit7 = AI1  (auto-increment bit 1)
 *   bit6 = AI0  (auto-increment bit 0)
 *   bit5 = AI   (auto-increment on / off)
 *   bit4 = OSC  (1 = oscillator off — chip idle; previously named SLEEP in
 *                 older datasheet revisions; same polarity)
 *   bit3 = SUB1  (sub-addresses disabled unless set)
 *   bit2 = SUB2
 *   bit1 = SUB3
 *   bit0 = ALLCALL (1 = respond to ALLCALL address)
 *
 * We want OSC=0 (oscillator on so PWM runs), ALLCALL=1 (default, harmless
 * for our single-chip case), AI=0 (we write single regs).
 */
#define TLC59116_MODE1_AWAKE  0x01U

/*
 * MODE2 bit layout (TLC59116 datasheet SLDS157E Table 5):
 *   bit7    = EFCLR (1 = clear output error status on read — unused)
 *   bit6    = reserved (keep 0)
 *   bit5    = DMBLNK (0 = group PWM dimming, 1 = group blinking)
 *   bit4    = reserved (keep 0)
 *   bit3    = OCH (output-change-on-ack vs on-stop — leave default 0)
 *   bit2..0 = reserved (keep 0)
 *
 * Verified against the local copy at docs/Datasheets/tlc59116.pdf p.16
 * (hardware-safety audit 2026-04-15). The earlier docstring had EFCLR at
 * bit5 and DMBLNK at bit4 — the macro `TLC59116_MODE2_BLINK = (1U << 5)`
 * was always correct (it sets DMBLNK), only the comment was wrong.
 */
#define TLC59116_MODE2_SOLID  0x00U
#define TLC59116_MODE2_BLINK  (1U << 5)

/*
 * LEDOUTn register has two bits per output:
 *   00 = driver off
 *   01 = driver fully on (ignore PWM)
 *   10 = individual PWM control
 *   11 = individual PWM control + group dimming/blinking
 *
 * We use mode 3 (11) on OUT0, OUT1, OUT2 so the group blink register can
 * flash the whole RGB cluster together. OUT3 stays off (00).
 */
#define TLC59116_LEDOUT0_RGB_GROUP  0b00111111U  /* OUT3=00, OUT2=11, OUT1=11, OUT0=11 */
#define TLC59116_LEDOUT0_OFF        0x00U

/*
 * Group-blink preset: 1 s period. User directive 2026-04-15 "default
 * total brightness equal to 80":
 *   - Solid mode (MODE2.DMBLNK=0): GRPPWM scales all group LEDs
 *     proportionally, so GRPPWM=80 gives a dimmer, safer status color.
 *   - Blink mode (MODE2.DMBLNK=1): GRPPWM is the ON-phase duty. Keep
 *     it at 128 (50% duty) so the 0.5 s on / 0.5 s off timing from the
 *     original user directive holds regardless of the solid-mode dim.
 */
#define TLC59116_GRPFREQ_1HZ        23U
#define TLC59116_GRPPWM_SOLID       80U
#define TLC59116_GRPPWM_BLINK_DUTY  128U

/*
 * RGB → TLC59116 channel mapping per the button board schematic:
 *   PWM0 = B (OUT0)
 *   PWM1 = R (OUT1)
 *   PWM2 = G (OUT2)
 */

/*
 * Per-channel brightness scaling (2026-04-17 user directive).
 * The RGB die in the button-board module has very unequal native
 * efficiency: at the same PWM duty, red emits much less perceived
 * brightness than green/blue. Without scaling, "red" looks dim,
 * "blue" overwhelms, and mixed colors (orange, white) read as cyan.
 *
 * Scale each channel by these percentages before writing PWM. Red is
 * the limiter so it stays at 100; green and blue are dimmed to roughly
 * match red's perceived brightness. These values are reasonable
 * starting points — tune by eye against the bench unit if needed.
 */
#define TLC59116_RED_SCALE_PCT     100U
#define TLC59116_GREEN_SCALE_PCT    45U
#define TLC59116_BLUE_SCALE_PCT     35U

static uint8_t laser_controller_rgb_led_scale(uint8_t value, uint32_t scale_pct)
{
    const uint32_t scaled = ((uint32_t)value * scale_pct) / 100U;
    return (uint8_t)(scaled > 255U ? 255U : scaled);
}

static esp_err_t laser_controller_rgb_led_write_reg(uint8_t reg, uint8_t value)
{
    const uint8_t tx[2] = { reg, value };
    return laser_controller_board_i2c_write(
        LASER_CONTROLLER_BUTTON_TLC59116_I2C_ADDR,
        tx,
        sizeof(tx));
}

static esp_err_t laser_controller_rgb_led_read_reg(uint8_t reg, uint8_t *value)
{
    const uint8_t tx[1] = { reg };
    return laser_controller_board_i2c_write_read(
        LASER_CONTROLLER_BUTTON_TLC59116_I2C_ADDR,
        tx,
        sizeof(tx),
        value,
        1U);
}

bool laser_controller_rgb_led_state_equal(
    const laser_controller_rgb_led_state_t *a,
    const laser_controller_rgb_led_state_t *b)
{
    if (a == NULL || b == NULL) {
        return false;
    }
    if (a->enabled != b->enabled) {
        return false;
    }
    if (a->blink != b->blink) {
        return false;
    }
    if (!a->enabled) {
        /* When both are disabled, channel values do not matter. */
        return true;
    }
    return a->r == b->r && a->g == b->g && a->b == b->b;
}

esp_err_t laser_controller_rgb_led_init(
    laser_controller_rgb_led_readback_t *readback)
{
    esp_err_t err;

    if (readback != NULL) {
        memset(readback, 0, sizeof(*readback));
    }

    err = laser_controller_board_i2c_probe(
        LASER_CONTROLLER_BUTTON_TLC59116_I2C_ADDR);
    if (err != ESP_OK) {
        if (readback != NULL) {
            readback->last_error = (int32_t)err;
        }
        return err;
    }
    if (readback != NULL) {
        readback->reachable = true;
    }

    /* Wake the oscillator; keep ALLCALL enabled. */
    err = laser_controller_rgb_led_write_reg(
        TLC59116_REG_MODE1,
        TLC59116_MODE1_AWAKE);
    if (err != ESP_OK) {
        goto config_failed;
    }

    /* Default posture: solid (non-blinking). blink flips MODE2 at apply time. */
    err = laser_controller_rgb_led_write_reg(
        TLC59116_REG_MODE2,
        TLC59116_MODE2_SOLID);
    if (err != ESP_OK) {
        goto config_failed;
    }

    /*
     * Program the group blink period/duty presets. They are ignored when
     * MODE2.DMBLNK=0, so we can write them once at init. Changing blink
     * state at runtime costs a single MODE2 write.
     */
    err = laser_controller_rgb_led_write_reg(
        TLC59116_REG_GRPPWM,
        TLC59116_GRPPWM_SOLID);
    if (err != ESP_OK) {
        goto config_failed;
    }
    err = laser_controller_rgb_led_write_reg(
        TLC59116_REG_GRPFREQ,
        TLC59116_GRPFREQ_1HZ);
    if (err != ESP_OK) {
        goto config_failed;
    }

    /*
     * Route OUT0/1/2 through individual+group PWM. Leave OUT3..15 off.
     * LEDOUT0 covers OUT0..3, LEDOUT1..3 cover 4..15 — all kept off.
     */
    err = laser_controller_rgb_led_write_reg(
        TLC59116_REG_LEDOUT0,
        TLC59116_LEDOUT0_RGB_GROUP);
    if (err != ESP_OK) {
        goto config_failed;
    }
    err = laser_controller_rgb_led_write_reg(TLC59116_REG_LEDOUT1, 0x00U);
    if (err != ESP_OK) {
        goto config_failed;
    }
    err = laser_controller_rgb_led_write_reg(TLC59116_REG_LEDOUT2, 0x00U);
    if (err != ESP_OK) {
        goto config_failed;
    }
    err = laser_controller_rgb_led_write_reg(TLC59116_REG_LEDOUT3, 0x00U);
    if (err != ESP_OK) {
        goto config_failed;
    }

    /* PWM channels to zero until the app-layer policy publishes a state. */
    err = laser_controller_rgb_led_write_reg(TLC59116_REG_PWM0, 0U);
    if (err != ESP_OK) {
        goto config_failed;
    }
    err = laser_controller_rgb_led_write_reg(TLC59116_REG_PWM1, 0U);
    if (err != ESP_OK) {
        goto config_failed;
    }
    err = laser_controller_rgb_led_write_reg(TLC59116_REG_PWM2, 0U);
    if (err != ESP_OK) {
        goto config_failed;
    }

    if (readback != NULL) {
        readback->configured = true;
        readback->mode1_reg = TLC59116_MODE1_AWAKE;
        readback->mode2_reg = TLC59116_MODE2_SOLID;
        readback->ledout0_reg = TLC59116_LEDOUT0_RGB_GROUP;
        readback->last_error = (int32_t)ESP_OK;
        readback->last_applied = (laser_controller_rgb_led_state_t){
            .r = 0U,
            .g = 0U,
            .b = 0U,
            .blink = false,
            .enabled = false,
        };
    }
    return ESP_OK;

config_failed:
    if (readback != NULL) {
        readback->configured = false;
        readback->last_error = (int32_t)err;
    }
    return err;
}

esp_err_t laser_controller_rgb_led_apply(
    const laser_controller_rgb_led_state_t *state,
    laser_controller_rgb_led_readback_t *readback)
{
    esp_err_t err;
    laser_controller_rgb_led_state_t effective;

    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    effective = *state;
    if (!effective.enabled) {
        effective.r = 0U;
        effective.g = 0U;
        effective.b = 0U;
        effective.blink = false;
    } else {
        /*
         * Apply per-channel scaling so red, green, and blue read with
         * roughly matched perceived brightness on the actual button-board
         * RGB die. App-level color choices (e.g. orange = 255,140,0) are
         * pre-tuned for the post-scale output. (2026-04-17)
         */
        effective.r = laser_controller_rgb_led_scale(
            effective.r, TLC59116_RED_SCALE_PCT);
        effective.g = laser_controller_rgb_led_scale(
            effective.g, TLC59116_GREEN_SCALE_PCT);
        effective.b = laser_controller_rgb_led_scale(
            effective.b, TLC59116_BLUE_SCALE_PCT);
    }

    /* Dirty-compare: skip bus traffic when nothing changed. */
    if (readback != NULL &&
        readback->reachable &&
        readback->configured &&
        laser_controller_rgb_led_state_equal(&effective, &readback->last_applied)) {
        return ESP_OK;
    }

    /*
     * Order:
     *   1. PWM values (so the new color is staged before we flip the
     *      output routing from off to on).
     *   2. MODE2 (blink vs solid) — safe to toggle regardless of LEDOUTn.
     *   3. LEDOUT0 — this is the "visible" switch. Writing mode 11 while
     *      PWM is zero keeps the LED dark; bringing PWM up after is a
     *      single-register write on the next apply so we don't flash
     *      unexpected colors.
     */
    err = laser_controller_rgb_led_write_reg(
        TLC59116_REG_PWM0,  /* B */
        effective.b);
    if (err != ESP_OK) {
        goto apply_failed;
    }
    err = laser_controller_rgb_led_write_reg(
        TLC59116_REG_PWM1,  /* R */
        effective.r);
    if (err != ESP_OK) {
        goto apply_failed;
    }
    err = laser_controller_rgb_led_write_reg(
        TLC59116_REG_PWM2,  /* G */
        effective.g);
    if (err != ESP_OK) {
        goto apply_failed;
    }

    /*
     * Write GRPPWM to match the current mode. In solid mode GRPPWM is a
     * brightness multiplier (80/255 ≈ 31% total per user directive
     * 2026-04-15). In blink mode GRPPWM is the ON-phase duty and must
     * stay at 128/255 = 50% to preserve the 0.5 s on / 0.5 s off timing
     * from the original blink directive. Two registers flip together
     * on any solid↔blink transition; both writes are one byte each.
     */
    const uint8_t grppwm_value =
        effective.blink ? TLC59116_GRPPWM_BLINK_DUTY : TLC59116_GRPPWM_SOLID;
    err = laser_controller_rgb_led_write_reg(TLC59116_REG_GRPPWM, grppwm_value);
    if (err != ESP_OK) {
        goto apply_failed;
    }

    const uint8_t mode2_value =
        effective.blink ? TLC59116_MODE2_BLINK : TLC59116_MODE2_SOLID;
    err = laser_controller_rgb_led_write_reg(TLC59116_REG_MODE2, mode2_value);
    if (err != ESP_OK) {
        goto apply_failed;
    }

    const uint8_t ledout0_value =
        effective.enabled ?
            TLC59116_LEDOUT0_RGB_GROUP :
            TLC59116_LEDOUT0_OFF;
    err = laser_controller_rgb_led_write_reg(TLC59116_REG_LEDOUT0, ledout0_value);
    if (err != ESP_OK) {
        goto apply_failed;
    }

    if (readback != NULL) {
        readback->reachable = true;
        readback->configured = true;
        readback->mode2_reg = mode2_value;
        readback->ledout0_reg = ledout0_value;
        readback->last_error = (int32_t)ESP_OK;
        readback->last_applied = effective;
    }
    return ESP_OK;

apply_failed:
    if (readback != NULL) {
        readback->last_error = (int32_t)err;
    }
    return err;
}

esp_err_t laser_controller_rgb_led_force_off(
    laser_controller_rgb_led_readback_t *readback)
{
    const laser_controller_rgb_led_state_t off = {
        .r = 0U,
        .g = 0U,
        .b = 0U,
        .blink = false,
        .enabled = false,
    };
    /*
     * Invalidate dirty-compare so the write actually goes to the bus even
     * if the previous applied state was also "off".
     */
    if (readback != NULL) {
        readback->last_applied.enabled = true;  /* force mismatch */
    }
    return laser_controller_rgb_led_apply(&off, readback);
}

/*
 * Unused helper reserved for future diagnostic readback (unit-test hook).
 * Kept static-free because the compiler strips unused non-static functions
 * under -Werror only when -Wunused-function is set — we pass -Wall -Wextra
 * -Werror and rely on the function being referenced by board.c via the
 * header when diagnostics are added.
 */
esp_err_t laser_controller_rgb_led_refresh_mode1(
    laser_controller_rgb_led_readback_t *readback)
{
    uint8_t value = 0U;
    const esp_err_t err = laser_controller_rgb_led_read_reg(
        TLC59116_REG_MODE1,
        &value);
    if (readback != NULL) {
        if (err == ESP_OK) {
            readback->mode1_reg = value;
        }
        readback->last_error = (int32_t)err;
    }
    return err;
}
