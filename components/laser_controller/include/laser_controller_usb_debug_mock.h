#pragma once

/*
 * USB-Debug Mock Layer — synthesized telemetry for online testing
 *
 * Purpose
 * -------
 * When the controller is connected over USB-CDC for development, USB power
 * alone CANNOT bring up the TEC or LD MPM3530 rails (USB host typically
 * negotiates programming_only PD tier, ~5V at <=1.5A, well below threshold).
 * As a hardware consequence:
 *   - PWR_TEC_GOOD (GPIO16) stays LOW
 *   - PWR_LD_PGOOD (GPIO18) stays LOW
 *   - tec_temp_good, tec_telemetry_valid, ld_telemetry_valid all read FALSE
 *   - decision.allow_nir is unconditionally false (power_tier != FULL)
 *   - The deployment checklist cannot reach READY_POSTURE
 *
 * This module provides an opt-in, hard-isolated mock layer that synthesizes
 * the missing telemetry so the host GUI, state machine, and protocol-level
 * paths can be exercised end-to-end from a USB-only session.
 *
 * Hard-isolation guarantees (see .agent/AGENT.md "USB-Only Debug Power")
 * ----------------------------------------------------------------------
 * 1. Off by default. NEVER engages automatically.
 * 2. Activation requires (a) service mode active, AND (b) power_tier ==
 *    programming_only at the moment of activation, AND (c) explicit
 *    `service.usb_debug_mock_enable` command from the operator.
 * 3. Auto-disables AND latches FAULT_USB_DEBUG_MOCK_PD_CONFLICT the moment
 *    real PD power (any tier above programming_only) is detected.
 * 4. Auto-disables on any other fault transition, on service-mode exit, or
 *    on explicit `service.usb_debug_mock_disable`.
 * 5. NEVER drives any GPIO. Mock only substitutes input-readback values
 *    inside `laser_controller_board_read_inputs`, AFTER the safe-default
 *    output computation has happened. Outputs remain governed by
 *    `derive_outputs` and the normal apply path.
 * 6. State is owned exclusively by the control task. Comms-task entry
 *    points only set request flags; the control task picks them up.
 * 7. When inactive, the mock module is a single `if` branch in the safe
 *    path with no other cost.
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "laser_controller_board.h"
#include "laser_controller_types.h"

typedef struct {
    bool active;
    bool pd_conflict_latched;
    bool enable_pending;
    laser_controller_time_ms_t activated_at_ms;
    laser_controller_time_ms_t deactivated_at_ms;
    const char *last_disable_reason;
} laser_controller_usb_debug_mock_status_t;

/*
 * Comms-task entry point: request enable. Returns ESP_OK if the request
 * was queued for the control task to consume on its next tick. Returns
 * ESP_ERR_INVALID_STATE if either guard is unmet.
 *
 * Guards:
 *   - service_mode_active must be true
 *   - power_tier must be LASER_CONTROLLER_POWER_TIER_PROGRAMMING_ONLY
 *
 * Even after this returns ESP_OK, the control task re-validates the
 * guards before flipping `active` to true. This double-check prevents
 * any race where a comms request lands during a power-tier upgrade.
 */
esp_err_t laser_controller_usb_debug_mock_request_enable(
    bool service_mode_active,
    laser_controller_power_tier_t power_tier,
    laser_controller_time_ms_t now_ms);

/*
 * Comms-task entry point: request disable. Always accepted. The control
 * task processes the request on its next tick. `reason` is a static
 * string literal; the module stores the pointer, not a copy.
 */
void laser_controller_usb_debug_mock_request_disable(
    const char *reason,
    laser_controller_time_ms_t now_ms);

/*
 * Control-task entry point: process pending enable/disable requests
 * AND check for PD-conflict. Must be called from `run_fast_cycle`
 * BEFORE safety_evaluate runs.
 *
 * Returns true if a PD-conflict fault should be latched on this tick.
 * The caller (run_fast_cycle) is responsible for actually recording the
 * fault via laser_controller_record_fault — this function only signals.
 *
 * Side effects (control-task only):
 *   - Picks up pending enable / disable requests.
 *   - Auto-disables if power_tier upgrades while active.
 *   - Auto-disables if service_mode_active drops while active.
 */
bool laser_controller_usb_debug_mock_tick(
    bool service_mode_active,
    laser_controller_power_tier_t power_tier,
    laser_controller_time_ms_t now_ms);

/*
 * Control-task entry point: force disable on any other fault transition.
 * Idempotent. `reason` is stored as a pointer.
 */
void laser_controller_usb_debug_mock_force_disable_on_fault(
    const char *reason,
    laser_controller_time_ms_t now_ms);

/*
 * Read the current activity state. Safe to call from any task —
 * `s_active` is read-only after activation, and the value is naturally
 * atomic on ESP32-S3 for `bool`.
 */
bool laser_controller_usb_debug_mock_is_active(void);

/*
 * Read the full status struct. Same threading rules as is_active.
 */
void laser_controller_usb_debug_mock_get_status(
    laser_controller_usb_debug_mock_status_t *out);

/*
 * Clear the latched PD-conflict fault. Called as part of the global
 * `clear_faults` command path.
 */
void laser_controller_usb_debug_mock_clear_pd_conflict_latch(void);

/*
 * Control-task entry point: substitute synthesized telemetry into the
 * board inputs struct. Called from `run_fast_cycle` in app.c AFTER
 * `laser_controller_board_read_inputs` has populated all real reads AND
 * AFTER `classify_power_tier` has computed the live PD tier from the real
 * inputs. Early-returns if mock is inactive, so the safe path cost is
 * one branch.
 *
 * commanded_tec_target_c and commanded_ld_current_a are passed in so the
 * mock can run a closed-loop simulation that follows operator intent.
 */
void laser_controller_usb_debug_mock_apply_to_inputs(
    laser_controller_board_inputs_t *inputs,
    const laser_controller_board_outputs_t *commanded_outputs,
    laser_controller_celsius_t commanded_tec_target_c,
    laser_controller_amps_t commanded_ld_current_a,
    laser_controller_time_ms_t now_ms);
