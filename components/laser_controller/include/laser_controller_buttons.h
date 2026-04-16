/*
 * MCP23017 button-board driver for the BSL laser controller.
 *
 * Hardware:
 *   MCP23017 I2C GPIO expander @ 0x20 (A2:A0 = GND).
 *   I2C bus: shared GPIO4/5, same bus as DAC80502, DRV2605, STUSB4500,
 *   VL53L1X, and TLC59116.
 *   INTA output -> ESP32 GPIO7 (open-drain, active-low, wire-OR-compatible
 *   but this net is single-sourced as of 2026-04-15).
 *
 * Pin assignments (MCP23017 side):
 *   GPA0  Main trigger stage 1 (shallow press)
 *   GPA1  Main trigger stage 2 (deep press; stage2 implies stage1)
 *   GPA2  Side button 1 (LED brightness +10%)
 *   GPA3  Side button 2 (LED brightness -10%)
 *   GPA4..GPA7, GPB0..GPB7  unused, configured as input with pull-up.
 *
 * All four buttons pull-to-GND when pressed; idle state is HIGH via a 3V3
 * net pull plus MCP23017 internal pull-up. Firmware inverts the GPIOA read
 * to "pressed".
 *
 * Driver ownership:
 *   - `laser_controller_buttons_init` is called once from
 *     `laser_controller_board_init_safe_defaults` during firmware start.
 *   - `laser_controller_buttons_refresh` is called on every control-task
 *     tick from `laser_controller_board_read_inputs`. It performs the
 *     I2C read and publishes `stateOut` into the caller's
 *     `laser_controller_button_state_t`. It is also responsible for edge
 *     detection against the previous tick's state.
 *   - `laser_controller_buttons_isr_fired` is incremented by the GPIO7 ISR
 *     (board.c installs the ISR). Its role is purely diagnostic — the
 *     control task does not use it to decide whether to poll.
 *   - The driver NEVER writes to any ESP32 GPIO peripheral. All hardware
 *     side effects go through the existing I2C primitives in board.c.
 *
 * Threading model:
 *   - `laser_controller_buttons_init` runs on the main task at boot.
 *   - `laser_controller_buttons_refresh` runs on the control task (5 ms,
 *     priority 20). It is the ONLY caller of the I2C layer for this chip.
 *   - `laser_controller_buttons_isr_fired` is incremented from ISR context.
 *     The returned value is read from the control task only.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "laser_controller_types.h"

typedef struct {
    /* Reachability probed at boot (and re-probed if we fail N reads). */
    bool reachable;
    /* Configuration steps completed successfully. */
    bool configured;
    /* Raw GPIOA register value at the last read (for diagnostics / host). */
    uint8_t last_gpioa;
    /* Interrupt-capture register at the last read (diagnostic). */
    uint8_t last_intcapa;
    /* Most recent I2C error code (ESP_OK when healthy). */
    int32_t last_error;
    /* Consecutive failed reads since last good read. */
    uint32_t consecutive_read_failures;
} laser_controller_button_board_readback_t;

/*
 * Called once during board bring-up. Probes the MCP23017 and writes the
 * static configuration registers (IODIR, IPOL, GPPU, GPINTEN, DEFVAL,
 * INTCON, IOCON). On success sets `reachable` and `configured` in the
 * readback struct. On failure returns the esp_err_t — the caller should
 * still boot the rest of the system; the button board is reported as
 * missing via telemetry.
 */
esp_err_t laser_controller_buttons_init(
    laser_controller_button_board_readback_t *readback);

/*
 * Control-task-only. Re-reads GPIOA, derives pressed/edge state against
 * the caller's previous-tick snapshot, and populates `out`. Updates
 * `readback` with the latest raw register values. Returns ESP_OK if the
 * I2C transaction succeeded. On failure, `out` is left with board_reachable
 * = false and the previous pressed state cleared to prevent stale "still
 * pressed" reads from driving the laser after a cable unseats.
 */
esp_err_t laser_controller_buttons_refresh(
    laser_controller_button_state_t *out,
    const laser_controller_button_state_t *prev,
    laser_controller_button_board_readback_t *readback);

/*
 * ISR-context only. Called from the GPIO7 interrupt handler in board.c.
 * Pure bookkeeping — increments a counter the control task can observe.
 * The I2C read itself happens in the control task via
 * `laser_controller_buttons_refresh`.
 */
void laser_controller_buttons_on_isr_fired(void);

/*
 * Monotonic count of ISR fires since boot. Published as telemetry.
 */
uint32_t laser_controller_buttons_get_isr_fire_count(void);
