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
    laser_controller_fault_code_t latched_fault_code;
    laser_controller_fault_class_t latched_fault_class;
    /*
     * Last fault detail strings (populated by record_fault). Surfaced
     * to the host so the operator sees WHY a fault tripped — previously
     * `unexpected_state (safety_latched)` had no actionable detail.
     * (2026-04-16)
     */
    char active_fault_reason[80];
    char latched_fault_reason[80];
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
    /*
     * Button-board runtime telemetry (control-task computed). Mirrors
     * fields that are otherwise context-private. Published so the host
     * can render the trigger phase / lockout state / RGB test status
     * without recomputing the firmware policy client-side.
     */
    struct {
        bool nir_lockout;
        uint32_t led_brightness_pct;
        bool led_owned;
        bool rgb_test_active;
        /*
         * Why button-driven NIR is currently blocked. Populated by
         * `apply_button_board_policy` after every tick's safety
         * decision. Set to "none" when stage1+stage2 would fire NIR
         * (or already are firing). Surfaced in the GUI Operate-page
         * trigger card so the operator sees WHY the laser doesn't
         * fire, instead of guessing. (2026-04-16 audit/refactor.)
         */
        char nir_block_reason[40];
    } button_runtime;
    /*
     * Diagnostic payload captured on the RISING EDGE of a fault. Currently
     * populated only for LD_OVERTEMP (added 2026-04-15 late) — the shape is
     * general so future faults can reuse it. `valid == false` when no diag
     * has been captured since the last `clear_fault_latch`.
     *
     * Frozen at trip time: once `valid == true`, later ticks do NOT update
     * the measured values even if the underlying condition drifts. This
     * preserves the exact reading that caused the trip for operator
     * diagnosis. Cleared by `laser_controller_app_clear_fault_latch`.
     *
     * Control-task-owned write; comms reads via the status copy path.
     */
    struct {
        bool valid;
        laser_controller_fault_code_t code;
        float measured_c;
        float measured_voltage_v;
        float limit_c;
        uint32_t ld_pgood_for_ms;
        uint32_t sbdn_not_off_for_ms;
        char expr[64];
    } active_fault_diag;
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

/*
 * Integrate-test RGB override for the button-board status LED. Engages a
 * watchdog-bounded direct color/blink override that survives until either
 * the watchdog expires or `laser_controller_app_clear_rgb_test` is called.
 *
 * Gated on (status.deployment.active == false) && (!status.fault_latched)
 * AND service-mode-active (the comms.c handler enforces this before
 * calling). The control task is the sole writer of the underlying state;
 * this entry point only stages a request that the next 5 ms tick picks up.
 *
 * `hold_ms` of 0 means "use the default 5 s watchdog".
 */
esp_err_t laser_controller_app_set_rgb_test(
    uint8_t r, uint8_t g, uint8_t b, bool blink, uint32_t hold_ms);
esp_err_t laser_controller_app_clear_rgb_test(void);

/*
 * Set the post-deployment front-LED brightness percent (0..100). Shared
 * source-of-truth for both side-button stepping and the GUI Operate-page
 * slider — calling this keeps the displayed value and the actual LED
 * brightness in sync. Clamped to [0, 100] AND to the configured
 * max_tof_led_duty_cycle_pct cap. (2026-04-17)
 */
esp_err_t laser_controller_app_set_button_led_brightness(uint32_t pct);

/*
 * Headless auto-deploy support (2026-04-16 user feature).
 *
 * `note_host_activity()` is called by the comms layer at the top of
 * `laser_controller_comms_receive_line` for every inbound command,
 * regardless of source (USB serial, WiFi WS, mock). The control task
 * uses the timestamp to decide whether to fire the 5-second-after-boot
 * auto-enter-deployment flow. Once any host command arrives, the
 * auto-deploy is suppressed for the entire boot session.
 */
void laser_controller_app_note_host_activity(void);
