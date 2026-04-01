#pragma once

#include <stdbool.h>

#include "laser_controller_board.h"
#include "laser_controller_config.h"
#include "laser_controller_faults.h"
#include "laser_controller_types.h"

typedef struct {
    laser_controller_time_ms_t now_ms;
    bool boot_complete;
    bool config_valid;
    bool fault_latched;
    bool service_mode_requested;
    bool service_mode_active;
    bool allow_missing_imu;
    bool allow_missing_tof;
    bool allow_missing_buttons;
    bool last_horizon_blocked;
    bool last_distance_blocked;
    bool last_lambda_drift_blocked;
    bool last_tec_temp_adc_blocked;
    laser_controller_power_tier_t power_tier;
    laser_controller_nm_t target_lambda_nm;
    laser_controller_nm_t actual_lambda_nm;
    laser_controller_board_inputs_t hw;
} laser_controller_safety_snapshot_t;

typedef struct {
    bool request_alignment;
    bool request_nir;
    bool allow_alignment;
    bool allow_nir;
    bool alignment_output_enable;
    bool nir_output_enable;
    bool horizon_blocked;
    bool distance_blocked;
    bool lambda_drift_blocked;
    bool tec_temp_adc_blocked;
    bool driver_standby_asserted;
    bool fault_present;
    laser_controller_fault_code_t fault_code;
    laser_controller_fault_class_t fault_class;
    const char *fault_reason;
} laser_controller_safety_decision_t;

void laser_controller_safety_evaluate(
    const laser_controller_config_t *config,
    const laser_controller_safety_snapshot_t *snapshot,
    laser_controller_safety_decision_t *decision);
