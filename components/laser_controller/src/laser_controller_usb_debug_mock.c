#include "laser_controller_usb_debug_mock.h"

#include <stdatomic.h>
#include <string.h>

#include "laser_controller_board.h"
#include "laser_controller_logger.h"
#include "laser_controller_pinmap.h"

/*
 * Threading model:
 *   - s_enable_request_pending and s_disable_request_reason are written by
 *     the comms task and read/cleared by the control task. Atomic types
 *     guarantee tear-free reads on ESP32-S3.
 *   - All other state (s_active, s_pd_conflict_latched, s_simulated_*,
 *     s_activated_at_ms, s_deactivated_at_ms, s_last_disable_reason) is
 *     written ONLY by the control task. Comms task reads via the helpers
 *     `is_active` / `get_status`. `bool` reads are naturally tear-free
 *     and the snapshot is allowed to be a few ms old.
 *
 * Closed-loop simulation:
 *   - TEC: simulated_tec_temp_c follows the commanded target with a
 *     first-order response, time-constant ~1500ms. PGOOD asserts after
 *     a short startup delay.
 *   - LD: rail PGOOD asserts after a separate startup delay if SBDN is
 *     in OPERATE (not OFF, not STANDBY). LIO mirrors the commanded
 *     current when SBDN is OPERATE; zero otherwise. Driver loop_good is
 *     true when SBDN is OPERATE.
 */

#define USB_DEBUG_MOCK_TEC_RAIL_PGOOD_DELAY_MS  120U
#define USB_DEBUG_MOCK_LD_RAIL_PGOOD_DELAY_MS   80U
#define USB_DEBUG_MOCK_TEC_TEMP_GOOD_DELAY_MS   400U
#define USB_DEBUG_MOCK_TEC_TIME_CONSTANT_MS     1500U
#define USB_DEBUG_MOCK_AMBIENT_TEMP_C           23.0f

/* Comms-task → control-task request channel. */
static atomic_bool s_enable_request_pending = ATOMIC_VAR_INIT(false);
static const char *_Atomic s_disable_request_reason = ATOMIC_VAR_INIT(NULL);
/*
 * Clearing the PD-conflict latch comes from the global clear-faults path,
 * which runs on the comms task. The latch itself is control-task-owned,
 * so the comms-side `clear_pd_conflict_latch()` API only sets this atomic
 * request flag. The control task consumes it at the top of
 * `usb_debug_mock_tick()` before any other latch read or write.
 *
 * Bug fix 2026-04-14 (Agent B1 audit): previously the comms task wrote
 * `s_pd_conflict_latched` directly, which violated the threading model
 * declared at the top of this file.
 */
static atomic_bool s_clear_pd_conflict_request = ATOMIC_VAR_INIT(false);

/* Control-task-owned state. */
static bool s_active = false;
static bool s_pd_conflict_latched = false;
static laser_controller_time_ms_t s_activated_at_ms = 0;
static laser_controller_time_ms_t s_deactivated_at_ms = 0;
static const char *s_last_disable_reason = NULL;
static laser_controller_celsius_t s_simulated_tec_temp_c = USB_DEBUG_MOCK_AMBIENT_TEMP_C;
static laser_controller_time_ms_t s_last_tick_ms = 0;

static void laser_controller_usb_debug_mock_internal_disable(
    const char *reason,
    laser_controller_time_ms_t now_ms)
{
    if (!s_active) {
        return;
    }
    s_active = false;
    s_deactivated_at_ms = now_ms;
    s_last_disable_reason = reason != NULL ? reason : "unspecified";
    s_simulated_tec_temp_c = USB_DEBUG_MOCK_AMBIENT_TEMP_C;
    laser_controller_logger_logf(
        now_ms,
        "usb_mock",
        "DISABLED (%s)",
        s_last_disable_reason);
}

esp_err_t laser_controller_usb_debug_mock_request_enable(
    bool service_mode_active,
    laser_controller_power_tier_t power_tier,
    laser_controller_time_ms_t now_ms)
{
    /*
     * Comms-side guard. The control task re-validates these conditions on
     * the next tick before flipping s_active to true. The double-check
     * prevents a race where a comms request lands during a power-tier
     * upgrade.
     */
    if (!service_mode_active) {
        return ESP_ERR_INVALID_STATE;
    }
    if (power_tier != LASER_CONTROLLER_POWER_TIER_PROGRAMMING_ONLY) {
        return ESP_ERR_INVALID_STATE;
    }
    atomic_store(&s_enable_request_pending, true);
    laser_controller_logger_logf(
        now_ms,
        "usb_mock",
        "ENABLE requested (service=%d power_tier_idx=%d)",
        service_mode_active,
        (int)power_tier);
    return ESP_OK;
}

