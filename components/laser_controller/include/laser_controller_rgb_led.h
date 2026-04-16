/*
 * TLC59116 RGB-status-LED driver for the BSL button board.
 *
 * Hardware:
 *   TLC59116 I2C LED driver @ 0x60 (A3..A0 = GND).
 *   I2C bus: shared GPIO4/5 (same bus as MCP23017, DAC80502, DRV2605,
 *   STUSB4500, VL53L1X).
 *   Default ALLCALL address is 0x68 and is NOT collision-risky with any
 *   current device on the bus.
 *
 * RGB wiring (non-standard channel order per the button board schematic):
 *   OUT0 = BLUE channel
 *   OUT1 = RED channel
 *   OUT2 = GREEN channel
 *   OUT3..15 unused — driven off.
 *
 * The driver uses hardware group-blinking for the two flashing states:
 *   GRPFREQ=23 gives a 1 s period.
 *   GRPPWM=128 gives 50% duty — 0.5 s on / 0.5 s off per user directive.
 * Flashing is controlled by MODE2.DMBLNK: 1=blinking, 0=dimming/solid.
 *
 * Color state is computed by the app-layer RGB policy helper. This driver
 * is a dumb sink — it just applies the state to the chip. It performs a
 * dirty-compare so repeated `apply()` calls with unchanged state do not
 * generate bus traffic.
 *
 * Threading model:
 *   - `laser_controller_rgb_led_init` runs once on the main task.
 *   - `laser_controller_rgb_led_apply` runs on the control task (5 ms) as
 *     part of `apply_outputs`. No other caller.
 *   - `laser_controller_rgb_led_diagnose` is a helper the control task can
 *     call periodically to refresh the readback struct (reads MODE1).
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    /* RGB intensity 0..255 per channel. "Enabled"=false means all off. */
    uint8_t r;
    uint8_t g;
    uint8_t b;
    /* When true, hardware group-blinking is applied. Period is fixed. */
    bool blink;
    /* When false, the channel outputs are all-off regardless of R/G/B. */
    bool enabled;
} laser_controller_rgb_led_state_t;

typedef struct {
    bool reachable;
    bool configured;
    uint8_t mode1_reg;
    uint8_t mode2_reg;
    uint8_t ledout0_reg;
    /* Last applied state (for dirty-compare). */
    laser_controller_rgb_led_state_t last_applied;
    int32_t last_error;
} laser_controller_rgb_led_readback_t;

/*
 * Probes the TLC59116, writes the static configuration (MODE1 awake,
 * MODE2 dimming by default, LEDOUT0 mode-3 for OUT0/1/2, GRPPWM/GRPFREQ
 * for the blink preset). On success sets reachable and configured.
 * On failure returns the esp_err_t; the caller should continue boot.
 */
esp_err_t laser_controller_rgb_led_init(
    laser_controller_rgb_led_readback_t *readback);

/*
 * Control-task-only. Applies the given state to the LED. No-op if the
 * state is byte-identical to the last applied state. Returns ESP_OK on
 * success. On failure, updates readback->last_error and leaves
 * last_applied unchanged so the next call retries.
 */
esp_err_t laser_controller_rgb_led_apply(
    const laser_controller_rgb_led_state_t *state,
    laser_controller_rgb_led_readback_t *readback);

/*
 * Drives the LED fully off immediately (safe-default call path). Used by
 * board init before the policy helper has had a chance to publish a
 * real state.
 */
esp_err_t laser_controller_rgb_led_force_off(
    laser_controller_rgb_led_readback_t *readback);

/*
 * Two-state comparison used by the dirty-compare. Exposed for tests.
 */
bool laser_controller_rgb_led_state_equal(
    const laser_controller_rgb_led_state_t *a,
    const laser_controller_rgb_led_state_t *b);
