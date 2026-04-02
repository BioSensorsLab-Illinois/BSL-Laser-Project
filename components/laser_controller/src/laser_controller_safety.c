#include "laser_controller_safety.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static bool laser_controller_power_allows_alignment(laser_controller_power_tier_t power_tier)
{
    return power_tier == LASER_CONTROLLER_POWER_TIER_REDUCED ||
           power_tier == LASER_CONTROLLER_POWER_TIER_FULL;
}

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

    memset(decision, 0, sizeof(*decision));
    decision->driver_standby_asserted = true;

    if (config == NULL || snapshot == NULL || hw == NULL) {
        laser_controller_set_fault(
            decision,
            LASER_CONTROLLER_FAULT_INVALID_CONFIG,
            LASER_CONTROLLER_FAULT_CLASS_SYSTEM_MAJOR,
            "missing config or snapshot");
        return;
    }

    if (!snapshot->allow_missing_buttons) {
        decision->request_alignment =
            hw->button.stage1_pressed && !hw->button.stage2_pressed;
        decision->request_nir =
            hw->button.stage1_pressed && hw->button.stage2_pressed;
    }

    if (!snapshot->allow_missing_buttons &&
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

    if (!snapshot->allow_missing_imu && !hw->imu_data_valid) {
        laser_controller_set_fault(
            decision,
            LASER_CONTROLLER_FAULT_IMU_INVALID,
            LASER_CONTROLLER_FAULT_CLASS_INTERLOCK_AUTO_CLEAR,
            "imu invalid");
    }

    if (!snapshot->allow_missing_imu && !hw->imu_data_fresh) {
        laser_controller_set_fault(
            decision,
            LASER_CONTROLLER_FAULT_IMU_STALE,
            LASER_CONTROLLER_FAULT_CLASS_INTERLOCK_AUTO_CLEAR,
            "imu stale");
    }

    if (!snapshot->allow_missing_tof && !hw->tof_data_valid) {
        laser_controller_set_fault(
            decision,
            LASER_CONTROLLER_FAULT_TOF_INVALID,
            LASER_CONTROLLER_FAULT_CLASS_INTERLOCK_AUTO_CLEAR,
            "tof invalid");
    }

    if (!snapshot->allow_missing_tof && !hw->tof_data_fresh) {
        laser_controller_set_fault(
            decision,
            LASER_CONTROLLER_FAULT_TOF_STALE,
            LASER_CONTROLLER_FAULT_CLASS_INTERLOCK_AUTO_CLEAR,
            "tof stale");
    }

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

    if (hw->measured_laser_current_a > config->thresholds.off_current_threshold_a &&
        !decision->request_nir) {
        laser_controller_set_fault(
            decision,
            LASER_CONTROLLER_FAULT_UNEXPECTED_CURRENT,
            LASER_CONTROLLER_FAULT_CLASS_SYSTEM_MAJOR,
            "current present while nir not requested");
    }

    if (hw->laser_driver_temp_c > config->thresholds.ld_overtemp_limit_c) {
        laser_controller_set_fault(
            decision,
            LASER_CONTROLLER_FAULT_LD_OVERTEMP,
            LASER_CONTROLLER_FAULT_CLASS_SYSTEM_MAJOR,
            "laser driver overtemperature");
    }

    if (hw->ld_rail_pgood && !hw->driver_loop_good) {
        laser_controller_set_fault(
            decision,
            LASER_CONTROLLER_FAULT_LD_LOOP_BAD,
            LASER_CONTROLLER_FAULT_CLASS_SAFETY_LATCHED,
            "laser driver loop not good");
    }

    if (!snapshot->boot_complete ||
        snapshot->fault_latched ||
        snapshot->service_mode_requested ||
        snapshot->service_mode_active) {
        decision->allow_alignment = false;
        decision->allow_nir = false;
        decision->alignment_output_enable = false;
        decision->nir_output_enable = false;
        decision->driver_standby_asserted = true;
        return;
    }

    decision->allow_alignment =
        laser_controller_power_allows_alignment(snapshot->power_tier) &&
        !decision->fault_present;

    decision->allow_nir =
        snapshot->power_tier == LASER_CONTROLLER_POWER_TIER_FULL &&
        !decision->fault_present &&
        hw->ld_rail_pgood &&
        (!config->require_tec_for_nir ||
         (hw->tec_rail_pgood && hw->tec_temp_good)) &&
        hw->driver_loop_good;

    if (!config->alignment_obeys_interlocks &&
        !decision->fault_present &&
        laser_controller_power_allows_alignment(snapshot->power_tier)) {
        decision->allow_alignment = true;
    }

    decision->alignment_output_enable =
        decision->request_alignment &&
        decision->allow_alignment;
    decision->nir_output_enable =
        decision->request_nir &&
        decision->allow_nir;
    decision->driver_standby_asserted = !decision->nir_output_enable;
}