void laser_controller_usb_debug_mock_request_disable(
    const char *reason,
    laser_controller_time_ms_t now_ms)
{
    /*
     * Always accepted. Set the disable reason pointer; the control task
     * picks it up on its next tick.
     */
    atomic_store(&s_disable_request_reason,
                 reason != NULL ? reason : "operator request");
    /* Cancel any pending enable so a stale request cannot resurrect us. */
    atomic_store(&s_enable_request_pending, false);
    laser_controller_logger_logf(
        now_ms,
        "usb_mock",
        "DISABLE requested (%s)",
        reason != NULL ? reason : "operator request");
}

bool laser_controller_usb_debug_mock_tick(
    bool service_mode_active,
    laser_controller_power_tier_t power_tier,
    laser_controller_time_ms_t now_ms)
{
    bool latch_pd_conflict = false;

    /*
     * 0. Consume the comms-side clear-PD-conflict request before any latch
     *    read or write below. This makes the comms-task → control-task
     *    handoff atomic-flag-only; the control task remains the sole
     *    writer of `s_pd_conflict_latched`.
     */
    if (atomic_exchange(&s_clear_pd_conflict_request, false)) {
        s_pd_conflict_latched = false;
        laser_controller_logger_log(
            now_ms,
            "usb_mock",
            "PD-conflict latch cleared (consumed comms-side request)");
    }

    /* 1. Process pending disable first — it always wins. */
    const char *disable_reason = atomic_exchange(&s_disable_request_reason, NULL);
    if (disable_reason != NULL) {
        laser_controller_usb_debug_mock_internal_disable(disable_reason, now_ms);
    }

    /* 2. PD-conflict check has highest priority while active. */
    if (s_active && power_tier != LASER_CONTROLLER_POWER_TIER_PROGRAMMING_ONLY &&
        power_tier != LASER_CONTROLLER_POWER_TIER_UNKNOWN &&
        power_tier != LASER_CONTROLLER_POWER_TIER_INSUFFICIENT) {
        if (!s_pd_conflict_latched) {
            s_pd_conflict_latched = true;
            latch_pd_conflict = true;
        }
        laser_controller_usb_debug_mock_internal_disable(
            "real PD power detected (auto-disable + fault latched)",
            now_ms);
    }

    /* 3. Service-mode exit auto-disables. */
    if (s_active && !service_mode_active) {
        laser_controller_usb_debug_mock_internal_disable(
            "service mode exited",
            now_ms);
    }

    /* 4. Process pending enable. Re-validate guards here. */
    if (atomic_exchange(&s_enable_request_pending, false)) {
        if (s_pd_conflict_latched) {
            laser_controller_logger_log(
                now_ms,
                "usb_mock",
                "ENABLE rejected: PD-conflict fault still latched");
        } else if (!service_mode_active) {
            laser_controller_logger_log(
                now_ms,
                "usb_mock",
                "ENABLE rejected: service mode no longer active");
        } else if (power_tier != LASER_CONTROLLER_POWER_TIER_PROGRAMMING_ONLY) {
            laser_controller_logger_log(
                now_ms,
                "usb_mock",
                "ENABLE rejected: power_tier upgraded since request");
        } else if (!s_active) {
            s_active = true;
            s_activated_at_ms = now_ms;
            s_last_disable_reason = NULL;
            s_simulated_tec_temp_c = USB_DEBUG_MOCK_AMBIENT_TEMP_C;
            s_last_tick_ms = now_ms;
            laser_controller_logger_log(
                now_ms,
                "usb_mock",
                "ACTIVE — telemetry is synthesized, no real rails");
        }
    }

    return latch_pd_conflict;
}

void laser_controller_usb_debug_mock_force_disable_on_fault(
    const char *reason,
    laser_controller_time_ms_t now_ms)
{
    if (!s_active) {
        return;
    }
    laser_controller_usb_debug_mock_internal_disable(
        reason != NULL ? reason : "fault transition",
        now_ms);
}

bool laser_controller_usb_debug_mock_is_active(void)
{
    return s_active;
}

void laser_controller_usb_debug_mock_get_status(
    laser_controller_usb_debug_mock_status_t *out)
{
    if (out == NULL) {
        return;
    }
    out->active = s_active;
    out->pd_conflict_latched = s_pd_conflict_latched;
    out->enable_pending = atomic_load(&s_enable_request_pending);
    out->activated_at_ms = s_activated_at_ms;
    out->deactivated_at_ms = s_deactivated_at_ms;
    out->last_disable_reason = s_last_disable_reason;
}

void laser_controller_usb_debug_mock_clear_pd_conflict_latch(void)
{
    /*
     * Comms-task entry point. The latch itself is control-task-owned; we
     * only set the request atomic. The control task consumes it at the top
     * of the next `usb_debug_mock_tick()` call.
     */
    atomic_store(&s_clear_pd_conflict_request, true);
}

