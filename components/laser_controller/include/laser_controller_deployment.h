#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "laser_controller_faults.h"
#include "laser_controller_service.h"
#include "laser_controller_types.h"

#define LASER_CONTROLLER_DEPLOYMENT_FAILURE_REASON_LEN 96U
#define LASER_CONTROLLER_DEPLOYMENT_SECONDARY_EFFECT_COUNT 4U

typedef enum {
    LASER_CONTROLLER_DEPLOYMENT_TARGET_MODE_TEMP = 0,
    LASER_CONTROLLER_DEPLOYMENT_TARGET_MODE_LAMBDA,
} laser_controller_deployment_target_mode_t;

typedef enum {
    LASER_CONTROLLER_DEPLOYMENT_PHASE_INACTIVE = 0,
    LASER_CONTROLLER_DEPLOYMENT_PHASE_ENTRY,
    LASER_CONTROLLER_DEPLOYMENT_PHASE_CHECKLIST,
    LASER_CONTROLLER_DEPLOYMENT_PHASE_READY_IDLE,
    LASER_CONTROLLER_DEPLOYMENT_PHASE_FAILED,
} laser_controller_deployment_phase_t;

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
    /*
     * Added 2026-04-15: SBDN -> LP_GOOD loop-lock verification.
     *
     * Drives the ATLS6A214 SBDN pin HIGH (OPERATE) with select_driver_low_current
     * asserted (PCN LOW → low-current mode) so the driver can lock its loop
     * without any runtime current being requested. Waits up to 1 s for
     * LD_LPGD (GPIO14) to go HIGH. If the timeout expires, the step fails
     * with primary fault LD_LP_GOOD_TIMEOUT and the deployment aborts.
     *
     * This is a safety-critical check because the rails can be PGOOD yet
     * the driver loop can still be unable to lock (e.g. missing interconnect,
     * damaged laser diode, incorrect TMO). Without this test, READY_POSTURE
     * could declare ready-idle on a driver that cannot actually operate.
     */
    LASER_CONTROLLER_DEPLOYMENT_STEP_LP_GOOD_CHECK,
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

/*
 * `laser_controller_deployment_ready_truth_t` — live snapshot of the
 * ready-posture observables, refreshed every tick while
 * `deployment.active` is true. NOT frozen at READY_POSTURE pass — the
 * fields are published for host diagnostics and the operator expects
 * them to reflect the current hardware state, not a stale snapshot.
 *
 * The READY_POSTURE step itself reads `sbdn_high`, `pcn_low`, and
 * `driver_loop_good` to qualify its advance, but those fields stay
 * correct across ticks because they mirror the committed
 * `last_outputs` / `last_inputs`, not a one-shot capture.
 */
typedef struct {
    bool tec_rail_pgood_raw;
    bool tec_rail_pgood_filtered;
    bool tec_temp_good;
    bool tec_analog_plausible;
    bool ld_rail_pgood_raw;
    bool ld_rail_pgood_filtered;
    bool driver_loop_good;
    bool sbdn_high;
    bool pcn_low;
    laser_controller_amps_t idle_bias_current_a;
} laser_controller_deployment_ready_truth_t;

typedef struct {
    laser_controller_fault_code_t code;
    char reason[LASER_CONTROLLER_DEPLOYMENT_FAILURE_REASON_LEN];
    laser_controller_time_ms_t at_ms;
} laser_controller_deployment_secondary_effect_t;

typedef struct {
    bool active;
    bool running;
    bool ready;
    bool ready_idle;
    bool ready_qualified;
    bool ready_invalidated;
    bool failed;
    uint32_t sequence_id;
    laser_controller_deployment_phase_t phase;
    laser_controller_deployment_step_t current_step;
    laser_controller_deployment_step_t last_completed_step;
    laser_controller_fault_code_t failure_code;
    char failure_reason[LASER_CONTROLLER_DEPLOYMENT_FAILURE_REASON_LEN];
    laser_controller_fault_code_t primary_failure_code;
    char primary_failure_reason[LASER_CONTROLLER_DEPLOYMENT_FAILURE_REASON_LEN];
    uint32_t secondary_effect_count;
    laser_controller_deployment_secondary_effect_t
        secondary_effects[LASER_CONTROLLER_DEPLOYMENT_SECONDARY_EFFECT_COUNT];
    laser_controller_deployment_ready_truth_t ready_truth;
    laser_controller_deployment_target_t target;
    laser_controller_amps_t max_laser_current_a;
    float max_optical_power_w;
    laser_controller_time_ms_t
        step_started_at_ms[LASER_CONTROLLER_DEPLOYMENT_STEP_COUNT];
    laser_controller_time_ms_t
        step_completed_at_ms[LASER_CONTROLLER_DEPLOYMENT_STEP_COUNT];
    laser_controller_deployment_step_status_t
        step_status[LASER_CONTROLLER_DEPLOYMENT_STEP_COUNT];
} laser_controller_deployment_status_t;

const char *laser_controller_deployment_target_mode_name(
    laser_controller_deployment_target_mode_t target_mode);
const char *laser_controller_deployment_phase_name(
    laser_controller_deployment_phase_t phase);
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
