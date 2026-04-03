#pragma once

#include <stdbool.h>

#include "esp_err.h"

#include "laser_controller_bench.h"
#include "laser_controller_board.h"
#include "laser_controller_config.h"
#include "laser_controller_deployment.h"
#include "laser_controller_faults.h"
#include "laser_controller_service.h"
#include "laser_controller_safety.h"
#include "laser_controller_state.h"

typedef struct {
    bool started;
    bool boot_complete;
    bool config_valid;
    bool fault_latched;
    laser_controller_time_ms_t uptime_ms;
    laser_controller_state_t state;
    laser_controller_power_tier_t power_tier;
    laser_controller_fault_code_t active_fault_code;
    laser_controller_fault_class_t active_fault_class;
    uint32_t active_fault_count;
    uint32_t trip_counter;
    laser_controller_time_ms_t last_fault_ms;
    laser_controller_board_inputs_t inputs;
    laser_controller_board_outputs_t outputs;
    laser_controller_safety_decision_t decision;
    laser_controller_config_t config;
    laser_controller_bench_status_t bench;
    laser_controller_service_status_t bringup;
    laser_controller_deployment_status_t deployment;
} laser_controller_runtime_status_t;

typedef struct {
    laser_controller_safety_thresholds_t thresholds;
    laser_controller_timeout_policy_t timeouts;
} laser_controller_runtime_safety_policy_t;

esp_err_t laser_controller_app_start(void);
bool laser_controller_app_copy_status(laser_controller_runtime_status_t *status);
esp_err_t laser_controller_app_clear_fault_latch(void);
esp_err_t laser_controller_app_set_runtime_safety_policy(
    const laser_controller_runtime_safety_policy_t *policy);
esp_err_t laser_controller_app_set_runtime_power_policy(
    const laser_controller_power_policy_t *policy);