void laser_controller_usb_debug_mock_apply_to_inputs(
    laser_controller_board_inputs_t *inputs,
    const laser_controller_board_outputs_t *commanded_outputs,
    laser_controller_celsius_t commanded_tec_target_c,
    laser_controller_amps_t commanded_ld_current_a,
    laser_controller_time_ms_t now_ms)
{
    if (!s_active || inputs == NULL || commanded_outputs == NULL) {
        return;
    }

    const laser_controller_time_ms_t age_since_activation_ms =
        now_ms >= s_activated_at_ms ? (now_ms - s_activated_at_ms) : 0U;
    const laser_controller_time_ms_t dt_ms =
        now_ms >= s_last_tick_ms ? (now_ms - s_last_tick_ms) : 0U;
    s_last_tick_ms = now_ms;

    /* TEC temperature first-order response toward target. */
    if (dt_ms > 0U && commanded_outputs->enable_tec_vin) {
        const float alpha =
            (float)dt_ms / (float)(USB_DEBUG_MOCK_TEC_TIME_CONSTANT_MS + dt_ms);
        s_simulated_tec_temp_c +=
            alpha * (commanded_tec_target_c - s_simulated_tec_temp_c);
    } else if (!commanded_outputs->enable_tec_vin) {
        /* Drift toward ambient when TEC rail is commanded off. */
        const float alpha =
            (float)dt_ms / (float)(USB_DEBUG_MOCK_TEC_TIME_CONSTANT_MS * 2U + dt_ms);
        s_simulated_tec_temp_c +=
            alpha * (USB_DEBUG_MOCK_AMBIENT_TEMP_C - s_simulated_tec_temp_c);
    }

    /* TEC rail PGOOD synthesized when commanded enable + delay. */
    const bool tec_pgood_synth =
        commanded_outputs->enable_tec_vin &&
        age_since_activation_ms >= USB_DEBUG_MOCK_TEC_RAIL_PGOOD_DELAY_MS;
    /* TEC temp_good when within ±1°C of target after settling delay. */
    const float temp_err =
        s_simulated_tec_temp_c - commanded_tec_target_c;
    const bool tec_temp_good_synth =
        tec_pgood_synth &&
        age_since_activation_ms >= USB_DEBUG_MOCK_TEC_TEMP_GOOD_DELAY_MS &&
        temp_err > -1.0f && temp_err < 1.0f;

    /* LD rail PGOOD synthesized when commanded enable + delay. */
    const bool ld_pgood_synth =
        commanded_outputs->enable_ld_vin &&
        age_since_activation_ms >= USB_DEBUG_MOCK_LD_RAIL_PGOOD_DELAY_MS;

    /* SBDN posture: only OPERATE counts as "armed". */
    const bool sbdn_operate =
        commanded_outputs->sbdn_state == LASER_CONTROLLER_SBDN_STATE_ON;
    const bool pcn_high_current =
        !commanded_outputs->select_driver_low_current;

    inputs->tec_rail_pgood = tec_pgood_synth;
    inputs->tec_telemetry_valid = tec_pgood_synth;
    inputs->tec_temp_good = tec_temp_good_synth;
    inputs->tec_temp_c = s_simulated_tec_temp_c;
    /* Linear back-translation of temp → ADC voltage using the live TEC
     * calibration assumption. We use a simple slope so the GUI shows
     * non-zero values. The exact polynomial does not matter for mock. */
    inputs->tec_temp_adc_voltage_v = 0.18f + (s_simulated_tec_temp_c - 6.0f) * 0.038f;
    inputs->tec_voltage_v = tec_pgood_synth ? 1.2f : 0.0f;
    inputs->tec_current_a = tec_pgood_synth ?
        (commanded_tec_target_c < USB_DEBUG_MOCK_AMBIENT_TEMP_C ? -0.3f : 0.3f) : 0.0f;

    inputs->ld_rail_pgood = ld_pgood_synth;
    inputs->driver_loop_good = sbdn_operate && ld_pgood_synth;
    if (sbdn_operate && ld_pgood_synth) {
        const float synth_current =
            pcn_high_current ? commanded_ld_current_a : 0.05f;
        inputs->measured_laser_current_a = synth_current;
        /* LIO scales ~ 0.4V per Amp on this board for the mock. */
        inputs->laser_current_monitor_voltage_v = synth_current * 0.4f;
    } else {
        inputs->measured_laser_current_a = 0.0f;
        inputs->laser_current_monitor_voltage_v = 0.0f;
    }
    inputs->laser_driver_temp_voltage_v = 0.45f;
    inputs->laser_driver_temp_c = 28.0f;
}
