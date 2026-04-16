#include "laser_controller_safety.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/*
 * Removed `laser_controller_power_allows_alignment`: green laser is now
 * ungated at the software level per user directive 2026-04-14. Hardware
 * rail availability (TPS22918 load switch on VDS_TEC_5V0) is the only
 * remaining gate, and that is not a firmware concern.
 */

/*
 * Minimum time both `ld_rail_pgood` AND `sbdn_state != OFF` must be stable
 * before the LD driver thermistor ADC read is trusted for the overtemp
 * check. The thermistor path goes through a voltage divider + the driver's
 * internal reference buffer; the first few samples after rail rise or
 * SBDN transition read garbage (often > 100 C). User directive
 * 2026-04-15 (late).
 */
#define LASER_CONTROLLER_LD_TEMP_SETTLE_MS 2000U

static bool laser_controller_thermal_supervision_active(
    const laser_controller_config_t *config,
    const laser_controller_safety_snapshot_t *snapshot)
{
    const laser_controller_board_inputs_t *hw =
        snapshot != NULL ? snapshot->hw : NULL;

    if (config == NULL || snapshot == NULL) {
        return false;
    }

    if (hw == NULL) {
        return false;
    }

    /*
     * Lambda/TEC thermal interlocks are only meaningful once the closed-loop TEC
     * path is actually part of the live NIR readiness path. Bench bring-up on a
     * 5 V host cable or in service mode should stay safe-off without presenting a
     * misleading thermal trip as the active fault.
     */
    if (snapshot->service_mode_requested || snapshot->service_mode_active ||
        snapshot->power_tier != LASER_CONTROLLER_POWER_TIER_FULL ||
        !config->require_tec_for_nir) {
        return false;
    }

    return hw->tec_rail_pgood ||
           hw->tec_temp_good ||
           hw->tec_voltage_v > 0.05f ||
           hw->tec_current_a > 0.05f ||
           hw->tec_current_a < -0.05f;
}

static bool laser_controller_horizon_blocked(
    const laser_controller_config_t *config,
    const laser_controller_safety_snapshot_t *snapshot)
{
    const laser_controller_board_inputs_t *hw =
        snapshot != NULL ? snapshot->hw : NULL;
    const laser_controller_radians_t trip_threshold =
        config->thresholds.horizon_threshold_rad;
    const laser_controller_radians_t clear_threshold =
        config->thresholds.horizon_threshold_rad -
        config->thresholds.horizon_hysteresis_rad;
    const laser_controller_radians_t pitch =
        hw != NULL ? hw->beam_pitch_rad : 0.0f;

    if (snapshot->last_horizon_blocked) {
        return pitch > clear_threshold;
    }

    return pitch > trip_threshold;
}

static bool laser_controller_distance_blocked(
    const laser_controller_config_t *config,
    const laser_controller_safety_snapshot_t *snapshot)
{
    const laser_controller_board_inputs_t *hw =
        snapshot != NULL ? snapshot->hw : NULL;
    const laser_controller_distance_m_t distance =
        hw != NULL ? hw->tof_distance_m : 0.0f;
    const laser_controller_distance_m_t min_trip = config->thresholds.tof_min_range_m;
    const laser_controller_distance_m_t max_trip = config->thresholds.tof_max_range_m;
    const laser_controller_distance_m_t min_clear =
        config->thresholds.tof_min_range_m + config->thresholds.tof_hysteresis_m;
    const laser_controller_distance_m_t max_clear =
        config->thresholds.tof_max_range_m - config->thresholds.tof_hysteresis_m;

    if (snapshot->last_distance_blocked) {
        return distance < min_clear || distance > max_clear;
    }

    return distance < min_trip || distance > max_trip;
}

