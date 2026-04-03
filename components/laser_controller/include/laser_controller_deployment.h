#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "laser_controller_faults.h"
#include "laser_controller_service.h"
#include "laser_controller_types.h"

#define LASER_CONTROLLER_DEPLOYMENT_FAILURE_REASON_LEN 96U

typedef enum {
    LASER_CONTROLLER_DEPLOYMENT_TARGET_MODE_TEMP = 0,
    LASER_CONTROLLER_DEPLOYMENT_TARGET_MODE_LAMBDA,
} laser_controller_deployment_target_mode_t;

typedef enum {
    LASER_CONTROLLER_DEPLOYMENT_STEP_NONE = 0,
    LASER_CONTROLLER_DEPLOYMENT_STEP_OWNERSHIP_RECLAIM,
    LASER_CONTROLLER_DEPLOYMENT_STEP_PD_INSPECT,
    LASER_CONTROLLER_DEPLOYMENT_STEP_POWER_CAP,
    LASER_CONTROLLER_DEPLOYMENT_STEP_OUTPUTS_OFF,
    LASER_CONTROLLER_DEPLOYMENT_STEP_STABILIZE_3V3,
    LASER_CONTROLLER_DEPLOYMENT_STEP_DAC_SAFE_ZERO,
    LASER_CONTROLLER_DEPLOYMENT_STEP_PERIPHERALS_VERIFY,
    LASER_CONTROLLER_DEPLOYMENT_STEP_RAIL_SEQUENCE,
    LASER_CONTROLLER_DEPLOYMENT_STEP_TEC_SETTLE,
    LASER_CONTROLLER_DEPLOYMENT_STEP_READY_POSTURE,
    LASER_CONTROLLER_DEPLOYMENT_STEP_COUNT,
} laser_controller_deployment_step_t;

typedef enum {
    LASER_CONTROLLER_DEPLOYMENT_STEP_STATUS_INACTIVE = 0,
    LASER_CONTROLLER_DEPLOYMENT_STEP_STATUS_PENDING,
    LASER_CONTROLLER_DEPLOYMENT_STEP_STATUS_IN_PROGRESS,
    LASER_CONTROLLER_DEPLOYMENT_STEP_STATUS_PASSED,
    LASER_CONTROLLER_DEPLOYMENT_STEP_STATUS_FAILED,
} laser_controller_deployment_step_status_t;

typedef struct {
    laser_controller_deployment_target_mode_t target_mode;
    laser_controller_celsius_t target_temp_c;
    laser_controller_nm_t target_lambda_nm;
} laser_controller_deployment_target_t;

typedef struct {
    bool active;
    bool running;
    bool ready;
    bool failed;
    laser_controller_deployment_step_t current_step;
    laser_controller_deployment_step_t last_completed_step;
    laser_controller_fault_code_t failure_code;
    char failure_reason[LASER_CONTROLLER_DEPLOYMENT_FAILURE_REASON_LEN];
    laser_controller_deployment_target_t target;
    laser_controller_amps_t max_laser_current_a;
    float max_optical_power_w;
    laser_controller_deployment_step_status_t
        step_status[LASER_CONTROLLER_DEPLOYMENT_STEP_COUNT];
} laser_controller_deployment_status_t;

const char *laser_controller_deployment_target_mode_name(
    laser_controller_deployment_target_mode_t target_mode);
const char *laser_controller_deployment_step_name(
    laser_controller_deployment_step_t step);
const char *laser_controller_deployment_step_label(
    laser_controller_deployment_step_t step);
const char *laser_controller_deployment_step_status_name(
    laser_controller_deployment_step_status_t status);
bool laser_controller_deployment_module_required(laser_controller_module_t module);

bool laser_controller_deployment_mode_active(void);
bool laser_controller_deployment_mode_running(void);
bool laser_controller_deployment_mode_ready(void);
bool laser_controller_deployment_copy_status(
    laser_controller_deployment_status_t *status);
esp_err_t laser_controller_app_enter_deployment_mode(void);
esp_err_t laser_controller_app_exit_deployment_mode(void);
esp_err_t laser_controller_app_run_deployment_sequence(void);
esp_err_t laser_controller_app_set_deployment_target(
    const laser_controller_deployment_target_t *target);