static bool laser_controller_lambda_drift_blocked(
    const laser_controller_config_t *config,
    const laser_controller_safety_snapshot_t *snapshot)
{
    const laser_controller_nm_t drift_nm =
        snapshot->actual_lambda_nm > snapshot->target_lambda_nm ?
            (snapshot->actual_lambda_nm - snapshot->target_lambda_nm) :
            (snapshot->target_lambda_nm - snapshot->actual_lambda_nm);
    const laser_controller_nm_t trip_threshold =
        config->thresholds.lambda_drift_limit_nm;
    const laser_controller_nm_t clear_threshold =
        config->thresholds.lambda_drift_limit_nm -
        config->thresholds.lambda_drift_hysteresis_nm;

    if (snapshot->last_lambda_drift_blocked) {
        return drift_nm > clear_threshold;
    }

    return drift_nm > trip_threshold;
}

static bool laser_controller_tec_temp_adc_blocked(
    const laser_controller_config_t *config,
    const laser_controller_safety_snapshot_t *snapshot)
{
    const laser_controller_board_inputs_t *hw =
        snapshot != NULL ? snapshot->hw : NULL;
    const laser_controller_volts_t trip_threshold =
        config->thresholds.tec_temp_adc_trip_v;
    const laser_controller_volts_t clear_threshold =
        config->thresholds.tec_temp_adc_trip_v -
        config->thresholds.tec_temp_adc_hysteresis_v;
    const laser_controller_volts_t adc_voltage_v =
        hw != NULL ? hw->tec_temp_adc_voltage_v : 0.0f;

    if (snapshot->last_tec_temp_adc_blocked) {
        return adc_voltage_v > clear_threshold;
    }

    return adc_voltage_v > trip_threshold;
}

static void laser_controller_set_fault(
    laser_controller_safety_decision_t *decision,
    laser_controller_fault_code_t code,
    laser_controller_fault_class_t fault_class,
    const char *reason)
{
    if (decision->fault_present) {
        return;
    }

    decision->fault_present = true;
    decision->fault_code = code;
    decision->fault_class = fault_class;
    decision->fault_reason = reason;
}

void laser_controller_safety_evaluate(
    const laser_controller_config_t *config,
    const laser_controller_safety_snapshot_t *snapshot,
    laser_controller_safety_decision_t *decision)
{
    const laser_controller_board_inputs_t *hw =
        snapshot != NULL ? snapshot->hw : NULL;
    const bool interlocks_disabled =
        snapshot != NULL && snapshot->interlocks_disabled;

    memset(decision, 0, sizeof(*decision));
    /*
     * Default to fast-shutdown (drive LOW). This is the datasheet-sanctioned
     * safe posture for the laser driver. STANDBY (Hi-Z) is reserved for
     * deliberate NIR-off-in-ready-state; OFF is what every error / unknown
     * path should assert.
     */
    decision->sbdn_state = LASER_CONTROLLER_SBDN_STATE_OFF;

    if (config == NULL || snapshot == NULL || hw == NULL) {
        laser_controller_set_fault(
            decision,
            LASER_CONTROLLER_FAULT_INVALID_CONFIG,
            LASER_CONTROLLER_FAULT_CLASS_SYSTEM_MAJOR,
            "missing config or snapshot");
        return;
    }

    decision->request_alignment = snapshot->host_request_alignment;
    decision->request_nir = snapshot->host_request_nir;

    /*
     * Button-driven requests are active ONLY in BINARY_TRIGGER runtime mode.
     * In MODULATED_HOST the host is the sole source of NIR requests; button
     * state is still read and published for telemetry, but does not drive
     * decisions. This matches the documented nir_blocked_reason taxonomy
     * and the user's 2026-04-15 directive that binary_trigger is the
     * button-control operator mode.
     *
     * `allow_missing_buttons` is honored even in BINARY_TRIGGER so service
     * mode can exercise the rest of the runtime even when the button board
     * is absent.
     */
    const bool buttons_effective =
        !snapshot->allow_missing_buttons &&
        snapshot->runtime_mode ==
            LASER_CONTROLLER_RUNTIME_MODE_BINARY_TRIGGER &&
        hw->button.board_reachable;

    if (buttons_effective) {
        const bool button_alignment_request =
            hw->button.stage1_pressed && !hw->button.stage2_pressed;
        bool button_nir_request =
            hw->button.stage1_pressed && hw->button.stage2_pressed;

        /*
         * Press-and-hold lockout: once an interlock fires while the
         * operator is holding the trigger, NIR stays blocked on this
         * press even if the interlock auto-clears. Only a full release
         * (both stages up, tracked by the control task) clears the
         * lockout. Alignment is NOT locked out — green laser is
         * ungated per the 2026-04-14 user directive.
         */
        if (snapshot->button_nir_lockout) {
            button_nir_request = false;
        }

        decision->request_alignment =
            decision->request_alignment || button_alignment_request;
        decision->request_nir =
            decision->request_nir || button_nir_request;
    }

    if (decision->request_nir) {
        decision->request_alignment = false;
    }

    if (!interlocks_disabled &&
        buttons_effective &&
        hw->button.stage2_pressed &&
        !hw->button.stage1_pressed) {
        laser_controller_set_fault(
            decision,
            LASER_CONTROLLER_FAULT_ILLEGAL_BUTTON_STATE,
            LASER_CONTROLLER_FAULT_CLASS_SAFETY_LATCHED,
            "stage2 without stage1");
    }

    if (!snapshot->config_valid) {
        laser_controller_set_fault(
            decision,
            LASER_CONTROLLER_FAULT_INVALID_CONFIG,
            LASER_CONTROLLER_FAULT_CLASS_SYSTEM_MAJOR,
            "configuration invalid");
    }

    if (!hw->comms_alive) {
        laser_controller_set_fault(
            decision,
            LASER_CONTROLLER_FAULT_COMMS_TIMEOUT,
            LASER_CONTROLLER_FAULT_CLASS_SYSTEM_MAJOR,
            "critical comms path unhealthy");
    }

    if (!hw->watchdog_ok) {
        laser_controller_set_fault(
            decision,
            LASER_CONTROLLER_FAULT_WATCHDOG_RESET,
            LASER_CONTROLLER_FAULT_CLASS_SYSTEM_MAJOR,
            "watchdog unhealthy");
    }

    if (hw->brownout_seen) {
        laser_controller_set_fault(
            decision,
            LASER_CONTROLLER_FAULT_BROWNOUT_RESET,
            LASER_CONTROLLER_FAULT_CLASS_SYSTEM_MAJOR,
            "brownout observed");
    }

    if (!interlocks_disabled &&
        !snapshot->allow_missing_imu &&
        !hw->imu_data_valid) {
        laser_controller_set_fault(
            decision,
            LASER_CONTROLLER_FAULT_IMU_INVALID,
            LASER_CONTROLLER_FAULT_CLASS_INTERLOCK_AUTO_CLEAR,
            "imu invalid");
    }

    if (!interlocks_disabled &&
        !snapshot->allow_missing_imu &&
        !hw->imu_data_fresh) {
        laser_controller_set_fault(
            decision,
            LASER_CONTROLLER_FAULT_IMU_STALE,
            LASER_CONTROLLER_FAULT_CLASS_INTERLOCK_AUTO_CLEAR,
            "imu stale");
    }

    if (!interlocks_disabled &&
        !snapshot->allow_missing_tof &&
        !hw->tof_data_valid) {
        laser_controller_set_fault(
            decision,
            LASER_CONTROLLER_FAULT_TOF_INVALID,
            LASER_CONTROLLER_FAULT_CLASS_INTERLOCK_AUTO_CLEAR,
            "tof invalid");
    }

    if (!interlocks_disabled &&
        !snapshot->allow_missing_tof &&
        !hw->tof_data_fresh) {
        laser_controller_set_fault(
            decision,
            LASER_CONTROLLER_FAULT_TOF_STALE,
            LASER_CONTROLLER_FAULT_CLASS_INTERLOCK_AUTO_CLEAR,
            "tof stale");
    }

    if (!interlocks_disabled) {
        decision->horizon_blocked =
            snapshot->allow_missing_imu ? false :
            laser_controller_horizon_blocked(config, snapshot);
        if (decision->horizon_blocked) {
            laser_controller_set_fault(
                decision,
                LASER_CONTROLLER_FAULT_HORIZON_CROSSED,
                LASER_CONTROLLER_FAULT_CLASS_INTERLOCK_AUTO_CLEAR,
                "beam pitch above horizon threshold");
        }

        decision->distance_blocked =
            snapshot->allow_missing_tof ? false :
            laser_controller_distance_blocked(config, snapshot);
        if (decision->distance_blocked) {
            laser_controller_set_fault(
                decision,
                LASER_CONTROLLER_FAULT_TOF_OUT_OF_RANGE,
                LASER_CONTROLLER_FAULT_CLASS_INTERLOCK_AUTO_CLEAR,
                "distance outside allowed window");
        }

        if (laser_controller_thermal_supervision_active(config, snapshot)) {
            decision->lambda_drift_blocked =
                laser_controller_lambda_drift_blocked(config, snapshot);
            decision->tec_temp_adc_blocked =
                laser_controller_tec_temp_adc_blocked(config, snapshot);
        }
    }

    if (!snapshot->service_mode_requested &&
        !snapshot->service_mode_active &&
        hw->ld_rail_pgood &&
        !snapshot->driver_operate_expected &&
        !snapshot->ready_idle_bias_allowed &&
        hw->measured_laser_current_a > config->thresholds.off_current_threshold_a &&
        !decision->request_nir) {
        laser_controller_set_fault(
            decision,
            LASER_CONTROLLER_FAULT_UNEXPECTED_CURRENT,
            LASER_CONTROLLER_FAULT_CLASS_SYSTEM_MAJOR,
            "current present while nir not requested");
    }

    /*
     * LD_OVERTEMP gate: only evaluate when both the LD rail has been up
     * and SBDN has been out of OFF for >= LASER_CONTROLLER_LD_TEMP_SETTLE_MS.
     * The 2 s window starts at the LATER of the two anchors — if the rail
     * comes up first and SBDN transitions later, the clock restarts from
     * the SBDN transition. This is enforced by tracking both timers
     * independently in the control task and requiring both to exceed
     * the threshold here.
     */
    const bool ld_temp_gate_open =
        (snapshot->ld_rail_pgood_for_ms >= LASER_CONTROLLER_LD_TEMP_SETTLE_MS) &&
        (snapshot->sbdn_not_off_for_ms  >= LASER_CONTROLLER_LD_TEMP_SETTLE_MS);

    if (hw->ld_rail_pgood &&
        ld_temp_gate_open &&
        hw->laser_driver_temp_c > config->thresholds.ld_overtemp_limit_c) {
        laser_controller_set_fault(
            decision,
            LASER_CONTROLLER_FAULT_LD_OVERTEMP,
            LASER_CONTROLLER_FAULT_CLASS_SYSTEM_MAJOR,
            "laser driver overtemperature");
        /*
         * Capture diagnostic values AT TRIP for the fault JSON. The
         * control task in app.c copies this into context->active_fault_diag
         * on the rising edge only — subsequent ticks will keep firing this
         * block while the condition persists, but the captured frame stays
         * frozen at trip time so operators see the value that tripped,
         * not a hair-trigger-later reading.
         *
         * Only populate when LD_OVERTEMP actually won the first-fault-wins
         * race inside `laser_controller_set_fault`. Otherwise a
         * higher-priority fault beat us (UNEXPECTED_CURRENT earlier in the
         * same evaluate pass) and the decision struct must not carry stale
         * LD_OVERTEMP diag for a fault that did not win. (B1 audit note.)
         */
        if (decision->fault_code == LASER_CONTROLLER_FAULT_LD_OVERTEMP) {
            decision->ld_overtemp_diag_valid   = true;
            decision->ld_overtemp_measured_c   = hw->laser_driver_temp_c;
            decision->ld_overtemp_voltage_v    = hw->laser_driver_temp_voltage_v;
            decision->ld_overtemp_limit_c      = config->thresholds.ld_overtemp_limit_c;
            decision->ld_overtemp_pgood_for_ms = snapshot->ld_rail_pgood_for_ms;
            decision->ld_overtemp_sbdn_for_ms  = snapshot->sbdn_not_off_for_ms;
        }
    }

    if (!interlocks_disabled &&
        hw->ld_rail_pgood &&
        snapshot->driver_operate_expected &&
        !hw->driver_loop_good) {
        laser_controller_set_fault(
            decision,
            LASER_CONTROLLER_FAULT_LD_LOOP_BAD,
            LASER_CONTROLLER_FAULT_CLASS_SAFETY_LATCHED,
            "laser driver loop not good");
    }

    /*
     * GREEN ALIGNMENT LASER — NO SOFTWARE INTERLOCK (user directive 2026-04-14)
     *
     * Green is inherently eye-safe at this product's output level; the user
     * has explicitly removed every software gate. `allow_alignment` is
     * unconditional. Hardware rail availability (TPS22918 load switch on
     * VDS_TEC_5V0) is the only remaining gate, and that is a hardware fact,
     * not a firmware decision.
     *
     * NIR remains fully gated (deployment ready-idle, FULL PD tier, rails
     * good, no fault). NIR gates are unchanged.
     */
    decision->allow_alignment = true;

    if (!snapshot->boot_complete ||
        (snapshot->fault_latched && !interlocks_disabled) ||
        snapshot->service_mode_requested ||
        snapshot->service_mode_active) {
        /*
         * NIR path stays safe-off in these states. Green follows request
         * unconditionally per the directive above.
         */
        decision->allow_nir = false;
        decision->alignment_output_enable = decision->request_alignment;
        decision->nir_output_enable = false;
        /*
         * Pre-boot / service / fault-latched — drive SBDN LOW for the
         * datasheet-fast 20 us shutdown. Not Hi-Z: standby still draws 8 mA
         * and is not the safe-off posture per the hardware-safety audit.
         */
        decision->sbdn_state = LASER_CONTROLLER_SBDN_STATE_OFF;
        return;
    }

    decision->allow_nir =
        snapshot->deployment_active &&
        snapshot->deployment_ready &&
        snapshot->deployment_ready_idle &&
        !snapshot->deployment_running &&
        snapshot->power_tier == LASER_CONTROLLER_POWER_TIER_FULL &&
        !decision->fault_present &&
        (interlocks_disabled ||
         (hw->ld_rail_pgood &&
          (!config->require_tec_for_nir ||
           (hw->tec_rail_pgood && hw->tec_temp_good))));

    decision->alignment_output_enable = decision->request_alignment;
    decision->nir_output_enable =
        decision->request_nir &&
        decision->allow_nir;
    /*
     * Three-state SBDN semantics (user directive 2026-04-14, verified against
     * ATLS6A214 datasheet):
     *   - NIR requested and allowed  → OPERATE, drive HIGH.
     *   - NIR not requested or not allowed → STANDBY (Hi-Z, 2.25 V via
     *     R27/R28 divider). Driver is idle and ready to re-enter operate in
     *     20 ms without going through full shutdown cycle.
     *   - The caller (app.c run_fast_cycle) overrides with OFF on any
     *     SYSTEM_MAJOR fault / interlock hold, so this branch never leaks a
     *     STANDBY through a fault state.
     */
    decision->sbdn_state = decision->nir_output_enable
        ? LASER_CONTROLLER_SBDN_STATE_ON
        : LASER_CONTROLLER_SBDN_STATE_STANDBY;
}
