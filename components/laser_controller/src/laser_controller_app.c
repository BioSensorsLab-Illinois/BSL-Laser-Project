#include "laser_controller_app.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "laser_controller_bench.h"
#include "laser_controller_board.h"
#include "laser_controller_comms.h"
#include "laser_controller_config.h"
#include "laser_controller_faults.h"
#include "laser_controller_logger.h"
#include "laser_controller_service.h"
#include "laser_controller_safety.h"
#include "laser_controller_state.h"
#include "laser_controller_usb_debug_mock.h"
#include "laser_controller_wireless.h"

#define LASER_CONTROLLER_CONTROL_PERIOD_MS 5U
#define LASER_CONTROLLER_SLOW_DIVIDER 10U
#define LASER_CONTROLLER_CONTROL_STACK_WORDS 6144U
#define LASER_CONTROLLER_CONTROL_PRIORITY 20U
#define LASER_CONTROLLER_MS_TO_TICKS_CEIL(ms_) \
    ((TickType_t)((((ms_) + portTICK_PERIOD_MS - 1U) / portTICK_PERIOD_MS) > 0U ? \
        (((ms_) + portTICK_PERIOD_MS - 1U) / portTICK_PERIOD_MS) : 1U))
#define LASER_CONTROLLER_CONTROL_PERIOD_TICKS \
    LASER_CONTROLLER_MS_TO_TICKS_CEIL(LASER_CONTROLLER_CONTROL_PERIOD_MS)
#define LASER_CONTROLLER_BENCH_MIN_TEMP_C    5.0f
#define LASER_CONTROLLER_BENCH_MAX_TEMP_C    65.0f
#define LASER_CONTROLLER_BENCH_MIN_LAMBDA_NM 771.2f
#define LASER_CONTROLLER_BENCH_MAX_LAMBDA_NM 790.0f
#define LASER_CONTROLLER_PD_RECONCILE_BOOT_DELAY_MS 1500U
#define LASER_CONTROLLER_PD_RECONCILE_SUCCESS_MS    3000U
#define LASER_CONTROLLER_PD_RECONCILE_RETRY_MS      10000U
#define LASER_CONTROLLER_PD_COMPARE_VOLTAGE_EPS_V   0.06f
#define LASER_CONTROLLER_PD_COMPARE_CURRENT_EPS_A   0.02f
/*
 * 2026-04-16 user directive: "for all items requires wait and check, keep
 * checking and if condition met, skip the wait. Also if there is any
 * intentional delay, keep them to 200 ms".
 *
 * STABILIZE_MS and STEP_DELAY_MS are pure time dwells — no external
 * condition can shorten them — so they are capped to 200 ms. The other
 * *_WAIT_MS values are step TIMEOUTS (max wait before the step fails).
 * Every step already polls its completion condition every 5 ms tick, so
 * no explicit "skip the wait" change is needed — the step advances as
 * soon as the condition is met, up to the per-step timeout.
 */
#define LASER_CONTROLLER_DEPLOYMENT_STABILIZE_MS    200U
#define LASER_CONTROLLER_DEPLOYMENT_PD_WAIT_MS      1200U
#define LASER_CONTROLLER_DEPLOYMENT_OWNERSHIP_WAIT_MS 1200U
#define LASER_CONTROLLER_DEPLOYMENT_OUTPUTS_WAIT_MS  800U
#define LASER_CONTROLLER_DEPLOYMENT_DAC_WAIT_MS      800U
#define LASER_CONTROLLER_DEPLOYMENT_PERIPHERAL_WAIT_MS 2500U
#define LASER_CONTROLLER_DEPLOYMENT_READY_WAIT_MS    1200U
#define LASER_CONTROLLER_DEPLOYMENT_STEP_DELAY_MS    200U
#define LASER_CONTROLLER_DEPLOYMENT_RAIL_SEQUENCE_MIN_WAIT_MS 4000U
#define LASER_CONTROLLER_DEPLOYMENT_READY_MIN_WAIT_MS 3000U
#define LASER_CONTROLLER_DEPLOYMENT_TEC_RESERVE_W    15.0f
#define LASER_CONTROLLER_DEPLOYMENT_PERIPHERAL_RESERVE_W 5.0f
#define LASER_CONTROLLER_DEPLOYMENT_MIN_LD_SOURCE_V  9.0f
#define LASER_CONTROLLER_DEPLOYMENT_FULL_POWER_W     40.0f
#define LASER_CONTROLLER_DEPLOYMENT_DEFAULT_TEMP_C   25.0f
#define LASER_CONTROLLER_DEPLOYMENT_DAC_CODE_MAX     65535U
#define LASER_CONTROLLER_DEPLOYMENT_DAC_FULL_SCALE_V 2.5f

typedef struct {
    bool state;
    bool pending_valid;
    bool pending_state;
    laser_controller_time_ms_t pending_since_ms;
} laser_controller_hold_filter_t;

typedef struct {
    bool waiting;
    laser_controller_time_ms_t pending_since_ms;
} laser_controller_rail_watch_t;

typedef enum {
    LASER_CONTROLLER_DEPLOYMENT_RAIL_PHASE_TEC = 0,
    LASER_CONTROLLER_DEPLOYMENT_RAIL_PHASE_LD,
} laser_controller_deployment_rail_phase_t;

typedef struct {
    bool started;
    bool boot_complete;
    bool config_valid;
    bool fault_latched;
    bool summary_snapshot_valid;
    bool last_horizon_blocked;
    bool last_distance_blocked;
    laser_controller_hold_filter_t lambda_drift_filter;
    laser_controller_hold_filter_t tec_temp_adc_filter;
    laser_controller_rail_watch_t ld_rail_watch;
    laser_controller_rail_watch_t tec_rail_watch;
    uint32_t active_fault_count;
    uint32_t trip_counter;
    laser_controller_time_ms_t last_fault_ms;
    laser_controller_fault_code_t active_fault_code;
    laser_controller_fault_class_t active_fault_class;
    laser_controller_fault_code_t latched_fault_code;
    laser_controller_fault_class_t latched_fault_class;
    /*
     * Last fault detail strings, populated by record_fault. Surfaced to
     * the host so the operator sees WHY a fault tripped — previously
     * `unexpected_state (safety_latched)` had no actionable detail.
     * Bounded buffers; safe to publish as JSON. (2026-04-16)
     */
    char active_fault_reason[80];
    char latched_fault_reason[80];
    laser_controller_power_tier_t power_tier;
    laser_controller_state_t last_summary_state;
    laser_controller_power_tier_t last_summary_power_tier;
    laser_controller_fault_code_t last_summary_fault_code;
    bool last_summary_alignment_enabled;
    bool last_summary_nir_enabled;
    laser_controller_time_ms_t next_pd_reconcile_ms;
    laser_controller_config_t config;
    laser_controller_state_machine_t state_machine;
    laser_controller_deployment_status_t deployment;
    laser_controller_time_ms_t deployment_step_started_ms;
    laser_controller_time_ms_t deployment_phase_started_ms;
    uint32_t deployment_substep;
    bool deployment_step_action_performed;
    laser_controller_time_ms_t deployment_tec_loss_since_ms;
    laser_controller_time_ms_t deployment_ld_loss_since_ms;
    /*
     * Rising-edge anchors for the LD-overtemp temp-valid gate.
     *
     * `ld_rail_pgood_since_ms` = timestamp at which the LD rail last rose
     *    from PGOOD-low to PGOOD-high. 0 when the rail is currently off.
     * `sbdn_not_off_since_ms` = timestamp at which `last_outputs.sbdn_state`
     *    last transitioned from OFF to STANDBY or ON. 0 when SBDN is
     *    currently OFF.
     *
     * The LD driver thermistor ADC read is not trusted until BOTH anchors
     * are at least LASER_CONTROLLER_LD_TEMP_SETTLE_MS (2 s) old — garbage
     * first-sample reads during rail rise or tri-state SBDN transitions
     * used to latch spurious SYSTEM_MAJOR LD_OVERTEMP faults. User
     * directive 2026-04-15 (late). Control-task-owned write; safety
     * snapshot carries the derived durations into safety_evaluate.
     */
    laser_controller_time_ms_t ld_rail_pgood_since_ms;
    laser_controller_time_ms_t sbdn_not_off_since_ms;
    /*
     * Diagnostic payload captured AT TRIP for LD_OVERTEMP. Populated on
     * the rising edge of the fault, frozen while the fault stays latched.
     * Cleared by `laser_controller_app_clear_fault_latch`. See
     * `laser_controller_runtime_status_t.active_fault_diag` docstring in
     * laser_controller_app.h for the full contract.
     *
     * THREADING: `active_fault_diag.*` is owned by the control task. Writes
     * happen in two places, both from the control task:
     *   - `run_fast_cycle` rising-edge capture after safety_evaluate.
     *   - `run_fast_cycle` drain of `clear_fault_diag_request` at the top
     *     of the tick.
     * The comms task NEVER writes `active_fault_diag.*` directly. Instead
     * it sets `clear_fault_diag_request = true` under `s_context_lock` and
     * the control task picks the flag up on the next tick (<=5 ms). This
     * eliminates the data race that would otherwise let comms zero a
     * just-captured at-trip frame.
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
    /*
     * Cross-task request flag: set by `laser_controller_app_clear_fault_latch`
     * (comms task) under `s_context_lock`, drained by `run_fast_cycle`
     * (control task) under `s_context_lock`. When the control task sees
     * the flag, it zeros `active_fault_diag` and clears the flag. This is
     * the canonical cross-task hand-off pattern per
     * `.agent/skills/firmware-change/SKILL.md`.
     */
    bool clear_fault_diag_request;
    /*
     * Cross-task request flag: set by `laser_controller_app_clear_fault_latch`
     * AND `laser_controller_app_exit_deployment_mode` (both comms task) under
     * `s_context_lock`, drained by `run_fast_cycle` (control task) under
     * `s_context_lock`. When the control task sees the flag, it zeros the
     * full set of control-task-owned fault fields:
     *   - `fault_latched`
     *   - `active_fault_code`, `active_fault_class`, `active_fault_reason`
     *   - `latched_fault_code`, `latched_fault_class`, `latched_fault_reason`
     *   - `last_fault_ms`
     * and clears the flag. Canonical cross-task hand-off so that comms
     * never races `record_fault` (which writes these fields unlocked from
     * the control task). Added 2026-04-17 to close the threading finding
     * that paralleled the GPIO6 LED injury.
     */
    bool clear_fault_latch_request;
    bool deployment_force_ld_safe;
    bool deployment_tec_contradiction_reported;
    laser_controller_board_inputs_t last_inputs;
    laser_controller_board_outputs_t last_outputs;
    laser_controller_safety_decision_t last_decision;
    /*
     * Button-board press-and-hold lockout state.
     *
     * `button_nir_lockout` is set by the control task when an interlock
     * fires during a binary-trigger NIR press (stage1+stage2 held).
     * Once set, NIR is forced off until BOTH stages release, then on the
     * next press the lockout is cleared and gates re-evaluate normally.
     * Cleared explicitly by `!stage1 && !stage2` edge in the control task.
     *
     * Written ONLY by the control task — same threading model as
     * `fault_latched`. Do not write from comms or main.
     */
    bool button_nir_lockout;
    /*
     * Front LED-board brightness (GPIO6 illumination) that the
     * binary-trigger button policy wants applied this tick. Stepped by
     * side1 (+5%) and side2 (-5%) edges. Reset to 20% on every stage1
     * rising edge per user directive 2026-04-15. Range 0..100.
     */
    uint32_t button_led_brightness_pct;
    /*
     * TRUE when the binary-trigger policy owns the GPIO6 front LED this
     * tick. When TRUE, the runtime illumination call uses
     * `button_led_brightness_pct` instead of the bench-status host slider
     * value. Set to TRUE when binary_trigger + deployment ready_idle +
     * stage1_pressed.
     */
    bool button_led_active;
    /*
     * One-shot flag: TRUE once `button_led_brightness_pct` has been
     * initialized for the current deployment-ready session. Cleared
     * when deployment is exited (NOT when ready_idle drops) so the
     * brightness default only resets on a fresh deployment cycle.
     * (2026-04-17)
     */
    bool button_led_initialized;
    /*
     * Sticky LED-on flag: TRUE once deployment first reaches
     * `ready_idle`. Stays TRUE through fault latches / interlock
     * trips so the front LED keeps illuminating the work area.
     * Cleared ONLY on deployment exit (or boot reset).
     * Per user directive 2026-04-17.
     */
    bool deployment_led_armed;
    /*
     * Diagnostic string explaining why the BUTTON path is currently
     * not firing NIR. Recomputed every tick at the end of
     * `apply_button_board_policy` and mirrored into
     * `s_runtime_status.button_runtime.nir_block_reason` for the
     * GUI's trigger card. (2026-04-16 audit/refactor.)
     */
    char nir_button_block_reason[40];
    /*
     * Headless auto-deploy support (2026-04-16 user feature):
     *   - `last_host_activity_ms` is updated by `note_host_activity()`
     *     on every inbound comms command. Stays 0 while no host has
     *     ever connected.
     *   - `auto_deploy_state` is a one-shot state machine driven from
     *     `run_fast_cycle`: IDLE -> ENTER_PENDING -> RUN_PENDING -> DONE.
     *     Triggers when uptime > 5000 ms AND last_host_activity_ms == 0
     *     AND no fault is latched AND deployment is not already active.
     */
    laser_controller_time_ms_t last_host_activity_ms;
    enum {
        LASER_CONTROLLER_AUTO_DEPLOY_IDLE = 0,
        LASER_CONTROLLER_AUTO_DEPLOY_ENTER_PENDING,
        LASER_CONTROLLER_AUTO_DEPLOY_RUN_PENDING,
        LASER_CONTROLLER_AUTO_DEPLOY_DONE,
    } auto_deploy_state;
    /*
     * Headless fault recovery via the two side (peripheral) buttons
     * (2026-04-16 user feature). When the device is latched in a
     * SYSTEM_MAJOR / SAFETY_LATCHED fault, the operator can recover
     * without a GUI by holding BOTH side1 and side2 buttons (the
     * peripheral LED-brightness buttons) for >= 2 s. At the 2 s mark
     * we clear the fault latch, enter deployment mode if not already,
     * and kick off the deployment sequence. The main stage1/stage2
     * trigger is deliberately NOT used — it is the NIR-fire gesture
     * and must stay single-purpose.
     *
     *   `both_side_held_since_ms` captures the uptime at which both
     *     side buttons entered the pressed state. Reset to 0 whenever
     *     either side button releases, or when the recovery fires.
     *
     *   `button_recovery_armed` latches true the instant the 2 s hold
     *     triggers recovery. Prevents a single long hold from
     *     repeatedly firing recovery while the buttons stay pressed.
     *     Cleared once both side buttons release.
     *
     * Gating: recovery only fires when `fault_latched` is true. In
     * normal ready_idle operation, side buttons drive LED brightness
     * (+/-5%) and never affect deployment — so a 2 s hold is
     * harmless if no fault is latched.
     */
    laser_controller_time_ms_t both_side_held_since_ms;
    bool button_recovery_armed;
    /*
     * One-shot request flag raised by `apply_button_board_policy` when
     * the 2 s side1+side2 hold triggers recovery. `run_fast_cycle`
     * consumes the flag at the END of the tick — safely AFTER outputs
     * have been applied — and performs: clear_fault_latch(),
     * enter_deployment_mode(), run_deployment_sequence(). Done after
     * outputs are applied so the tick that detects the hold still
     * publishes the "still-faulted" telemetry; the NEXT tick starts
     * fresh with the fault cleared and deployment re-arming.
     */
    bool button_recovery_requested;
    /*
     * Headless button-driven START-OF-DEPLOYMENT (2026-04-17 user
     * feature). Same side1+side2 hold gesture as the 2 s fault-recovery
     * path, but fires the NORMAL deployment sequence at 1 s when:
     *   - no host is connected (no WS client, no recent USB activity)
     *   - no fault is latched
     *   - deployment is NOT already active
     *   - service mode is NOT active
     * At the 1 s mark `button_deploy_requested` is raised and consumed
     * by `run_fast_cycle` end-of-tick handler: `enter_deployment_mode`
     * then `run_deployment_sequence`. One-shot per hold — the gesture
     * must be fully released and re-asserted to trigger again.
     *
     * `button_deploy_armed` latches true the instant the 1 s hold
     * triggers, mirroring `button_recovery_armed`.
     */
    bool button_deploy_armed;
    bool button_deploy_requested;
    /*
     * Auto-deploy-on-boot (2026-04-20 user directive). When
     * `config.service_flags & LASER_CONTROLLER_SERVICE_FLAG_AUTO_DEPLOY_ON_BOOT`
     * is set, `auto_deploy_pending` latches true at start(). A short
     * settle window later (first tick with `boot_complete && config_valid &&
     * !fault_latched && !deployment.active && !service_mode`) raises
     * `auto_deploy_requested` as a one-shot; the existing end-of-tick
     * handler consumes it and calls `enter_deployment_mode` +
     * `run_deployment_sequence`. `auto_deploy_armed` latches the
     * transition so the path only fires once per power-cycle.
     */
    bool auto_deploy_pending;
    bool auto_deploy_armed;
    bool auto_deploy_requested;
    /*
     * Last whole-second boundary logged during a recovery hold. Used
     * to de-duplicate the "hold X s elapsed" diagnostic so one entry
     * per second goes to the event log, not one per 5 ms tick. Reset
     * to 0 on release.
     */
    uint32_t button_recovery_last_whole_sec;
    /*
     * Optional integrate-test RGB override. When `rgb_test_until_ms` is
     * non-zero and >= now_ms, the RGB policy returns `rgb_test_state`
     * verbatim. Cleared on service-mode exit, on any fault, on deployment
     * entry, or when the watchdog window expires. Watchdog window is
     * 5 seconds, set in the comms.c handler.
     */
    laser_controller_rgb_led_state_t rgb_test_state;
    laser_controller_time_ms_t rgb_test_until_ms;
} laser_controller_context_t;

static laser_controller_context_t s_context;
static laser_controller_runtime_status_t s_runtime_status;
static StaticTask_t s_control_task_tcb;
static StackType_t s_control_task_stack[LASER_CONTROLLER_CONTROL_STACK_WORDS];
static portMUX_TYPE s_context_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_runtime_status_lock = portMUX_INITIALIZER_UNLOCKED;

static laser_controller_board_outputs_t laser_controller_safe_outputs(void)
{
    return (laser_controller_board_outputs_t){
        .enable_ld_vin = false,
        .enable_tec_vin = false,
        .enable_haptic_driver = false,
        .enable_alignment_laser = false,
        /*
         * Safe posture is FAST SHUTDOWN (SBDN driven LOW). Standby (Hi-Z)
         * is reserved for intentional NIR-off-in-ready; safe outputs fire
         * on boot / power loss / unknown — must be fully shut down.
         */
        .sbdn_state = LASER_CONTROLLER_SBDN_STATE_OFF,
        .select_driver_low_current = true,
    };
}

static bool laser_controller_fault_latch_active(
    const laser_controller_context_t *context)
{
    return context != NULL &&
           context->latched_fault_code != LASER_CONTROLLER_FAULT_NONE &&
           context->latched_fault_class != LASER_CONTROLLER_FAULT_CLASS_NONE;
}

static laser_controller_fault_code_t laser_controller_effective_fault_code(
    const laser_controller_context_t *context)
{
    if (context == NULL) {
        return LASER_CONTROLLER_FAULT_NONE;
    }

    if (context->active_fault_code != LASER_CONTROLLER_FAULT_NONE) {
        return context->active_fault_code;
    }

    return context->latched_fault_code;
}

static void laser_controller_force_safe_outputs(
    laser_controller_context_t *context)
{
    const laser_controller_board_outputs_t outputs =
        laser_controller_safe_outputs();

    if (context != NULL) {
        context->last_outputs = outputs;
    }

    laser_controller_board_apply_outputs(&outputs);
}

static void laser_controller_init_nvs_store(laser_controller_time_ms_t now_ms)
{
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        (void)nvs_flash_erase();
        err = nvs_flash_init();
    }

    if (err != ESP_OK) {
        laser_controller_logger_logf(
            now_ms,
            "nvs",
            "nvs init failed: %s",
            esp_err_to_name(err));
    }
}

static const char *laser_controller_power_tier_name(laser_controller_power_tier_t power_tier)
{
    switch (power_tier) {
        case LASER_CONTROLLER_POWER_TIER_UNKNOWN:
            return "unknown";
        case LASER_CONTROLLER_POWER_TIER_PROGRAMMING_ONLY:
            return "programming_only";
        case LASER_CONTROLLER_POWER_TIER_INSUFFICIENT:
            return "insufficient";
        case LASER_CONTROLLER_POWER_TIER_REDUCED:
            return "reduced";
        case LASER_CONTROLLER_POWER_TIER_FULL:
            return "full";
        default:
            return "invalid";
    }
}

static bool laser_controller_power_tier_is_operational(laser_controller_power_tier_t power_tier)
{
    return power_tier == LASER_CONTROLLER_POWER_TIER_REDUCED ||
           power_tier == LASER_CONTROLLER_POWER_TIER_FULL;
}

static laser_controller_power_tier_t laser_controller_classify_power_tier(
    const laser_controller_config_t *config,
    const laser_controller_board_inputs_t *inputs)
{
    const float operational_min_w =
        config != NULL &&
                config->power.reduced_mode_min_w >
                    config->power.programming_only_max_w
            ? config->power.reduced_mode_min_w
            : (config != NULL ? config->power.programming_only_max_w : 0.0f);

    if (config == NULL) {
        return LASER_CONTROLLER_POWER_TIER_UNKNOWN;
    }

    if (!laser_controller_service_module_expected(LASER_CONTROLLER_MODULE_PD)) {
        return LASER_CONTROLLER_POWER_TIER_PROGRAMMING_ONLY;
    }

    if (inputs->pd_source_is_host_only) {
        return LASER_CONTROLLER_POWER_TIER_PROGRAMMING_ONLY;
    }

    if (!inputs->pd_contract_valid) {
        return LASER_CONTROLLER_POWER_TIER_PROGRAMMING_ONLY;
    }

    if (inputs->pd_negotiated_power_w < operational_min_w) {
        return LASER_CONTROLLER_POWER_TIER_INSUFFICIENT;
    }

    if (inputs->pd_negotiated_power_w <= config->power.reduced_mode_max_w) {
        return LASER_CONTROLLER_POWER_TIER_REDUCED;
    }

    if (inputs->pd_negotiated_power_w >= config->power.full_mode_min_w) {
        return LASER_CONTROLLER_POWER_TIER_FULL;
    }

    return LASER_CONTROLLER_POWER_TIER_REDUCED;
}

static bool laser_controller_config_has_valid_lut(const laser_controller_config_t *config)
{
    return config != NULL && config->wavelength_lut.point_count >= 2U;
}

static laser_controller_nm_t laser_controller_lambda_from_temp(
    const laser_controller_config_t *config,
    laser_controller_celsius_t temp_c)
{
    if (laser_controller_config_has_valid_lut(config)) {
        for (uint8_t index = 1U; index < config->wavelength_lut.point_count; ++index) {
            const float temp_low = config->wavelength_lut.target_temp_c[index - 1U];
            const float temp_high = config->wavelength_lut.target_temp_c[index];

            if (temp_c <= temp_high) {
                const float lambda_low = config->wavelength_lut.wavelength_nm[index - 1U];
                const float lambda_high = config->wavelength_lut.wavelength_nm[index];
                const float span = temp_high - temp_low;
                const float ratio = span > 0.0f ? (temp_c - temp_low) / span : 0.0f;
                return lambda_low + ((lambda_high - lambda_low) * ratio);
            }
        }

        return config->wavelength_lut.wavelength_nm[config->wavelength_lut.point_count - 1U];
    }

    if (temp_c <= LASER_CONTROLLER_BENCH_MIN_TEMP_C) {
        return LASER_CONTROLLER_BENCH_MIN_LAMBDA_NM;
    }

    if (temp_c >= LASER_CONTROLLER_BENCH_MAX_TEMP_C) {
        return LASER_CONTROLLER_BENCH_MAX_LAMBDA_NM;
    }

    return LASER_CONTROLLER_BENCH_MIN_LAMBDA_NM +
           ((temp_c - LASER_CONTROLLER_BENCH_MIN_TEMP_C) *
            (LASER_CONTROLLER_BENCH_MAX_LAMBDA_NM - LASER_CONTROLLER_BENCH_MIN_LAMBDA_NM) /
            (LASER_CONTROLLER_BENCH_MAX_TEMP_C - LASER_CONTROLLER_BENCH_MIN_TEMP_C));
}

static laser_controller_celsius_t laser_controller_temp_from_lambda(
    const laser_controller_config_t *config,
    laser_controller_nm_t target_lambda_nm)
{
    if (laser_controller_config_has_valid_lut(config)) {
        for (uint8_t index = 1U; index < config->wavelength_lut.point_count; ++index) {
            const float lambda_low = config->wavelength_lut.wavelength_nm[index - 1U];
            const float lambda_high = config->wavelength_lut.wavelength_nm[index];

            if (target_lambda_nm <= lambda_high) {
                const float temp_low = config->wavelength_lut.target_temp_c[index - 1U];
                const float temp_high = config->wavelength_lut.target_temp_c[index];
                const float span = lambda_high - lambda_low;
                const float ratio = fabsf(span) > 0.0f ?
                    (target_lambda_nm - lambda_low) / span :
                    0.0f;
                return temp_low + ((temp_high - temp_low) * ratio);
            }
        }

        return config->wavelength_lut.target_temp_c[
            config->wavelength_lut.point_count - 1U];
    }

    if (target_lambda_nm <= LASER_CONTROLLER_BENCH_MIN_LAMBDA_NM) {
        return LASER_CONTROLLER_BENCH_MIN_TEMP_C;
    }

    if (target_lambda_nm >= LASER_CONTROLLER_BENCH_MAX_LAMBDA_NM) {
        return LASER_CONTROLLER_BENCH_MAX_TEMP_C;
    }

    return LASER_CONTROLLER_BENCH_MIN_TEMP_C +
           ((target_lambda_nm - LASER_CONTROLLER_BENCH_MIN_LAMBDA_NM) *
            (LASER_CONTROLLER_BENCH_MAX_TEMP_C - LASER_CONTROLLER_BENCH_MIN_TEMP_C) /
            (LASER_CONTROLLER_BENCH_MAX_LAMBDA_NM - LASER_CONTROLLER_BENCH_MIN_LAMBDA_NM));
}

static laser_controller_celsius_t laser_controller_deployment_clamp_target_temp_c(
    const laser_controller_config_t *config,
    laser_controller_celsius_t temp_c)
{
    const float min_c =
        config != NULL ? config->thresholds.tec_min_command_c :
                         LASER_CONTROLLER_BENCH_MIN_TEMP_C;
    const float max_c =
        config != NULL ? config->thresholds.tec_max_command_c :
                         LASER_CONTROLLER_BENCH_MAX_TEMP_C;
    const float clamp_min =
        min_c < LASER_CONTROLLER_BENCH_MIN_TEMP_C ?
            LASER_CONTROLLER_BENCH_MIN_TEMP_C :
            min_c;
    const float clamp_max =
        max_c > LASER_CONTROLLER_BENCH_MAX_TEMP_C ?
            LASER_CONTROLLER_BENCH_MAX_TEMP_C :
            max_c;

    if (temp_c < clamp_min) {
        return clamp_min;
    }

    if (temp_c > clamp_max) {
        return clamp_max;
    }

    return temp_c;
}

static laser_controller_celsius_t laser_controller_deployment_target_temp_c(
    const laser_controller_context_t *context,
    const laser_controller_bench_status_t *bench_status)
{
    if (context != NULL && context->deployment.active &&
        !context->deployment.ready) {
        return context->deployment.target.target_temp_c;
    }

    return bench_status != NULL ? bench_status->target_temp_c :
                                  LASER_CONTROLLER_DEPLOYMENT_DEFAULT_TEMP_C;
}

static laser_controller_nm_t laser_controller_deployment_target_lambda_nm(
    const laser_controller_context_t *context,
    const laser_controller_bench_status_t *bench_status)
{
    if (context != NULL && context->deployment.active &&
        !context->deployment.ready) {
        return context->deployment.target.target_lambda_nm;
    }

    return bench_status != NULL ? bench_status->target_lambda_nm :
                                  laser_controller_lambda_from_temp(
                                      NULL,
                                      LASER_CONTROLLER_DEPLOYMENT_DEFAULT_TEMP_C);
}

static laser_controller_amps_t laser_controller_deployment_effective_current_limit(
    const laser_controller_context_t *context,
    const laser_controller_config_t *config)
{
    const float config_limit =
        config != NULL ? config->thresholds.max_laser_current_a : 0.0f;

    if (context != NULL && context->deployment.active &&
        context->deployment.max_laser_current_a > 0.0f) {
        if (config_limit > 0.0f &&
            context->deployment.max_laser_current_a > config_limit) {
            return config_limit;
        }

        return context->deployment.max_laser_current_a;
    }

    return config_limit;
}

static laser_controller_amps_t laser_controller_commanded_laser_current_a(
    const laser_controller_context_t *context,
    const laser_controller_config_t *config,
    const laser_controller_bench_status_t *bench_status,
    bool nir_output_enable)
{
    laser_controller_amps_t requested_current_a =
        bench_status != NULL ? bench_status->high_state_current_a : 0.0f;
    const laser_controller_amps_t current_limit =
        laser_controller_deployment_effective_current_limit(context, config);

    if (!nir_output_enable) {
        return 0.0f;
    }

    if (current_limit > 0.0f &&
        requested_current_a > current_limit) {
        requested_current_a = current_limit;
    }

    return requested_current_a < 0.0f ? 0.0f : requested_current_a;
}

static laser_controller_volts_t laser_controller_deployment_target_temp_to_voltage_v(
    const laser_controller_config_t *config,
    laser_controller_celsius_t target_temp_c)
{
    /*
     * 2026-04-20 (issue 3): prefer the bench-measured non-linear LUT from
     * `laser_controller_board.c::kTecTempCalibration` so the commanded
     * TEC setpoint voltage matches the voltage the thermistor will
     * actually read at the target temperature. The old path multiplied
     * target_temp_c * tec_command_volts_per_c (linear 0.03846 V/°C),
     * which disagreed with the LUT and produced a persistent 2-4 °C
     * offset at the TEC plate. An explicit non-zero
     * `tec_command_volts_per_c` still wins so per-unit calibration can
     * override.
     */
    float voltage_v;
    if (config != NULL && config->analog.tec_command_volts_per_c > 0.0f) {
        voltage_v = target_temp_c * config->analog.tec_command_volts_per_c;
    } else {
        voltage_v = laser_controller_board_tec_target_voltage_from_temp_c(
            target_temp_c);
    }

    if (voltage_v < 0.0f) {
        voltage_v = 0.0f;
    }

    if (voltage_v > LASER_CONTROLLER_DEPLOYMENT_DAC_FULL_SCALE_V) {
        voltage_v = LASER_CONTROLLER_DEPLOYMENT_DAC_FULL_SCALE_V;
    }

    return voltage_v;
}

static uint16_t laser_controller_deployment_voltage_to_dac_code(float voltage_v)
{
    const float clamped_v =
        voltage_v < 0.0f ? 0.0f :
        voltage_v > LASER_CONTROLLER_DEPLOYMENT_DAC_FULL_SCALE_V ?
            LASER_CONTROLLER_DEPLOYMENT_DAC_FULL_SCALE_V :
            voltage_v;
    const float ratio = clamped_v / LASER_CONTROLLER_DEPLOYMENT_DAC_FULL_SCALE_V;
    const uint32_t code =
        (uint32_t)lroundf(ratio * (float)LASER_CONTROLLER_DEPLOYMENT_DAC_CODE_MAX);

    return (uint16_t)(code > LASER_CONTROLLER_DEPLOYMENT_DAC_CODE_MAX ?
                          LASER_CONTROLLER_DEPLOYMENT_DAC_CODE_MAX :
                          code);
}

static void laser_controller_deployment_reset_secondary_effects(
    laser_controller_context_t *context)
{
    if (context == NULL) {
        return;
    }

    context->deployment.secondary_effect_count = 0U;
    memset(
        context->deployment.secondary_effects,
        0,
        sizeof(context->deployment.secondary_effects));
}

static void laser_controller_deployment_set_primary_failure(
    laser_controller_context_t *context,
    laser_controller_fault_code_t failure_code,
    const char *failure_reason)
{
    if (context == NULL) {
        return;
    }

    context->deployment.primary_failure_code = failure_code;
    (void)snprintf(
        context->deployment.primary_failure_reason,
        sizeof(context->deployment.primary_failure_reason),
        "%s",
        failure_reason != NULL ? failure_reason : "deployment failed");
}

static void laser_controller_deployment_record_secondary_effect(
    laser_controller_context_t *context,
    laser_controller_time_ms_t now_ms,
    laser_controller_fault_code_t failure_code,
    const char *failure_reason)
{
    laser_controller_deployment_secondary_effect_t *effect = NULL;

    if (context == NULL) {
        return;
    }

    for (uint32_t index = 0U;
         index < context->deployment.secondary_effect_count;
         ++index) {
        if (context->deployment.secondary_effects[index].code == failure_code &&
            strncmp(
                context->deployment.secondary_effects[index].reason,
                failure_reason != NULL ? failure_reason : "",
                sizeof(context->deployment.secondary_effects[index].reason)) == 0) {
            return;
        }
    }

    if (context->deployment.secondary_effect_count >=
        LASER_CONTROLLER_DEPLOYMENT_SECONDARY_EFFECT_COUNT) {
        return;
    }

    effect = &context->deployment.secondary_effects[
        context->deployment.secondary_effect_count++];
    effect->code = failure_code;
    effect->at_ms = now_ms;
    (void)snprintf(
        effect->reason,
        sizeof(effect->reason),
        "%s",
        failure_reason != NULL ? failure_reason : "");
}

static void laser_controller_deployment_reset_step_status(
    laser_controller_context_t *context,
    laser_controller_deployment_step_status_t status)
{
    if (context == NULL) {
        return;
    }

    for (uint32_t index = 0U;
         index < LASER_CONTROLLER_DEPLOYMENT_STEP_COUNT;
         ++index) {
        context->deployment.step_status[index] = status;
        context->deployment.step_started_at_ms[index] = 0U;
        context->deployment.step_completed_at_ms[index] = 0U;
    }

    context->deployment.step_status[LASER_CONTROLLER_DEPLOYMENT_STEP_NONE] =
        LASER_CONTROLLER_DEPLOYMENT_STEP_STATUS_INACTIVE;
    context->deployment_force_ld_safe = false;
    context->deployment_tec_loss_since_ms = 0U;
    context->deployment_ld_loss_since_ms = 0U;
    context->deployment_tec_contradiction_reported = false;
}

static void laser_controller_deployment_begin_step(
    laser_controller_context_t *context,
    laser_controller_deployment_step_t step,
    laser_controller_time_ms_t now_ms)
{
    if (context == NULL ||
        step <= LASER_CONTROLLER_DEPLOYMENT_STEP_NONE ||
        step >= LASER_CONTROLLER_DEPLOYMENT_STEP_COUNT) {
        return;
    }

    context->deployment.current_step = step;
    context->deployment.step_status[step] =
        LASER_CONTROLLER_DEPLOYMENT_STEP_STATUS_IN_PROGRESS;
    context->deployment.step_started_at_ms[step] = now_ms;
    context->deployment.step_completed_at_ms[step] = 0U;
    context->deployment_step_started_ms = now_ms;
    context->deployment_phase_started_ms = now_ms;
    context->deployment_substep = 0U;
    context->deployment_step_action_performed = false;
    context->deployment.phase = LASER_CONTROLLER_DEPLOYMENT_PHASE_CHECKLIST;
}

static bool laser_controller_deployment_step_delay_elapsed(
    const laser_controller_context_t *context,
    laser_controller_time_ms_t now_ms)
{
    return context == NULL ||
           context->deployment.current_step <= LASER_CONTROLLER_DEPLOYMENT_STEP_NONE ||
           context->deployment.current_step >= LASER_CONTROLLER_DEPLOYMENT_STEP_COUNT ||
           (now_ms - context->deployment_step_started_ms) >=
               LASER_CONTROLLER_DEPLOYMENT_STEP_DELAY_MS;
}

static void laser_controller_deployment_complete_current_step(
    laser_controller_context_t *context,
    laser_controller_time_ms_t now_ms)
{
    laser_controller_deployment_step_t next_step;

    if (context == NULL) {
        return;
    }

    if (context->deployment.current_step <= LASER_CONTROLLER_DEPLOYMENT_STEP_NONE ||
        context->deployment.current_step >= LASER_CONTROLLER_DEPLOYMENT_STEP_COUNT) {
        return;
    }

    context->deployment.step_status[context->deployment.current_step] =
        LASER_CONTROLLER_DEPLOYMENT_STEP_STATUS_PASSED;
    context->deployment.step_completed_at_ms[context->deployment.current_step] =
        now_ms;
    context->deployment.last_completed_step = context->deployment.current_step;
    next_step = (laser_controller_deployment_step_t)(context->deployment.current_step + 1U);

    if (next_step >= LASER_CONTROLLER_DEPLOYMENT_STEP_COUNT) {
        context->deployment.current_step = LASER_CONTROLLER_DEPLOYMENT_STEP_NONE;
        context->deployment.running = false;
        context->deployment.ready = true;
        context->deployment.ready_idle = true;
        context->deployment.ready_qualified = true;
        context->deployment.ready_invalidated = false;
        context->deployment.failed = false;
        context->deployment.phase = LASER_CONTROLLER_DEPLOYMENT_PHASE_READY_IDLE;
        context->deployment.failure_code = LASER_CONTROLLER_FAULT_NONE;
        context->deployment.failure_reason[0] = '\0';
        context->deployment_force_ld_safe = false;
        if (context->fault_latched ||
            context->active_fault_code != LASER_CONTROLLER_FAULT_NONE ||
            context->active_fault_class != LASER_CONTROLLER_FAULT_CLASS_NONE ||
            context->latched_fault_code != LASER_CONTROLLER_FAULT_NONE ||
            context->latched_fault_class != LASER_CONTROLLER_FAULT_CLASS_NONE) {
            laser_controller_logger_logf(
                now_ms,
                "deploy",
                "deployment success cleared active=%s (%s) latched=%s (%s)",
                laser_controller_fault_code_name(context->active_fault_code),
                laser_controller_fault_class_name(context->active_fault_class),
                laser_controller_fault_code_name(context->latched_fault_code),
                laser_controller_fault_class_name(context->latched_fault_class));
        }
        context->fault_latched = false;
        context->active_fault_code = LASER_CONTROLLER_FAULT_NONE;
        context->active_fault_class = LASER_CONTROLLER_FAULT_CLASS_NONE;
        context->latched_fault_code = LASER_CONTROLLER_FAULT_NONE;
        context->latched_fault_class = LASER_CONTROLLER_FAULT_CLASS_NONE;
        context->active_fault_reason[0] = '\0';
        context->latched_fault_reason[0] = '\0';
        context->deployment_step_started_ms = now_ms;
        context->deployment_phase_started_ms = now_ms;
        context->deployment_step_action_performed = false;
        return;
    }

    laser_controller_deployment_begin_step(context, next_step, now_ms);
}

static void laser_controller_deployment_fail(
    laser_controller_context_t *context,
    laser_controller_time_ms_t now_ms,
    laser_controller_fault_code_t failure_code,
    const char *failure_reason)
{
    if (context == NULL) {
        return;
    }

    if (context->deployment.current_step > LASER_CONTROLLER_DEPLOYMENT_STEP_NONE &&
        context->deployment.current_step < LASER_CONTROLLER_DEPLOYMENT_STEP_COUNT) {
        context->deployment.step_status[context->deployment.current_step] =
            LASER_CONTROLLER_DEPLOYMENT_STEP_STATUS_FAILED;
        context->deployment.step_completed_at_ms[context->deployment.current_step] =
            now_ms;
    }

    context->deployment.running = false;
    context->deployment.ready = false;
    context->deployment.ready_idle = false;
    context->deployment.ready_qualified = false;
    context->deployment.ready_invalidated = false;
    context->deployment.failed = true;
    context->deployment.phase = LASER_CONTROLLER_DEPLOYMENT_PHASE_FAILED;
    context->deployment.failure_code = failure_code;
    context->deployment.current_step = LASER_CONTROLLER_DEPLOYMENT_STEP_NONE;
    context->deployment_step_started_ms = now_ms;
    context->deployment_phase_started_ms = now_ms;
    context->deployment_substep = 0U;
    context->deployment_step_action_performed = false;
    (void)snprintf(
        context->deployment.failure_reason,
        sizeof(context->deployment.failure_reason),
        "%s",
        failure_reason != NULL ? failure_reason : "deployment failed");
    if (context->deployment.primary_failure_code == LASER_CONTROLLER_FAULT_NONE) {
        laser_controller_deployment_set_primary_failure(
            context,
            failure_code,
            failure_reason);
    } else if (context->deployment.primary_failure_code != failure_code ||
               strncmp(
                   context->deployment.primary_failure_reason,
                   failure_reason != NULL ? failure_reason : "",
                   sizeof(context->deployment.primary_failure_reason)) != 0) {
        laser_controller_deployment_record_secondary_effect(
            context,
            now_ms,
            failure_code,
            failure_reason);
    }
    laser_controller_logger_logf(
        now_ms,
        "deploy",
        "deployment failed: %s (%s)",
        laser_controller_fault_code_name(failure_code),
        context->deployment.failure_reason);
}

static void laser_controller_deployment_invalidate_ready(
    laser_controller_context_t *context,
    laser_controller_time_ms_t now_ms,
    laser_controller_fault_code_t failure_code,
    const char *failure_reason)
{
    if (context == NULL ||
        !context->deployment.active ||
        !context->deployment.ready) {
        return;
    }

    context->deployment.ready = false;
    context->deployment.ready_idle = false;
    context->deployment.ready_qualified = false;
    context->deployment.ready_invalidated = true;
    context->deployment.failed = true;
    context->deployment.running = false;
    context->deployment.phase = LASER_CONTROLLER_DEPLOYMENT_PHASE_FAILED;
    context->deployment.current_step = LASER_CONTROLLER_DEPLOYMENT_STEP_NONE;
    context->deployment.failure_code = failure_code;
    (void)snprintf(
        context->deployment.failure_reason,
        sizeof(context->deployment.failure_reason),
        "%s",
        failure_reason != NULL ? failure_reason : "deployment invalidated");
    if (context->deployment.primary_failure_code == LASER_CONTROLLER_FAULT_NONE) {
        laser_controller_deployment_set_primary_failure(
            context,
            failure_code,
            failure_reason);
    } else if (context->deployment.primary_failure_code != failure_code ||
               strncmp(
                   context->deployment.primary_failure_reason,
                   failure_reason != NULL ? failure_reason : "",
                   sizeof(context->deployment.primary_failure_reason)) != 0) {
        laser_controller_deployment_record_secondary_effect(
            context,
            now_ms,
            failure_code,
            failure_reason);
    }
    laser_controller_logger_logf(
        now_ms,
        "deploy",
        "deployment ready state cleared: %s (%s)",
        laser_controller_fault_code_name(failure_code),
        context->deployment.failure_reason);
}

static const laser_controller_board_gpio_pin_readback_t *
laser_controller_deployment_find_gpio_pin(
    const laser_controller_board_inputs_t *inputs,
    uint32_t gpio_num)
{
    if (inputs == NULL) {
        return NULL;
    }

    for (size_t index = 0U;
         index < LASER_CONTROLLER_BOARD_GPIO_INSPECTOR_PIN_COUNT;
         ++index) {
        const laser_controller_board_gpio_pin_readback_t *pin =
            &inputs->gpio_inspector.pins[index];

        if ((uint32_t)pin->gpio_num == gpio_num) {
            return pin;
        }
    }

    return NULL;
}

static bool laser_controller_deployment_pin_is_low(
    const laser_controller_board_inputs_t *inputs,
    uint32_t gpio_num)
{
    const laser_controller_board_gpio_pin_readback_t *pin =
        laser_controller_deployment_find_gpio_pin(inputs, gpio_num);

    return pin != NULL && !pin->level_high;
}

static bool laser_controller_deployment_pin_is_high(
    const laser_controller_board_inputs_t *inputs,
    uint32_t gpio_num)
{
    const laser_controller_board_gpio_pin_readback_t *pin =
        laser_controller_deployment_find_gpio_pin(inputs, gpio_num);

    return pin != NULL && pin->level_high;
}

static void laser_controller_deployment_update_ready_truth(
    laser_controller_context_t *context,
    bool tec_filtered,
    bool ld_filtered)
{
    if (context == NULL) {
        return;
    }

    context->deployment.ready_truth.tec_rail_pgood_raw =
        context->last_inputs.tec_rail_pgood;
    context->deployment.ready_truth.tec_rail_pgood_filtered = tec_filtered;
    context->deployment.ready_truth.tec_temp_good =
        context->last_inputs.tec_temp_good;
    context->deployment.ready_truth.tec_analog_plausible =
        context->last_inputs.tec_temp_adc_voltage_v > 0.05f &&
        context->last_inputs.tec_temp_adc_voltage_v < 3.2f &&
        fabsf(context->last_inputs.tec_current_a) < 10.0f &&
        context->last_inputs.tec_voltage_v < 10.0f;
    context->deployment.ready_truth.ld_rail_pgood_raw =
        context->last_inputs.ld_rail_pgood;
    context->deployment.ready_truth.ld_rail_pgood_filtered = ld_filtered;
    context->deployment.ready_truth.driver_loop_good =
        context->last_inputs.driver_loop_good;
    context->deployment.ready_truth.sbdn_high =
        laser_controller_deployment_pin_is_high(
            &context->last_inputs,
            LASER_CONTROLLER_GPIO_LD_SBDN);
    context->deployment.ready_truth.pcn_low =
        laser_controller_deployment_pin_is_low(
            &context->last_inputs,
            LASER_CONTROLLER_GPIO_LD_PCN);
    context->deployment.ready_truth.idle_bias_current_a =
        context->last_inputs.measured_laser_current_a;
}

static bool laser_controller_deployment_outputs_off_confirmed(
    const laser_controller_context_t *context)
{
    const laser_controller_board_inputs_t *inputs =
        context != NULL ? &context->last_inputs : NULL;

    return context != NULL &&
           inputs != NULL &&
           !context->last_outputs.enable_ld_vin &&
           !context->last_outputs.enable_tec_vin &&
           !context->last_outputs.enable_haptic_driver &&
           !context->last_outputs.enable_alignment_laser &&
           context->last_outputs.sbdn_state == LASER_CONTROLLER_SBDN_STATE_OFF &&
           context->last_outputs.select_driver_low_current &&
           laser_controller_deployment_pin_is_low(inputs, LASER_CONTROLLER_GPIO_LD_SBDN) &&
           laser_controller_deployment_pin_is_low(inputs, LASER_CONTROLLER_GPIO_LD_PCN) &&
           laser_controller_deployment_pin_is_low(inputs, LASER_CONTROLLER_GPIO_PWR_LD_EN) &&
           laser_controller_deployment_pin_is_low(inputs, LASER_CONTROLLER_GPIO_PWR_TEC_EN) &&
           laser_controller_deployment_pin_is_low(inputs, LASER_CONTROLLER_GPIO_ERM_EN) &&
           laser_controller_deployment_pin_is_low(inputs, LASER_CONTROLLER_GPIO_ERM_TRIG_GN_LD_EN) &&
           !inputs->pcn_pwm_active &&
           !inputs->tof_illumination_pwm_active;
}

static bool laser_controller_deployment_ready_posture_confirmed(
    const laser_controller_context_t *context)
{
    const laser_controller_board_inputs_t *inputs =
        context != NULL ? &context->last_inputs : NULL;

    return context != NULL &&
           inputs != NULL &&
           context->last_outputs.enable_ld_vin &&
           context->last_outputs.enable_tec_vin &&
           context->last_outputs.sbdn_state == LASER_CONTROLLER_SBDN_STATE_ON &&
           context->last_outputs.select_driver_low_current &&
           inputs->ld_rail_pgood &&
           inputs->tec_rail_pgood &&
           laser_controller_deployment_pin_is_high(inputs, LASER_CONTROLLER_GPIO_LD_SBDN) &&
           laser_controller_deployment_pin_is_low(inputs, LASER_CONTROLLER_GPIO_LD_PCN) &&
           !inputs->pcn_pwm_active &&
           !inputs->tof_illumination_pwm_active;
}

static bool laser_controller_deployment_ownership_reclaimed(
    const laser_controller_context_t *context)
{
    return context != NULL &&
           !laser_controller_service_mode_requested() &&
           !laser_controller_service_interlocks_disabled() &&
           context->state_machine.current != LASER_CONTROLLER_STATE_SERVICE_MODE &&
           !context->last_inputs.gpio_inspector.any_override_active &&
           !context->last_inputs.tof_illumination_pwm_active &&
           !context->last_inputs.pcn_pwm_active;
}

static void laser_controller_deployment_recompute_power_cap(
    laser_controller_context_t *context,
    const laser_controller_config_t *config)
{
    float max_current_a =
        config != NULL ? config->thresholds.max_laser_current_a : 0.0f;

    if (context == NULL || config == NULL) {
        return;
    }

    if (context->last_inputs.pd_contract_valid &&
        context->last_inputs.pd_negotiated_power_w > 0.0f &&
        context->last_inputs.pd_negotiated_power_w < LASER_CONTROLLER_DEPLOYMENT_FULL_POWER_W) {
        const float laser_budget_w =
            context->last_inputs.pd_negotiated_power_w -
            LASER_CONTROLLER_DEPLOYMENT_TEC_RESERVE_W -
            LASER_CONTROLLER_DEPLOYMENT_PERIPHERAL_RESERVE_W;
        const float capped_current_a =
            laser_budget_w > 0.0f ?
                (laser_budget_w * 0.9f) / 3.0f :
                0.0f;

        if (capped_current_a < max_current_a) {
            max_current_a = capped_current_a;
        }
    }

    if (max_current_a < 0.0f) {
        max_current_a = 0.0f;
    }

    context->deployment.max_laser_current_a = max_current_a;
    context->deployment.max_optical_power_w = max_current_a;
}

static uint32_t laser_controller_deployment_rail_sequence_wait_ms(
    const laser_controller_config_t *config)
{
    uint32_t wait_ms =
        config != NULL ? config->timeouts.rail_good_timeout_ms : 0U;

    if (wait_ms < LASER_CONTROLLER_DEPLOYMENT_RAIL_SEQUENCE_MIN_WAIT_MS) {
        wait_ms = LASER_CONTROLLER_DEPLOYMENT_RAIL_SEQUENCE_MIN_WAIT_MS;
    }

    return wait_ms;
}

static uint32_t laser_controller_deployment_ready_wait_ms(
    const laser_controller_config_t *config)
{
    uint32_t wait_ms = LASER_CONTROLLER_DEPLOYMENT_READY_WAIT_MS;

    if (config != NULL && config->timeouts.rail_good_timeout_ms > wait_ms) {
        wait_ms = config->timeouts.rail_good_timeout_ms;
    }
    if (wait_ms < LASER_CONTROLLER_DEPLOYMENT_READY_MIN_WAIT_MS) {
        wait_ms = LASER_CONTROLLER_DEPLOYMENT_READY_MIN_WAIT_MS;
    }

    return wait_ms;
}

static void laser_controller_deployment_tick(
    laser_controller_context_t *context,
    const laser_controller_config_t *config,
    laser_controller_time_ms_t now_ms)
{
    float actual_lambda_nm = 0.0f;
    float target_lambda_nm = 0.0f;
    float lambda_drift_nm = 0.0f;
    bool lambda_drift_blocked = false;
    bool tec_temp_adc_blocked = false;
    bool tec_telemetry_plausible = false;
    const uint32_t deployment_rail_wait_ms =
        laser_controller_deployment_rail_sequence_wait_ms(config);
    const uint32_t deployment_ready_wait_ms =
        laser_controller_deployment_ready_wait_ms(config);

    if (context == NULL || config == NULL || !context->deployment.active) {
        return;
    }

    actual_lambda_nm =
        laser_controller_lambda_from_temp(config, context->last_inputs.tec_temp_c);
    target_lambda_nm = context->deployment.target.target_lambda_nm;
    lambda_drift_nm =
        actual_lambda_nm > target_lambda_nm ?
            (actual_lambda_nm - target_lambda_nm) :
            (target_lambda_nm - actual_lambda_nm);
    lambda_drift_blocked =
        lambda_drift_nm > config->thresholds.lambda_drift_limit_nm;
    tec_temp_adc_blocked =
        context->last_inputs.tec_temp_adc_voltage_v >
        config->thresholds.tec_temp_adc_trip_v;
    tec_telemetry_plausible =
        context->last_inputs.tec_temp_adc_voltage_v > 0.05f &&
        context->last_inputs.tec_temp_adc_voltage_v < 3.2f &&
        fabsf(context->last_inputs.tec_current_a) < 10.0f &&
        context->last_inputs.tec_voltage_v < 10.0f;

    laser_controller_deployment_update_ready_truth(
        context,
        context->last_inputs.tec_rail_pgood,
        context->last_inputs.ld_rail_pgood);

    if (context->deployment.ready && !context->deployment.running) {
        const bool tec_contradiction =
            !context->last_inputs.tec_rail_pgood &&
            context->last_inputs.tec_temp_good &&
            tec_telemetry_plausible;

        if (!context->last_inputs.pd_contract_valid ||
            context->last_inputs.pd_source_voltage_v <
                LASER_CONTROLLER_DEPLOYMENT_MIN_LD_SOURCE_V) {
            laser_controller_deployment_invalidate_ready(
                context,
                now_ms,
                LASER_CONTROLLER_FAULT_PD_LOST,
                "deployment source voltage or PD contract became invalid");
            return;
        }

        if (!context->last_inputs.tec_rail_pgood) {
            if (tec_contradiction) {
                if (!context->deployment_tec_contradiction_reported) {
                    context->deployment_tec_contradiction_reported = true;
                    laser_controller_deployment_record_secondary_effect(
                        context,
                        now_ms,
                        LASER_CONTROLLER_FAULT_UNEXPECTED_STATE,
                        "deployment telemetry contradiction: TEC PGOOD low while TEMPGD and analog telemetry remain plausible");
                    laser_controller_logger_log(
                        now_ms,
                        "deploy",
                        "deployment contradiction observed: TEC PGOOD low while TEMPGD remains high");
                }
                context->deployment_tec_loss_since_ms = 0U;
                context->deployment_force_ld_safe = false;
            } else {
                if (context->deployment_tec_loss_since_ms == 0U) {
                    context->deployment_tec_loss_since_ms = now_ms;
                }
                context->deployment_force_ld_safe = true;
                if ((now_ms - context->deployment_tec_loss_since_ms) >=
                    config->timeouts.rail_good_timeout_ms) {
                    laser_controller_deployment_invalidate_ready(
                        context,
                        now_ms,
                        LASER_CONTROLLER_FAULT_TEC_RAIL_BAD,
                        "deployment TEC rail loss was corroborated by temperature telemetry");
                }
            }
        } else {
            context->deployment_tec_loss_since_ms = 0U;
            context->deployment_force_ld_safe = false;
        }

        if (!context->last_inputs.ld_rail_pgood) {
            if (context->deployment_ld_loss_since_ms == 0U) {
                context->deployment_ld_loss_since_ms = now_ms;
            }
            if ((now_ms - context->deployment_ld_loss_since_ms) >=
                config->timeouts.rail_good_timeout_ms) {
                laser_controller_deployment_invalidate_ready(
                    context,
                    now_ms,
                    LASER_CONTROLLER_FAULT_LD_RAIL_BAD,
                    "deployment LD rail lost PGOOD");
            }
        } else {
            context->deployment_ld_loss_since_ms = 0U;
        }

        /*
         * Deployment-runtime loop-bad watch was previously here and called
         * `deployment_invalidate_ready(LD_LOOP_BAD, ...)` whenever the
         * driver loop dropped while emitting. That permanently invalidated
         * the deployment ready posture and forced the operator to re-run
         * the entire checklist for any transient loop blip — even though
         * SBDN is now held HIGH throughout deployment so a momentary loop
         * drop is recoverable. Removed 2026-04-17.
         *
         * The safety evaluator's loop-bad gate (safety.c, INTERLOCK_AUTO_
         * CLEAR class) still trips immediately and forces SBDN OFF so the
         * driver stops emitting; on loop recovery the fault auto-clears
         * and SBDN goes back HIGH on the next tick. Deployment ready
         * posture stays intact across the blip.
         */
        laser_controller_deployment_update_ready_truth(
            context,
            context->last_inputs.tec_rail_pgood || tec_contradiction,
            context->last_inputs.ld_rail_pgood ||
                (context->deployment_ld_loss_since_ms != 0U &&
                 (now_ms - context->deployment_ld_loss_since_ms) <
                     config->timeouts.rail_good_timeout_ms));
        return;
    }

    if (!context->deployment.running) {
        return;
    }

    if (!laser_controller_deployment_step_delay_elapsed(context, now_ms)) {
        return;
    }

    switch (context->deployment.current_step) {
        case LASER_CONTROLLER_DEPLOYMENT_STEP_OWNERSHIP_RECLAIM:
            if (laser_controller_deployment_ownership_reclaimed(context)) {
                laser_controller_deployment_complete_current_step(context, now_ms);
            } else if ((now_ms - context->deployment_step_started_ms) >=
                       LASER_CONTROLLER_DEPLOYMENT_OWNERSHIP_WAIT_MS) {
                laser_controller_deployment_fail(
                    context,
                    now_ms,
                    LASER_CONTROLLER_FAULT_SERVICE_OVERRIDE_REJECTED,
                    "service ownership did not clear before deployment");
            }
            break;
        case LASER_CONTROLLER_DEPLOYMENT_STEP_PD_INSPECT:
            if (context->last_inputs.pd_contract_valid &&
                context->last_inputs.pd_source_voltage_v >=
                    LASER_CONTROLLER_DEPLOYMENT_MIN_LD_SOURCE_V) {
                laser_controller_deployment_complete_current_step(context, now_ms);
            } else if ((now_ms - context->deployment_step_started_ms) >=
                       LASER_CONTROLLER_DEPLOYMENT_PD_WAIT_MS) {
                laser_controller_deployment_fail(
                    context,
                    now_ms,
                    context->last_inputs.pd_last_updated_ms == 0U ?
                        LASER_CONTROLLER_FAULT_PD_LOST :
                    context->last_inputs.pd_source_voltage_v <
                            LASER_CONTROLLER_DEPLOYMENT_MIN_LD_SOURCE_V ?
                        LASER_CONTROLLER_FAULT_PD_INSUFFICIENT :
                        LASER_CONTROLLER_FAULT_PD_LOST,
                    context->last_inputs.pd_last_updated_ms == 0U ?
                        "no cached PD snapshot is available; refresh PD from Integrate before deployment" :
                        "valid cached PD source with at least 9V was not present");
            }
            break;
        case LASER_CONTROLLER_DEPLOYMENT_STEP_POWER_CAP:
            laser_controller_deployment_recompute_power_cap(context, config);
            if (context->deployment.max_laser_current_a <= 0.0f) {
                laser_controller_deployment_fail(
                    context,
                    now_ms,
                    LASER_CONTROLLER_FAULT_PD_INSUFFICIENT,
                    "deployment PD budget left no laser operating headroom");
                break;
            }
            laser_controller_logger_logf(
                now_ms,
                "deploy",
                "deployment current cap %.3f A (pd=%.1fW, target=%s %.2f)",
                context->deployment.max_laser_current_a,
                context->last_inputs.pd_negotiated_power_w,
                laser_controller_deployment_target_mode_name(
                    context->deployment.target.target_mode),
                context->deployment.target.target_mode ==
                        LASER_CONTROLLER_DEPLOYMENT_TARGET_MODE_TEMP ?
                    context->deployment.target.target_temp_c :
                    context->deployment.target.target_lambda_nm);
            laser_controller_deployment_complete_current_step(context, now_ms);
            break;
        case LASER_CONTROLLER_DEPLOYMENT_STEP_OUTPUTS_OFF:
            if (laser_controller_deployment_outputs_off_confirmed(context)) {
                laser_controller_deployment_complete_current_step(context, now_ms);
            } else if ((now_ms - context->deployment_step_started_ms) >=
                       LASER_CONTROLLER_DEPLOYMENT_OUTPUTS_WAIT_MS) {
                laser_controller_deployment_fail(
                    context,
                    now_ms,
                    LASER_CONTROLLER_FAULT_SERVICE_OVERRIDE_REJECTED,
                    "controlled outputs did not reach the required safe-off posture");
            }
            break;
        case LASER_CONTROLLER_DEPLOYMENT_STEP_STABILIZE_3V3:
            if ((now_ms - context->deployment_step_started_ms) >=
                LASER_CONTROLLER_DEPLOYMENT_STABILIZE_MS) {
                laser_controller_deployment_complete_current_step(context, now_ms);
            }
            break;
        case LASER_CONTROLLER_DEPLOYMENT_STEP_DAC_SAFE_ZERO:
            if (!context->deployment_step_action_performed) {
                const esp_err_t dac_err =
                    laser_controller_board_configure_dac_debug(
                        LASER_CONTROLLER_SERVICE_DAC_REFERENCE_INTERNAL,
                        true,
                        true,
                        LASER_CONTROLLER_SERVICE_DAC_SYNC_ASYNC);
                const esp_err_t ld_err =
                    dac_err == ESP_OK ?
                        laser_controller_board_set_dac_debug_output(false, 0.0f) :
                        dac_err;
                const esp_err_t tec_err =
                    ld_err == ESP_OK ?
                        laser_controller_board_set_dac_debug_output(
                            true,
                            laser_controller_deployment_target_temp_to_voltage_v(
                                config,
                                LASER_CONTROLLER_DEPLOYMENT_DEFAULT_TEMP_C)) :
                        ld_err;

                context->deployment_step_action_performed = true;
                context->deployment_phase_started_ms = now_ms;
                if (tec_err != ESP_OK) {
                    laser_controller_deployment_fail(
                        context,
                        now_ms,
                        LASER_CONTROLLER_FAULT_INVALID_CONFIG,
                        "DAC safe-zero initialization failed");
                }
                break;
            }
            if (context->last_inputs.dac.reachable &&
                context->last_inputs.dac.configured &&
                !context->last_inputs.dac.ref_alarm &&
                context->last_inputs.dac.data_a_reg == 0U &&
                context->last_inputs.dac.data_b_reg ==
                    laser_controller_deployment_voltage_to_dac_code(
                        laser_controller_deployment_target_temp_to_voltage_v(
                            config,
                            LASER_CONTROLLER_DEPLOYMENT_DEFAULT_TEMP_C))) {
                laser_controller_deployment_complete_current_step(context, now_ms);
            } else if ((now_ms - context->deployment_step_started_ms) >=
                       LASER_CONTROLLER_DEPLOYMENT_DAC_WAIT_MS) {
                laser_controller_deployment_fail(
                    context,
                    now_ms,
                    LASER_CONTROLLER_FAULT_INVALID_CONFIG,
                    "DAC readback did not match safe deployment initialization");
            }
            break;
        case LASER_CONTROLLER_DEPLOYMENT_STEP_PERIPHERALS_VERIFY: {
            /*
             * 2026-04-17 user directive: if an operator has disabled a
             * peripheral's runtime interlocks from the Integrate safety
             * form (or flipped ToF to low-bound-only), they explicitly
             * want that peripheral NOT to gate deployment either. A
             * peripheral's readback is considered satisfied-by-policy
             * when its invalid+stale pair are both disabled, or — for
             * ToF — when low-bound-only mode is on (which already
             * ignores missing / stale data at runtime).
             */
            const laser_controller_interlock_enable_t *il =
                &context->config.thresholds.interlocks;
            const bool tof_gate_bypassed =
                il->tof_low_bound_only ||
                (!il->tof_invalid_enabled && !il->tof_stale_enabled);
            const bool imu_gate_bypassed =
                !il->imu_invalid_enabled && !il->imu_stale_enabled;

            const bool imu_ok =
                imu_gate_bypassed ||
                (context->last_inputs.imu_readback.reachable &&
                 context->last_inputs.imu_readback.configured &&
                 context->last_inputs.imu_readback.who_am_i == 0x6CU);
            const bool tof_ok =
                tof_gate_bypassed ||
                (context->last_inputs.tof_readback.reachable &&
                 context->last_inputs.tof_readback.configured &&
                 context->last_inputs.tof_readback.boot_state != 0U &&
                 context->last_inputs.tof_readback.sensor_id == 0xEACCU);
            const bool haptic_ok =
                context->last_inputs.haptic_readback.reachable;

            if (imu_ok && tof_ok && haptic_ok) {
                laser_controller_deployment_complete_current_step(context, now_ms);
            } else if ((now_ms - context->deployment_step_started_ms) >=
                       LASER_CONTROLLER_DEPLOYMENT_PERIPHERAL_WAIT_MS) {
                laser_controller_deployment_fail(
                    context,
                    now_ms,
                    !imu_ok ?
                        LASER_CONTROLLER_FAULT_IMU_INVALID :
                    !tof_ok ?
                        LASER_CONTROLLER_FAULT_TOF_INVALID :
                        LASER_CONTROLLER_FAULT_SERVICE_OVERRIDE_REJECTED,
                    "one or more deployment peripherals failed readback verification");
            }
            break;
        }
        case LASER_CONTROLLER_DEPLOYMENT_STEP_RAIL_SEQUENCE:
            if (!context->deployment_step_action_performed) {
                context->deployment_substep =
                    LASER_CONTROLLER_DEPLOYMENT_RAIL_PHASE_TEC;
                context->deployment_step_action_performed = true;
                context->deployment_phase_started_ms = now_ms;
                break;
            }
            if (context->deployment_substep ==
                LASER_CONTROLLER_DEPLOYMENT_RAIL_PHASE_TEC) {
                if (context->last_inputs.tec_rail_pgood) {
                    context->deployment_substep =
                        LASER_CONTROLLER_DEPLOYMENT_RAIL_PHASE_LD;
                    context->deployment_phase_started_ms = now_ms;
                } else if ((now_ms - context->deployment_phase_started_ms) >=
                           deployment_rail_wait_ms) {
                    laser_controller_deployment_fail(
                        context,
                        now_ms,
                        LASER_CONTROLLER_FAULT_TEC_RAIL_BAD,
                        "TEC rail PGOOD did not assert during deployment sequencing");
                }
                break;
            }
            if (context->last_inputs.ld_rail_pgood) {
                laser_controller_deployment_complete_current_step(context, now_ms);
            } else if ((now_ms - context->deployment_phase_started_ms) >=
                       deployment_rail_wait_ms) {
                laser_controller_deployment_fail(
                    context,
                    now_ms,
                    LASER_CONTROLLER_FAULT_LD_RAIL_BAD,
                    "LD rail PGOOD did not assert during deployment sequencing");
            }
            break;
        case LASER_CONTROLLER_DEPLOYMENT_STEP_TEC_SETTLE:
            if (context->last_inputs.tec_rail_pgood &&
                context->last_inputs.ld_rail_pgood &&
                context->last_inputs.tec_temp_good &&
                tec_telemetry_plausible &&
                !lambda_drift_blocked &&
                !tec_temp_adc_blocked) {
                laser_controller_deployment_complete_current_step(context, now_ms);
            } else if ((now_ms - context->deployment_step_started_ms) >=
                       config->timeouts.tec_settle_timeout_ms) {
                laser_controller_deployment_fail(
                    context,
                    now_ms,
                    LASER_CONTROLLER_FAULT_TEC_NOT_SETTLED,
                    "TEC did not settle to the deployment target");
            }
            break;
        case LASER_CONTROLLER_DEPLOYMENT_STEP_LP_GOOD_CHECK:
            /*
             * Three-phase loop-lock verification.
             *
             * Originally each substep required 2 s of stability before
             * advancing. Per user directive 2026-04-16 ("keep checking
             * and if condition met, skip the wait; intentional delays
             * ≤ 200 ms"), the stability floors are reduced to 200 ms.
             * The 200 ms window is still sampled every 5 ms, so once
             * the condition is met the step advances on the next tick.
             *
             *   substep 0 — "LD rail settle". SBDN stays OFF. Wait for
             *     `ld_rail_pgood_for_ms >= 200` so the rail is briefly
             *     stable before the driver is enabled. Fails at 5 s if
             *     the rail never stabilizes.
             *
             *   substep 1 — "SBDN settle". derive_outputs drives SBDN
             *     HIGH (OPERATE). Wait for `sbdn_not_off_for_ms >= 200`
             *     (committed last_outputs.sbdn_state == ON). Fails at
             *     5 s if SBDN never reaches HIGH.
             *
             *   substep 2 — "LP_GOOD probe". Check `driver_loop_good`
             *     every tick within a 1 s window. Fails with
             *     LD_LP_GOOD_TIMEOUT if the line never asserts.
             *
             * select_driver_low_current stays true throughout so PCN is
             * LOW — this is a loop-lock-at-zero-drive check. READY_POSTURE
             * still does the bias verification afterward.
             */
            {
                const uint32_t ld_pgood_for_ms =
                    context->ld_rail_pgood_since_ms == 0U
                        ? 0U
                        : (uint32_t)(now_ms - context->ld_rail_pgood_since_ms);
                const uint32_t sbdn_not_off_for_ms =
                    context->sbdn_not_off_since_ms == 0U
                        ? 0U
                        : (uint32_t)(now_ms - context->sbdn_not_off_since_ms);

                if (context->deployment_substep == 0U) {
                    if (ld_pgood_for_ms >= 200U) {
                        context->deployment_substep = 1U;
                        context->deployment_step_started_ms = now_ms;
                        laser_controller_logger_log(
                            now_ms,
                            "deploy",
                            "LP_GOOD_CHECK: LD rail stable, driving SBDN HIGH");
                    } else if ((now_ms - context->deployment_step_started_ms) >=
                               5000U) {
                        laser_controller_deployment_fail(
                            context,
                            now_ms,
                            LASER_CONTROLLER_FAULT_LD_RAIL_BAD,
                            "LD rail did not hold PGOOD during LP_GOOD_CHECK");
                    }
                } else if (context->deployment_substep == 1U) {
                    if (sbdn_not_off_for_ms >= 200U &&
                        context->last_outputs.sbdn_state ==
                            LASER_CONTROLLER_SBDN_STATE_ON) {
                        context->deployment_substep = 2U;
                        context->deployment_step_started_ms = now_ms;
                        laser_controller_logger_log(
                            now_ms,
                            "deploy",
                            "LP_GOOD_CHECK: SBDN HIGH stable, probing LP_GOOD");
                    } else if ((now_ms - context->deployment_step_started_ms) >=
                               5000U) {
                        laser_controller_deployment_fail(
                            context,
                            now_ms,
                            LASER_CONTROLLER_FAULT_LD_LP_GOOD_TIMEOUT,
                            "SBDN did not reach HIGH+stable within 5 s during LP_GOOD_CHECK");
                    }
                } else {
                    if (context->last_inputs.driver_loop_good) {
                        laser_controller_deployment_complete_current_step(
                            context, now_ms);
                    } else if ((now_ms - context->deployment_step_started_ms) >=
                               1000U) {
                        laser_controller_deployment_fail(
                            context,
                            now_ms,
                            LASER_CONTROLLER_FAULT_LD_LP_GOOD_TIMEOUT,
                            "SBDN HIGH but LP_GOOD never asserted within 1 s");
                    }
                }
            }
            break;
        case LASER_CONTROLLER_DEPLOYMENT_STEP_READY_POSTURE:
            if (context->deployment_substep == 0U) {
                /*
                 * 2026-04-16 user directive: PCN stays LOW throughout the
                 * checklist. Substep 0 verifies LPGD locks at LISL idle
                 * bias (driver in OPERATE = SBDN HIGH, PCN LOW, no PWM).
                 * The previous LISH-driven check was redundant with the
                 * preceding LP_GOOD_CHECK step.
                 */
                if (context->last_inputs.ld_rail_pgood &&
                    context->last_inputs.tec_rail_pgood &&
                    context->last_inputs.driver_loop_good &&
                    laser_controller_deployment_pin_is_high(
                        &context->last_inputs,
                        LASER_CONTROLLER_GPIO_LD_SBDN) &&
                    laser_controller_deployment_pin_is_low(
                        &context->last_inputs,
                        LASER_CONTROLLER_GPIO_LD_PCN) &&
                    !context->last_inputs.pcn_pwm_active) {
                    context->deployment_substep = 1U;
                    context->deployment_phase_started_ms = now_ms;
                } else if ((now_ms - context->deployment_phase_started_ms) >=
                           deployment_ready_wait_ms) {
                    laser_controller_deployment_fail(
                        context,
                        now_ms,
                        LASER_CONTROLLER_FAULT_LD_LOOP_BAD,
                        "deployment ready test could not validate LPGD at LISL idle");
                }
            } else if (laser_controller_deployment_ready_posture_confirmed(context)) {
                laser_controller_deployment_complete_current_step(context, now_ms);
                laser_controller_logger_log(
                    now_ms,
                    "deploy",
                    "deployment sequence complete; runtime control unlocked");
            } else if ((now_ms - context->deployment_step_started_ms) >=
                       deployment_ready_wait_ms) {
                laser_controller_deployment_fail(
                    context,
                    now_ms,
                    LASER_CONTROLLER_FAULT_LD_LOOP_BAD,
                    "deployment ready posture was not confirmed");
            }
            break;
        default:
            break;
    }
}

static uint8_t laser_controller_expected_pd_profile_count(
    const laser_controller_service_pd_profile_t *profiles)
{
    uint8_t count = 1U;

    if (profiles == NULL) {
        return 0U;
    }

    if (profiles[1].enabled) {
        count = 2U;
    }
    if (profiles[1].enabled && profiles[2].enabled) {
        count = 3U;
    }

    return count;
}

static bool laser_controller_pd_profiles_match(
    const laser_controller_service_pd_profile_t *expected_profiles,
    const laser_controller_service_pd_profile_t *actual_profiles,
    uint8_t actual_profile_count)
{
    if (expected_profiles == NULL || actual_profiles == NULL) {
        return false;
    }

    if (actual_profile_count !=
        laser_controller_expected_pd_profile_count(expected_profiles)) {
        return false;
    }

    for (size_t index = 0U;
         index < LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT;
         ++index) {
        const bool expected_enabled = expected_profiles[index].enabled;
        const bool actual_enabled = actual_profiles[index].enabled;

        if (expected_enabled != actual_enabled) {
            return false;
        }

        if (!expected_enabled) {
            continue;
        }

        if (fabsf(expected_profiles[index].voltage_v - actual_profiles[index].voltage_v) >
                LASER_CONTROLLER_PD_COMPARE_VOLTAGE_EPS_V ||
            fabsf(expected_profiles[index].current_a - actual_profiles[index].current_a) >
                LASER_CONTROLLER_PD_COMPARE_CURRENT_EPS_A) {
            return false;
        }
    }

    return true;
}

static void laser_controller_maybe_reconcile_pd_runtime(
    laser_controller_context_t *context,
    laser_controller_time_ms_t now_ms)
{
    laser_controller_power_policy_t stored_power_policy = { 0 };
    laser_controller_service_pd_profile_t
        stored_profiles[LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT] = { 0 };
    bool firmware_plan_enabled = false;

    if (context == NULL ||
        context->next_pd_reconcile_ms == 0U ||
        now_ms < context->next_pd_reconcile_ms ||
        !laser_controller_service_module_expected(LASER_CONTROLLER_MODULE_PD)) {
        return;
    }

    context->next_pd_reconcile_ms = 0U;

    laser_controller_service_get_pd_config(
        &stored_power_policy,
        stored_profiles,
        LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT,
        &firmware_plan_enabled);

    if (!firmware_plan_enabled) {
        return;
    }

    if (context->last_inputs.pd_last_updated_ms == 0U ||
        context->last_inputs.pd_source == LASER_CONTROLLER_PD_SNAPSHOT_SOURCE_NONE ||
        ((!context->last_inputs.pd_contract_valid &&
          !context->last_inputs.pd_source_is_host_only) ||
         context->last_inputs.pd_sink_profile_count == 0U)) {
        return;
    }

    if (laser_controller_pd_profiles_match(
            stored_profiles,
            context->last_inputs.pd_sink_profiles,
            context->last_inputs.pd_sink_profile_count)) {
        return;
    }

    /*
     * 2026-04-16 user directive: NO auto-writes to the STUSB4500 PD
     * chip. Any PDO write triggers a soft-reset / renegotiation that
     * momentarily drops USB-C power — completely unwanted as a silent
     * background side-effect. The operator must explicitly re-apply
     * the saved plan via the bringup workbench (with confirmation).
     *
     * The mismatch is logged so the operator can see in History that
     * a re-apply MIGHT be needed; they decide whether and when to act.
     * `apply_saved_pd_runtime` is no longer called from this path.
     */
    laser_controller_logger_log(
        now_ms,
        "pd",
        "saved PDO plan differs from live STUSB runtime state; auto-reconcile DISABLED — operator must explicitly re-apply via Integrate page if a change is intended");
}

static bool laser_controller_update_hold_filter(
    laser_controller_hold_filter_t *filter,
    bool raw_state,
    uint32_t hold_ms,
    laser_controller_time_ms_t now_ms)
{
    if (filter == NULL) {
        return raw_state;
    }

    if (raw_state == filter->state) {
        filter->pending_valid = false;
        filter->pending_state = raw_state;
        filter->pending_since_ms = 0U;
        return filter->state;
    }

    if (!filter->pending_valid || filter->pending_state != raw_state) {
        filter->pending_valid = true;
        filter->pending_state = raw_state;
        filter->pending_since_ms = now_ms;
        return filter->state;
    }

    if ((now_ms - filter->pending_since_ms) >= hold_ms) {
        filter->state = raw_state;
        filter->pending_valid = false;
        filter->pending_since_ms = 0U;
    }

    return filter->state;
}

static bool laser_controller_rail_timeout_expired(
    laser_controller_rail_watch_t *watch,
    bool rail_enabled,
    bool rail_pgood,
    uint32_t timeout_ms,
    laser_controller_time_ms_t now_ms)
{
    if (watch == NULL || !rail_enabled || rail_pgood || timeout_ms == 0U) {
        if (watch != NULL) {
            watch->waiting = false;
            watch->pending_since_ms = 0U;
        }
        return false;
    }

    if (!watch->waiting) {
        watch->waiting = true;
        watch->pending_since_ms = now_ms;
        return false;
    }

    return (now_ms - watch->pending_since_ms) >= timeout_ms;
}

static void laser_controller_record_fault(
    laser_controller_context_t *context,
    laser_controller_time_ms_t now_ms,
    laser_controller_fault_code_t fault_code,
    laser_controller_fault_class_t fault_class,
    const char *reason)
{
    if (fault_code == LASER_CONTROLLER_FAULT_NONE) {
        return;
    }

    if (fault_code != context->active_fault_code ||
        fault_class != context->active_fault_class) {
        context->active_fault_count++;
        context->trip_counter++;
        context->last_fault_ms = now_ms;
        laser_controller_logger_logf(
            now_ms,
            "fault",
            "%s (%s): %s",
            laser_controller_fault_code_name(fault_code),
            laser_controller_fault_class_name(fault_class),
            reason != NULL ? reason : "");
    }

    context->active_fault_code = fault_code;
    context->active_fault_class = fault_class;
    /*
     * Always copy the active reason so polling host gets the latest
     * detail. snprintf truncates safely if reason is longer than the
     * buffer; null reason → empty string.
     */
    if (reason != NULL) {
        (void)snprintf(
            context->active_fault_reason,
            sizeof(context->active_fault_reason),
            "%s",
            reason);
    } else {
        context->active_fault_reason[0] = '\0';
    }

    if (fault_class != LASER_CONTROLLER_FAULT_CLASS_INTERLOCK_AUTO_CLEAR) {
        context->latched_fault_code = fault_code;
        context->latched_fault_class = fault_class;
        context->fault_latched = true;
        /* Mirror reason into the latched slot too. */
        if (reason != NULL) {
            (void)snprintf(
                context->latched_fault_reason,
                sizeof(context->latched_fault_reason),
                "%s",
                reason);
        } else {
            context->latched_fault_reason[0] = '\0';
        }
        /*
         * Any non-auto-clear fault auto-disables the USB-Debug Mock if it
         * was active. Idempotent if mock is already off. Skipped for the
         * mock's own PD-conflict fault to avoid a redundant log line —
         * the mock already auto-disabled itself in the same tick before
         * recording the fault.
         */
        if (fault_code != LASER_CONTROLLER_FAULT_USB_DEBUG_MOCK_PD_CONFLICT) {
            laser_controller_usb_debug_mock_force_disable_on_fault(
                laser_controller_fault_code_name(fault_code),
                now_ms);
        }
    } else if (!laser_controller_fault_latch_active(context)) {
        context->fault_latched = false;
    }
}

static void laser_controller_latch_fault_if_needed(
    laser_controller_context_t *context,
    laser_controller_time_ms_t now_ms,
    const laser_controller_safety_decision_t *decision)
{
    if (decision->fault_present) {
        laser_controller_record_fault(
            context,
            now_ms,
            decision->fault_code,
            decision->fault_class,
            decision->fault_reason);
        return;
    }

    if (context->active_fault_code != LASER_CONTROLLER_FAULT_NONE) {
        laser_controller_logger_logf(
            now_ms,
            "fault",
            "active fault condition cleared: %s",
            laser_controller_fault_code_name(context->active_fault_code));
        context->active_fault_code = LASER_CONTROLLER_FAULT_NONE;
        context->active_fault_class = LASER_CONTROLLER_FAULT_CLASS_NONE;
        context->active_fault_reason[0] = '\0';
    }

    context->fault_latched = laser_controller_fault_latch_active(context);
}

static void laser_controller_publish_runtime_status(
    const laser_controller_context_t *context,
    laser_controller_time_ms_t now_ms)
{
    laser_controller_config_t config_snapshot;

    portENTER_CRITICAL(&s_context_lock);
    config_snapshot = context->config;
    portEXIT_CRITICAL(&s_context_lock);

    portENTER_CRITICAL(&s_runtime_status_lock);
    s_runtime_status.started = context->started;
    s_runtime_status.boot_complete = context->boot_complete;
    s_runtime_status.config_valid = context->config_valid;
    s_runtime_status.fault_latched = context->fault_latched;
    s_runtime_status.uptime_ms = now_ms;
    s_runtime_status.state = context->state_machine.current;
    s_runtime_status.power_tier = context->power_tier;
    s_runtime_status.active_fault_code = context->active_fault_code;
    s_runtime_status.active_fault_class = context->active_fault_class;
    s_runtime_status.latched_fault_code = context->latched_fault_code;
    s_runtime_status.latched_fault_class = context->latched_fault_class;
    /*
     * Mirror reason strings into the published status so comms.c can
     * include them in JSON. memcpy because both buffers are the same
     * fixed size; fault reason buffers are always null-terminated by
     * record_fault. (2026-04-16)
     */
    memcpy(s_runtime_status.active_fault_reason,
           context->active_fault_reason,
           sizeof(s_runtime_status.active_fault_reason));
    memcpy(s_runtime_status.latched_fault_reason,
           context->latched_fault_reason,
           sizeof(s_runtime_status.latched_fault_reason));
    s_runtime_status.active_fault_count = context->active_fault_count;
    s_runtime_status.trip_counter = context->trip_counter;
    s_runtime_status.last_fault_ms = context->last_fault_ms;
    s_runtime_status.inputs = context->last_inputs;
    s_runtime_status.outputs = context->last_outputs;
    s_runtime_status.decision = context->last_decision;
    s_runtime_status.config = config_snapshot;
    s_runtime_status.deployment = context->deployment;
    s_runtime_status.button_runtime.nir_lockout = context->button_nir_lockout;
    s_runtime_status.button_runtime.led_brightness_pct =
        context->button_led_brightness_pct;
    s_runtime_status.button_runtime.led_owned =
        context->button_led_active || context->button_led_initialized;
    s_runtime_status.button_runtime.rgb_test_active =
        context->rgb_test_until_ms != 0U && now_ms <= context->rgb_test_until_ms;
    /*
     * Mirror the diagnostic block-reason set by apply_button_board_policy.
     * Always null-terminated by the policy (snprintf). (2026-04-16)
     */
    memcpy(s_runtime_status.button_runtime.nir_block_reason,
           context->nir_button_block_reason,
           sizeof(s_runtime_status.button_runtime.nir_block_reason));
    /*
     * Mirror the frozen at-trip diagnostic frame for the comms-side fault
     * JSON writer. `valid == false` means "no diag captured" and the host
     * will render `triggerDiag: null`.
     */
    s_runtime_status.active_fault_diag.valid =
        context->active_fault_diag.valid;
    s_runtime_status.active_fault_diag.code =
        context->active_fault_diag.code;
    s_runtime_status.active_fault_diag.measured_c =
        context->active_fault_diag.measured_c;
    s_runtime_status.active_fault_diag.measured_voltage_v =
        context->active_fault_diag.measured_voltage_v;
    s_runtime_status.active_fault_diag.limit_c =
        context->active_fault_diag.limit_c;
    s_runtime_status.active_fault_diag.ld_pgood_for_ms =
        context->active_fault_diag.ld_pgood_for_ms;
    s_runtime_status.active_fault_diag.sbdn_not_off_for_ms =
        context->active_fault_diag.sbdn_not_off_for_ms;
    memcpy(s_runtime_status.active_fault_diag.expr,
           context->active_fault_diag.expr,
           sizeof(s_runtime_status.active_fault_diag.expr));
    portEXIT_CRITICAL(&s_runtime_status_lock);
}

static laser_controller_board_outputs_t laser_controller_derive_outputs(
    const laser_controller_context_t *context,
    const laser_controller_safety_decision_t *decision)
{
    const bool interlocks_disabled =
        laser_controller_service_interlocks_disabled();
    laser_controller_board_outputs_t outputs =
        laser_controller_safe_outputs();
    /*
     * GREEN ALIGNMENT — NO SOFTWARE INTERLOCK (user directive 2026-04-14).
     * `alignment_output_enable` is set in safety.c to mirror the operator
     * request unconditionally. Every path below that is reachable after
     * boot-complete honors that request; only the pre-boot / pre-config path
     * keeps green off, because the GPIO peripheral has not been configured
     * and driving the pin is not even physically possible yet.
     */
    const bool green_request_after_boot = decision->alignment_output_enable;

    if (!context->boot_complete || !context->config_valid) {
        return outputs;
    }

    /*
     * Hard-fault override (added 2026-04-14 alongside USB-Debug Mock).
     *
     * If a SYSTEM_MAJOR fault is present OR latched, force safe outputs
     * immediately and DO NOT take any deployment-step branch below. The
     * deployment running paths previously force `sbdn_state = ON` during
     * the READY_POSTURE substep, which would race with a same-tick
     * SYSTEM_MAJOR fault (e.g. LD_RAIL_BAD, TEC_RAIL_BAD).
     *
     * Two conditions, because:
     *  - `decision.fault_present` is set by safety_evaluate when a fault
     *    is detected during this tick (rail timeouts, etc.).
     *  - `context.fault_latched` is set by `record_fault` BEFORE
     *    safety_evaluate runs (e.g. USB_DEBUG_MOCK_PD_CONFLICT recorded
     *    in run_fast_cycle right after mock_tick). On that tick,
     *    safety_evaluate's fault_latched short-circuit returns WITHOUT
     *    setting `decision.fault_present`, so checking only the decision
     *    field would miss the same-tick latch. The latched-class check
     *    closes that gap (Agent B1 audit 2026-04-14).
     *
     * Green follows request even through hard faults, per user directive.
     */
    const bool latched_system_major =
        context != NULL && context->fault_latched &&
        context->latched_fault_class ==
            LASER_CONTROLLER_FAULT_CLASS_SYSTEM_MAJOR;
    const bool decision_system_major =
        decision != NULL && decision->fault_present &&
        decision->fault_class == LASER_CONTROLLER_FAULT_CLASS_SYSTEM_MAJOR;
    if (decision_system_major || latched_system_major) {
        outputs.enable_alignment_laser = green_request_after_boot;
        outputs.sbdn_state = LASER_CONTROLLER_SBDN_STATE_OFF;
        return outputs;
    }

    if (context->state_machine.current == LASER_CONTROLLER_STATE_SERVICE_MODE) {
        laser_controller_service_get_service_output_requests(
            &outputs.enable_ld_vin,
            &outputs.enable_tec_vin,
            &outputs.enable_haptic_driver);
        outputs.enable_alignment_laser = green_request_after_boot;
        return outputs;
    }

    if (context->deployment.active) {
        if (context->deployment.running) {
            switch (context->deployment.current_step) {
                case LASER_CONTROLLER_DEPLOYMENT_STEP_RAIL_SEQUENCE:
                    outputs.enable_tec_vin = true;
                    outputs.enable_ld_vin =
                        context->deployment_substep >=
                        LASER_CONTROLLER_DEPLOYMENT_RAIL_PHASE_LD;
                    outputs.enable_alignment_laser = green_request_after_boot;
                    return outputs;
                case LASER_CONTROLLER_DEPLOYMENT_STEP_TEC_SETTLE:
                    outputs.enable_tec_vin = true;
                    outputs.enable_ld_vin = true;
                    outputs.enable_alignment_laser = green_request_after_boot;
                    return outputs;
                case LASER_CONTROLLER_DEPLOYMENT_STEP_LP_GOOD_CHECK:
                    /*
                     * Three-phase loop-lock verification. Substep
                     * stability windows were reduced to 200 ms per
                     * user directive 2026-04-16 ("intentional delays
                     * ≤ 200 ms"). Each window is still polled every
                     * 5 ms, so the step advances on the first tick
                     * that sees the condition hold at least 200 ms.
                     *
                     *   substep 0: SBDN = OFF. Wait for the LD rail
                     *     to be PGOOD for >= 200 ms (fail at 5 s).
                     *     The `deployment_tick` switch advances to
                     *     substep 1 when `ld_rail_pgood_for_ms >= 200`.
                     *
                     *   substep 1 / 2: SBDN = ON (OPERATE). `deployment_
                     *     tick` waits 200 ms for `sbdn_not_off_for_ms`
                     *     (substep 1, fail at 5 s), then probes
                     *     LP_GOOD within 1 s (substep 2, fail with
                     *     LD_LP_GOOD_TIMEOUT).
                     *
                     * select_driver_low_current stays true throughout so
                     * PCN is LOW — this is a loop-lock-at-zero-drive
                     * check. READY_POSTURE still does the bias
                     * verification afterward.
                     */
                    outputs.enable_tec_vin = true;
                    outputs.enable_ld_vin = true;
                    outputs.sbdn_state =
                        context->deployment_substep == 0U
                            ? LASER_CONTROLLER_SBDN_STATE_OFF
                            : LASER_CONTROLLER_SBDN_STATE_ON;
                    outputs.select_driver_low_current = true;
                    outputs.enable_alignment_laser = green_request_after_boot;
                    return outputs;
                case LASER_CONTROLLER_DEPLOYMENT_STEP_READY_POSTURE:
                    outputs.enable_tec_vin = true;
                    outputs.enable_ld_vin = true;
                    /*
                     * Arm SBDN high so the driver enters OPERATE mode.
                     * 2026-04-16 user directive: PCN stays LOW (LISL idle
                     * bias) throughout the entire checklist — including
                     * READY_POSTURE. The earlier "verify LPGD at LISH"
                     * substep-0 path was removed; LP_GOOD_CHECK already
                     * verifies LPGD at LISL, and substep 1 here verifies
                     * the actual idle-bias current value via the existing
                     * `ready_posture_confirmed` checks.
                     */
                    outputs.sbdn_state = LASER_CONTROLLER_SBDN_STATE_ON;
                    outputs.select_driver_low_current = true;
                    outputs.enable_alignment_laser = green_request_after_boot;
                    return outputs;
                default:
                    outputs.enable_alignment_laser = green_request_after_boot;
                    return outputs;
            }
        }

        if (context->deployment_force_ld_safe &&
            context->deployment.ready &&
            !context->deployment.running) {
            outputs.enable_tec_vin = true;
            outputs.enable_ld_vin = false;
            outputs.enable_alignment_laser = green_request_after_boot;
            /* Force-safe: fast shutdown (drive LOW). Not Hi-Z. */
            outputs.sbdn_state = LASER_CONTROLLER_SBDN_STATE_OFF;
            outputs.select_driver_low_current = true;
            return outputs;
        }

        if (context->deployment.failed &&
            context->deployment.last_completed_step ==
                LASER_CONTROLLER_DEPLOYMENT_STEP_READY_POSTURE) {
            /*
             * Once deployment has already reached powered-ready posture, a later
             * invalidation must drop the LD path safe immediately without
             * reflexively turning TEC off as well. This preserves the
             * TEC-first / LD-safe policy and avoids collapsing the TEC rail on
             * transient post-ready faults. Green stays ungated.
             *
             * SBDN must be driven LOW (OFF) — the 20us fast-shutdown path.
             * Standby (Hi-Z) is not acceptable here because a post-ready
             * invalidation is a fault event.
             */
            outputs.enable_tec_vin = true;
            outputs.enable_ld_vin = false;
            outputs.enable_alignment_laser = green_request_after_boot;
            outputs.sbdn_state = LASER_CONTROLLER_SBDN_STATE_OFF;
            outputs.select_driver_low_current = true;
            return outputs;
        }

        if (!context->deployment.ready || context->deployment.failed) {
            outputs.enable_alignment_laser = green_request_after_boot;
            return outputs;
        }

        outputs.enable_tec_vin = true;
        outputs.enable_ld_vin = true;
        outputs.enable_alignment_laser = green_request_after_boot;
        /*
         * Defer to the safety decision for SBDN posture in the ready path.
         * safety.c sets sbdn_state = ON when NIR is requested and allowed,
         * and STANDBY (Hi-Z) when NIR is off — that is the documented
         * operator-facing semantics (datasheet p.2-5 and user directive
         * 2026-04-14).
         */
        outputs.sbdn_state = decision->sbdn_state;
        outputs.select_driver_low_current = !decision->nir_output_enable;
        if (decision->fault_present &&
            decision->fault_class == LASER_CONTROLLER_FAULT_CLASS_SYSTEM_MAJOR) {
            outputs.enable_ld_vin = false;
            outputs.enable_tec_vin = false;
            /* Fault → fast shutdown (drive LOW). Standby is not the
             * safe-on-fault posture — see hardware-safety audit. */
            outputs.sbdn_state = LASER_CONTROLLER_SBDN_STATE_OFF;
            /* Green follows request even through SYSTEM_MAJOR faults. */
        }
        return outputs;
    }

    if ((!interlocks_disabled && laser_controller_fault_latch_active(context)) ||
        laser_controller_service_mode_requested()) {
        outputs.enable_alignment_laser = green_request_after_boot;
        return outputs;
    }

    outputs.enable_alignment_laser = green_request_after_boot;
    return outputs;
}

static laser_controller_state_t laser_controller_derive_state(
    const laser_controller_context_t *context,
    const laser_controller_safety_decision_t *decision)
{
    const bool interlocks_disabled =
        laser_controller_service_interlocks_disabled();

    if (laser_controller_service_mode_requested()) {
        return LASER_CONTROLLER_STATE_SERVICE_MODE;
    }

    if (laser_controller_fault_latch_active(context) && !interlocks_disabled) {
        return LASER_CONTROLLER_STATE_FAULT_LATCHED;
    }

    if (context->deployment.active &&
        (!context->deployment.ready || context->deployment.running)) {
        return laser_controller_power_tier_is_operational(context->power_tier) ?
            LASER_CONTROLLER_STATE_SAFE_IDLE :
            LASER_CONTROLLER_STATE_PROGRAMMING_ONLY;
    }

    switch (context->power_tier) {
        case LASER_CONTROLLER_POWER_TIER_PROGRAMMING_ONLY:
        case LASER_CONTROLLER_POWER_TIER_INSUFFICIENT:
            return LASER_CONTROLLER_STATE_PROGRAMMING_ONLY;
        case LASER_CONTROLLER_POWER_TIER_UNKNOWN:
            return LASER_CONTROLLER_STATE_POWER_NEGOTIATION;
        case LASER_CONTROLLER_POWER_TIER_REDUCED:
            if (decision->alignment_output_enable) {
                return LASER_CONTROLLER_STATE_ALIGNMENT_ACTIVE;
            }
            if (decision->allow_alignment) {
                return LASER_CONTROLLER_STATE_READY_ALIGNMENT;
            }
            return LASER_CONTROLLER_STATE_LIMITED_POWER_IDLE;
        case LASER_CONTROLLER_POWER_TIER_FULL:
            if (interlocks_disabled) {
                if (decision->nir_output_enable) {
                    return LASER_CONTROLLER_STATE_NIR_ACTIVE;
                }
                if (decision->alignment_output_enable) {
                    return LASER_CONTROLLER_STATE_ALIGNMENT_ACTIVE;
                }
                if (decision->allow_nir) {
                    return LASER_CONTROLLER_STATE_READY_NIR;
                }
                if (decision->allow_alignment) {
                    return LASER_CONTROLLER_STATE_READY_ALIGNMENT;
                }
                return LASER_CONTROLLER_STATE_SAFE_IDLE;
            }
            if (!context->last_inputs.tec_rail_pgood) {
                return LASER_CONTROLLER_STATE_TEC_WARMUP;
            }
            if (!context->last_inputs.tec_temp_good) {
                if (decision->alignment_output_enable) {
                    return LASER_CONTROLLER_STATE_ALIGNMENT_ACTIVE;
                }
                return LASER_CONTROLLER_STATE_TEC_SETTLING;
            }
            if (decision->nir_output_enable) {
                return LASER_CONTROLLER_STATE_NIR_ACTIVE;
            }
            if (decision->alignment_output_enable) {
                return LASER_CONTROLLER_STATE_ALIGNMENT_ACTIVE;
            }
            if (decision->allow_nir) {
                return LASER_CONTROLLER_STATE_READY_NIR;
            }
            if (decision->allow_alignment) {
                return LASER_CONTROLLER_STATE_READY_ALIGNMENT;
            }
            return LASER_CONTROLLER_STATE_SAFE_IDLE;
        default:
            return LASER_CONTROLLER_STATE_SAFE_IDLE;
    }
}

static void laser_controller_log_state_if_changed(
    laser_controller_context_t *context,
    laser_controller_state_t next_state,
    laser_controller_time_ms_t now_ms)
{
    if (next_state == context->state_machine.current) {
        return;
    }

    if (!laser_controller_state_transition(
            &context->state_machine,
            next_state,
            now_ms,
            laser_controller_effective_fault_code(context))) {
        /*
         * Format the actual blocked from→to so the operator sees a
         * useful detail in the GUI fault banner instead of just
         * "illegal state transition blocked". (2026-04-16)
         */
        char detail[80];
        (void)snprintf(
            detail,
            sizeof(detail),
            "illegal state transition %s -> %s blocked",
            laser_controller_state_name(context->state_machine.current),
            laser_controller_state_name(next_state));
        laser_controller_record_fault(
            context,
            now_ms,
            LASER_CONTROLLER_FAULT_UNEXPECTED_STATE,
            LASER_CONTROLLER_FAULT_CLASS_SAFETY_LATCHED,
            detail);
        (void)laser_controller_state_transition(
            &context->state_machine,
            LASER_CONTROLLER_STATE_FAULT_LATCHED,
            now_ms,
            laser_controller_effective_fault_code(context));
        return;
    }

    laser_controller_logger_logf(
        now_ms,
        "state",
        "%s -> %s",
        laser_controller_state_name(context->state_machine.previous),
        laser_controller_state_name(context->state_machine.current));
}

/*
 * Button-board policy — runs on the control task after safety_evaluate
 * and the SYSTEM_MAJOR overrides. Updates:
 *   - context->button_nir_lockout: press-and-hold lockout state
 *   - context->button_led_brightness_pct: stepped by side button edges
 *   - context->button_led_active: TRUE when binary_trigger should drive LED
 *   - context->last_outputs.rgb_led: RGB status color/brightness/blink
 *
 * Also raises FAULT_BUTTON_BOARD_I2C_LOST when binary_trigger is the
 * active runtime mode and the MCP23017 has been declared unreachable.
 *
 * Threading: control-task only. Same ownership as fault writes.
 */
static void laser_controller_apply_button_board_policy(
    laser_controller_context_t *context,
    laser_controller_safety_decision_t *decision,
    const laser_controller_bench_status_t *bench_status,
    laser_controller_time_ms_t now_ms)
{
    if (context == NULL || decision == NULL || bench_status == NULL) {
        return;
    }

    const laser_controller_button_state_t *btn = &context->last_inputs.button;
    /*
     * Buttons are effective whenever deployment is ready_idle and the
     * board is reachable. Decoupled from runtime_mode toggle (2026-04-17
     * user directive): main button is the primary post-deployment trigger
     * regardless of which Operate-page label the operator sees. Host
     * `operate.set_output` still gated by runtime_mode == MODULATED_HOST.
     */
    const bool deployment_ready_idle =
        context->deployment.active &&
        context->deployment.ready &&
        context->deployment.ready_idle &&
        !context->deployment.running;
    const bool button_board_present = btn->board_reachable;
    const bool buttons_effective =
        deployment_ready_idle && button_board_present;
    const bool service_mode_active_now =
        laser_controller_service_mode_requested();

    /*
     * Service-mode short-circuit. In service mode there is "no safety at
     * all" (2026-04-17 user directive). Buttons do not drive runtime
     * outputs, brightness stepping is skipped, lockout cleared. The RGB
     * branch below renders a distinctive yellow so the operator
     * visually confirms the privileged state.
     */
    if (service_mode_active_now) {
        context->button_led_active = false;
        context->button_nir_lockout = false;
        /* Skip arming logic — service mode neither sets nor clears it. */
    } else {
        /*
         * LED arming: latch on first ready_idle, hold through faults.
         * Cleared only on `exit_deployment_mode` (deployment.active=false)
         * so the front LED keeps illuminating the work area through any
         * interlock/safety latch that might transiently flip ready off.
         */
        if (deployment_ready_idle && !context->deployment_led_armed) {
            context->deployment_led_armed = true;
        }
        if (!context->deployment.active) {
            context->deployment_led_armed = false;
            context->button_led_initialized = false;
        }

        /*
         * Press-and-hold lockout policy (revised 2026-04-16).
         *
         * Latch ONLY for SAFETY_LATCHED / SYSTEM_MAJOR faults — those
         * require explicit `clear_faults` to recover, so locking the
         * trigger until full release is appropriate.
         *
         * INTERLOCK_AUTO_CLEAR faults (tof_out_of_range, tof_invalid,
         * imu_*, lambda_drift, tec_temp_adc_high, ld_loop_bad) do NOT
         * latch the lockout. They block NIR for the tick(s) they fire
         * (allow_nir=false in safety.c), but as soon as the underlying
         * condition resolves the same in-progress press is allowed to
         * fire NIR. Previously this latched too — which trapped the
         * operator in an infinite "release-and-retry, fault-re-fires,
         * lockout-re-latches" cycle whenever an interlock condition
         * was even momentarily true.
         */
        const bool fault_locks_trigger =
            decision->fault_present &&
            decision->fault_class !=
                LASER_CONTROLLER_FAULT_CLASS_INTERLOCK_AUTO_CLEAR;

        if (buttons_effective) {
            if (fault_locks_trigger &&
                (btn->stage1_pressed || btn->stage2_pressed)) {
                context->button_nir_lockout = true;
            }
            if (!btn->stage1_pressed && !btn->stage2_pressed) {
                context->button_nir_lockout = false;
            }
        } else {
            context->button_nir_lockout = false;
        }

        /*
         * LED-brightness stepping. side1 = +5%, side2 = -5%. The first
         * tick of deployment_ready_idle seeds the value to 20% via
         * `button_led_initialized`; from then on, only side buttons and
         * the GUI slider (via `app_set_button_led_brightness`) modify
         * the brightness. Stage1 / stage2 do NOT touch brightness — the
         * old stage1_edge=20% reset was leftover from the pre-armed
         * design and was destroying user-set brightness on every NIR
         * trigger (Bug fix 2026-04-17).
         */
        if (buttons_effective) {
            if (!context->button_led_initialized) {
                context->button_led_brightness_pct = 20U;
                context->button_led_initialized = true;
            }
            if (btn->side1_edge) {
                uint32_t next = context->button_led_brightness_pct + 5U;
                if (next > 100U) {
                    next = 100U;
                }
                context->button_led_brightness_pct = next;
            }
            if (btn->side2_edge) {
                uint32_t cur = context->button_led_brightness_pct;
                context->button_led_brightness_pct = cur >= 5U ? cur - 5U : 0U;
            }
            /*
             * `button_led_active` was previously the only LED-on flag; now
             * the LED stays on whenever `deployment_led_armed`, so keep
             * this field for compat (it indicates "stage1 currently held"
             * — used by the RGB armed-color check below).
             */
            context->button_led_active = btn->stage1_pressed;
        } else {
            context->button_led_active = false;
        }
    }

    /*
     * MCP23017 lost while deployment is active → SYSTEM_MAJOR fault.
     * Buttons are now the primary trigger surface (Bug 2 fix
     * 2026-04-17), so the fault fires regardless of runtime_mode. Skipped
     * in service mode (no safety per directive).
     */
    if (!service_mode_active_now &&
        context->deployment.active &&
        !btn->board_reachable &&
        !context->fault_latched) {
        laser_controller_record_fault(
            context,
            now_ms,
            LASER_CONTROLLER_FAULT_BUTTON_BOARD_I2C_LOST,
            LASER_CONTROLLER_FAULT_CLASS_SYSTEM_MAJOR,
            "button board MCP23017 unreachable while deployment active");
        decision->fault_present = true;
        decision->fault_code = LASER_CONTROLLER_FAULT_BUTTON_BOARD_I2C_LOST;
        decision->fault_class = LASER_CONTROLLER_FAULT_CLASS_SYSTEM_MAJOR;
        decision->fault_reason = "button board unreachable";
    }

    /*
     * RGB status policy. Computed in firmware so the host doesn't need to
     * mirror gate ordering. Priorities (highest first):
     *
     *   1. Integrate test override (service-mode-only, watchdog-bounded).
     *   2. Service mode → solid YELLOW.
     *   3. SYSTEM_MAJOR latched / present, or button-board lost
     *      → flash RED (255, 0, 0). "Unrecoverable" per user spec.
     *   4. SAFETY_LATCHED or INTERLOCK_AUTO_CLEAR fault present, or button
     *      lockout active → flash ORANGE (255, 140, 0). "Recoverable".
     *   5. Deployment checklist running → alternate BLUE / GREEN each
     *      500 ms ("working on it" indication that is distinct from the
     *      solid-blue "ready, waiting for trigger" state). 2026-04-16.
     *   6. Outside deployment-ready-idle → off.
     *   7. NIR currently emitting → solid RED.
     *   8. stage1 + LD_GOOD + TEC_TEMP_GOOD → solid GREEN (armed).
     *   9. otherwise → solid BLUE (ready, awaiting trigger).
     */
    laser_controller_rgb_led_state_t rgb = {
        .r = 0U,
        .g = 0U,
        .b = 0U,
        .blink = false,
        .enabled = false,
    };

    if (context->rgb_test_until_ms != 0U && now_ms <= context->rgb_test_until_ms) {
        rgb = context->rgb_test_state;
    } else if (service_mode_active_now) {
        /*
         * Service mode = full sudo. RGB shows distinctive yellow so the
         * operator visually confirms privileged state and pre-existing
         * operate-mode faults do NOT bleed through as flashing orange
         * (Bug 4 fix 2026-04-17). Solid, not blinking — service mode
         * is meant to be obvious.
         */
        rgb.r = 255U;
        rgb.g = 200U;
        rgb.b = 0U;
        rgb.blink = false;
        rgb.enabled = true;
    } else {
        if (context->rgb_test_until_ms != 0U && now_ms > context->rgb_test_until_ms) {
            context->rgb_test_until_ms = 0U;
        }

        const bool unrecoverable =
            (context->fault_latched &&
             context->latched_fault_class ==
                 LASER_CONTROLLER_FAULT_CLASS_SYSTEM_MAJOR) ||
            (decision->fault_present &&
             decision->fault_class ==
                 LASER_CONTROLLER_FAULT_CLASS_SYSTEM_MAJOR);
        const bool recoverable =
            !unrecoverable &&
            (context->fault_latched ||
             decision->fault_present ||
             context->button_nir_lockout);

        if (unrecoverable) {
            rgb.r = 255U;
            rgb.g = 0U;
            rgb.b = 0U;
            rgb.blink = true;
            rgb.enabled = true;
        } else if (recoverable) {
            /*
             * Orange = warm red + a touch of green to take the chill off.
             * Pre-RGB-scaling raw value bumped from g=80 to g=140 so that
             * after Phase C per-channel scaling (red 100%, green ~45%) the
             * effective LED color is a real orange, not a near-red.
             */
            rgb.r = 255U;
            rgb.g = 140U;
            rgb.b = 0U;
            rgb.blink = true;
            rgb.enabled = true;
        } else if (context->deployment.active && context->deployment.running) {
            /*
             * 2026-04-17 (field report): checklist-running used to
             * alternate blue↔green every 500 ms by swapping rgb.r/g/b
             * on every control-task tick. That approach had two
             * problems:
             *   - It depended on the 5 ms control tick actually
             *     re-applying the LED state on the exact tick
             *     boundary. Under WS-drop load the apply path skipped
             *     ticks and the LED looked "stuck" on one color
             *     (operator reported solid-blue-while-running).
             *   - Blue↔green is a harsh flash that reads as a fault
             *     warning rather than "working on it".
             *
             * Now: solid blue + HARDWARE BLINK. The TLC59116 driver
             * handles the 0.5 s on / 0.5 s off cadence natively
             * (GRPPWM + blink mode), so the LED keeps pulsing even if
             * the control task falls behind. Visually this reads as
             * "same color as ready, but busy" — a clean distinction
             * from solid-blue "ready to fire".
             */
            rgb.r = 0U;
            rgb.g = 0U;
            rgb.b = 255U;
            rgb.blink = true;
            rgb.enabled = true;
        } else if (!deployment_ready_idle) {
            /* Pre-deployment / pre-ready: LED dark. */
            rgb.enabled = false;
        } else if (decision->nir_output_enable) {
            rgb.r = 255U;
            rgb.g = 0U;
            rgb.b = 0U;
            rgb.blink = false;
            rgb.enabled = true;
        } else if (buttons_effective &&
                   btn->stage1_pressed &&
                   context->last_inputs.ld_rail_pgood &&
                   context->last_inputs.tec_temp_good) {
            rgb.r = 0U;
            rgb.g = 255U;
            rgb.b = 0U;
            rgb.blink = false;
            rgb.enabled = true;
        } else {
            /* Solid blue: ready for NIR, awaiting trigger. */
            rgb.r = 0U;
            rgb.g = 0U;
            rgb.b = 255U;
            rgb.blink = false;
            rgb.enabled = true;
        }
    }

    context->last_outputs.rgb_led = rgb;

    /*
     * Headless fault-recovery via side1+side2 hold (2026-04-16 user
     * feature). When the device is fault-latched and no GUI is
     * available to clear it, the operator holds both peripheral
     * buttons for 2 seconds. At the 2 s mark we RAISE a one-shot
     * request flag that `run_fast_cycle` consumes at end-of-tick.
     *
     * Not gated on host-activity — the user directive said "for no
     * gui operation" as context, but a fault latch disables the same
     * NIR gesture regardless, so there is no conflicting use of the
     * side buttons while a fault is latched.
     *
     * Board-unreachable guard: if the MCP23017 is missing we can't
     * trust any button bits, so we bail out immediately.
     */
    /*
     * Recovery gesture: side1 + side2 held for 2 s. PERIPHERAL
     * BUTTONS ONLY — the main trigger (stage1/stage2) is the NIR-fire
     * gesture and must stay single-purpose, so it is NEVER part of
     * this recovery path (2026-04-16 user directive).
     *
     * Tick-level logging is added at the moment both peripheral
     * buttons first register, and at every whole second of hold, so
     * the operator can diagnose whether the firmware is seeing their
     * press via the event log.
     */
    const bool recovery_gesture_held =
        !service_mode_active_now && btn->board_reachable &&
        btn->side1_pressed && btn->side2_pressed;

    if (recovery_gesture_held) {
        if (context->both_side_held_since_ms == 0U) {
            context->both_side_held_since_ms = now_ms == 0U ? 1U : now_ms;
            context->button_recovery_last_whole_sec = 0U;
            laser_controller_logger_logf(
                now_ms,
                "deploy",
                "headless recovery: side1+side2 hold detected, "
                "fault_latched=%d",
                (int)context->fault_latched);
        } else {
            const uint32_t held_ms =
                (uint32_t)(now_ms - context->both_side_held_since_ms);
            const uint32_t whole_sec = held_ms / 1000U;
            if (whole_sec > 0U &&
                whole_sec != context->button_recovery_last_whole_sec) {
                context->button_recovery_last_whole_sec = whole_sec;
                laser_controller_logger_logf(
                    now_ms,
                    "deploy",
                    "headless recovery: hold %lu s elapsed, fault_latched=%d",
                    (unsigned long)whole_sec,
                    (int)context->fault_latched);
            }
            /*
             * Recovery fires on EITHER `fault_latched` (a SAFETY /
             * SYSTEM_MAJOR fault that will not auto-clear) OR a
             * deployment-failed state (checklist bailed without
             * latching a persistent safety fault). Both are stuck
             * states that require an explicit reset from the
             * operator's point of view.
             */
            const bool recovery_needed =
                context->fault_latched || context->deployment.failed;
            if (!context->button_recovery_armed &&
                recovery_needed &&
                held_ms >= 2000U) {
                context->button_recovery_armed = true;
                context->button_recovery_requested = true;
                laser_controller_logger_logf(
                    now_ms,
                    "deploy",
                    "headless recovery: hold crossed 2 s "
                    "(fault_latched=%d deploy_failed=%d); queuing "
                    "clear + checklist restart",
                    (int)context->fault_latched,
                    (int)context->deployment.failed);
            }

            /*
             * Headless START-OF-DEPLOYMENT (2026-04-17 user feature).
             * Fires at 1 s when no host GUI is connected, no fault, no
             * deployment, no service mode — i.e. the operator wants to
             * arm the device from nothing but the peripheral buttons.
             * Distinct from the 2 s recovery path: this one does NOT
             * touch the fault latch and does NOT require `fault_latched`
             * — it assumes the device is idle and ready to deploy.
             *
             * "No GUI connected" = zero WS clients AND no USB comms
             * activity in the last 3 s. A USB cable that only delivers
             * power (headless bench cable, wall-wart-to-USB-C) is fine;
             * an actively-commanding USB host will update
             * `last_host_activity_ms` and suppress this trigger.
             */
            const bool host_inactive =
                !laser_controller_wireless_has_clients() &&
                (context->last_host_activity_ms == 0U ||
                 (now_ms - context->last_host_activity_ms) >= 3000U);
            const bool deploy_idle =
                !context->deployment.active &&
                !context->fault_latched &&
                !context->deployment.failed &&
                context->boot_complete &&
                context->state_machine.current !=
                    LASER_CONTROLLER_STATE_BOOT_INIT &&
                context->state_machine.current !=
                    LASER_CONTROLLER_STATE_SERVICE_MODE &&
                !service_mode_active_now;
            if (!context->button_deploy_armed &&
                deploy_idle &&
                host_inactive &&
                held_ms >= 1000U) {
                context->button_deploy_armed = true;
                context->button_deploy_requested = true;
                laser_controller_logger_log(
                    now_ms,
                    "deploy",
                    "headless deploy: side1+side2 hold crossed 1 s "
                    "with no GUI; queuing enter + checklist");
            }
        }
    } else {
        if (context->both_side_held_since_ms != 0U) {
            laser_controller_logger_log(
                now_ms,
                "deploy",
                "headless recovery: hold released before trigger");
        }
        context->both_side_held_since_ms = 0U;
        context->button_recovery_armed = false;
        context->button_deploy_armed = false;
        context->button_recovery_last_whole_sec = 0U;
    }

    /*
     * Auto-deploy-on-boot (2026-04-20 user directive). Fires the same
     * `enter_deployment_mode` + `run_deployment_sequence` path as the
     * headless button gesture, but from the AUTO_DEPLOY_ON_BOOT
     * service flag rather than a physical hold. Preconditions mirror
     * the button path (boot complete, config valid, no fault, not
     * already deploying, not in service mode) so operator overrides
     * (service mode entry, an active fault) always win. Armed exactly
     * once per power cycle — after that, `auto_deploy_armed` stays
     * true and the operator drives deployment with the normal
     * `deployment.enter` / `deployment.exit` commands.
     */
    if (context->auto_deploy_pending &&
        !context->auto_deploy_armed &&
        context->boot_complete &&
        context->config_valid &&
        !service_mode_active_now &&
        !context->fault_latched &&
        !context->deployment.active &&
        !context->deployment.failed &&
        context->state_machine.current !=
            LASER_CONTROLLER_STATE_BOOT_INIT &&
        context->state_machine.current !=
            LASER_CONTROLLER_STATE_SERVICE_MODE) {
        context->auto_deploy_armed = true;
        context->auto_deploy_requested = true;
        laser_controller_logger_log(
            now_ms,
            "deploy",
            "auto deploy: AUTO_DEPLOY_ON_BOOT service flag set; "
            "queuing enter + checklist");
    }

    /*
     * Diagnostic: WHY is button-driven NIR currently blocked? Computed
     * here in priority order (most-fundamental block first). Mirrored
     * to the host via runtime_status.button_runtime.nir_block_reason.
     * (2026-04-16 audit/refactor — surfaces gauntlet stage to operator
     * so debugging "stage 2 doesn't fire" doesn't require code reading.)
     */
    const char *reason = NULL;
    if (service_mode_active_now) {
        reason = "service-mode";
    } else if (!context->deployment.active) {
        reason = "deployment-not-active";
    } else if (context->deployment.running) {
        reason = "deployment-running";
    } else if (!context->deployment.ready) {
        reason = "deployment-not-ready";
    } else if (!context->deployment.ready_idle) {
        reason = "deployment-not-ready-idle";
    } else if (!btn->board_reachable) {
        reason = "button-board-unreachable";
    } else if (context->power_tier != LASER_CONTROLLER_POWER_TIER_FULL) {
        reason = "power-not-full";
    } else if (!context->last_inputs.ld_rail_pgood) {
        reason = "ld-rail-not-good";
    } else if (context->config.require_tec_for_nir &&
               (!context->last_inputs.tec_rail_pgood ||
                !context->last_inputs.tec_temp_good)) {
        reason = "tec-not-settled";
    } else if (decision->fault_present) {
        /* Format inline since we need the fault code name */
        (void)snprintf(
            context->nir_button_block_reason,
            sizeof(context->nir_button_block_reason),
            "fault: %s",
            laser_controller_fault_code_name(decision->fault_code));
        return;
    } else if (context->button_nir_lockout) {
        reason = "lockout-latched";
    } else if (!btn->stage1_pressed || !btn->stage2_pressed) {
        reason = "not-pressed";
    } else {
        reason = "none";
    }
    (void)snprintf(
        context->nir_button_block_reason,
        sizeof(context->nir_button_block_reason),
        "%s",
        reason);
}

/*
 * Service entry point used by comms.c integrate.rgb_led.set. Validates
 * gating (service mode + no deployment + no fault latch) and arms the
 * watchdog window. The control task picks up the override via the
 * context fields on the next tick.
 *
 * Threading: comms task → context fields are control-task-owned but a
 * single-word atomic-ish write is acceptable because:
 *   (a) we use a portENTER_CRITICAL around the writes,
 *   (b) the control task only READS these fields in the policy helper.
 */
esp_err_t laser_controller_app_set_rgb_test(
    uint8_t r, uint8_t g, uint8_t b, bool blink, uint32_t hold_ms)
{
    if (!s_context.started) {
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * Gate re-check. Read `s_context` fields directly under the lock
     * instead of calling `laser_controller_app_copy_status` — the full
     * status struct is ~10 KB and the comms RX task stack is 8 KB, so a
     * local copy here on top of the caller's local copy blows the stack
     * (2026-04-15 crash fix). The canonical gates are already enforced
     * at the comms-side handler; this is a belt-and-suspenders re-check
     * in case another caller (future) routes through without going
     * through comms.c.
     */
    portENTER_CRITICAL(&s_context_lock);
    const bool deployment_active = s_context.deployment.active;
    const bool fault_latched = s_context.fault_latched;
    portEXIT_CRITICAL(&s_context_lock);

    if (deployment_active || fault_latched) {
        return ESP_ERR_INVALID_STATE;
    }

    const laser_controller_time_ms_t now_ms = laser_controller_board_uptime_ms();
    const uint32_t bounded_hold = hold_ms == 0U ? 5000U : hold_ms;

    portENTER_CRITICAL(&s_context_lock);
    s_context.rgb_test_state.r = r;
    s_context.rgb_test_state.g = g;
    s_context.rgb_test_state.b = b;
    s_context.rgb_test_state.blink = blink;
    s_context.rgb_test_state.enabled = true;
    s_context.rgb_test_until_ms = now_ms + bounded_hold;
    portEXIT_CRITICAL(&s_context_lock);
    return ESP_OK;
}

esp_err_t laser_controller_app_clear_rgb_test(void)
{
    if (!s_context.started) {
        return ESP_ERR_INVALID_STATE;
    }
    portENTER_CRITICAL(&s_context_lock);
    s_context.rgb_test_state.enabled = false;
    s_context.rgb_test_until_ms = 0U;
    portEXIT_CRITICAL(&s_context_lock);
    return ESP_OK;
}

/*
 * Set the front-LED brightness used by the deployment-armed LED path AND
 * the side-button stepping (Bug 1 fix 2026-04-17). Comms-task entry; the
 * control task reads `button_led_brightness_pct` next tick. Clamp to
 * [0, 100] AND to the configured max_tof_led_duty_cycle_pct cap so this
 * setter cannot exceed the safety limit.
 *
 * The `button_led_initialized` flag is NOT touched: once deployment first
 * reaches ready_idle, the policy hits the 20% default exactly once; from
 * then on, both side buttons and this setter co-write the same field.
 */
esp_err_t laser_controller_app_set_button_led_brightness(uint32_t pct)
{
    if (!s_context.started) {
        return ESP_ERR_INVALID_STATE;
    }
    if (pct > 100U) {
        pct = 100U;
    }
    portENTER_CRITICAL(&s_context_lock);
    const uint32_t cap =
        s_context.config.thresholds.max_tof_led_duty_cycle_pct;
    portEXIT_CRITICAL(&s_context_lock);
    if (cap > 0U && pct > cap) {
        pct = cap;
    }
    portENTER_CRITICAL(&s_context_lock);
    s_context.button_led_brightness_pct = pct;
    portEXIT_CRITICAL(&s_context_lock);
    return ESP_OK;
}

/*
 * Comms calls this for every inbound command (see
 * `laser_controller_comms_receive_line`). Suppresses the 5-second
 * headless auto-deploy. Single-word atomic write under critical
 * section; never blocks. (2026-04-16 user feature.)
 */
void laser_controller_app_note_host_activity(void)
{
    if (!s_context.started) {
        return;
    }
    const laser_controller_time_ms_t now = laser_controller_board_uptime_ms();
    portENTER_CRITICAL(&s_context_lock);
    s_context.last_host_activity_ms = now > 0U ? now : 1U;
    portEXIT_CRITICAL(&s_context_lock);
}

static void laser_controller_run_fast_cycle(laser_controller_context_t *context)
{
    const laser_controller_time_ms_t now_ms = laser_controller_board_uptime_ms();
    laser_controller_config_t config_snapshot;
    laser_controller_bench_status_t bench_status;
    laser_controller_safety_snapshot_t snapshot;
    laser_controller_safety_decision_t decision;
    bool raw_lambda_drift_blocked;
    bool raw_tec_temp_adc_blocked;
    bool clear_fault_diag_requested = false;
    bool clear_fault_latch_requested = false;
    const laser_controller_power_tier_t previous_tier = context->power_tier;

    portENTER_CRITICAL(&s_context_lock);
    config_snapshot = context->config;
    clear_fault_diag_requested = context->clear_fault_diag_request;
    context->clear_fault_diag_request = false;
    clear_fault_latch_requested = context->clear_fault_latch_request;
    context->clear_fault_latch_request = false;
    portEXIT_CRITICAL(&s_context_lock);

    /*
     * Drain the cross-task `active_fault_diag` clear request from comms.
     * Control task is the sole writer of `active_fault_diag.*` — see the
     * struct docstring. Performing the zero-out here closes the data race
     * that would otherwise exist if comms wrote the fields directly.
     */
    if (clear_fault_diag_requested) {
        context->active_fault_diag.valid = false;
        context->active_fault_diag.code = LASER_CONTROLLER_FAULT_NONE;
        context->active_fault_diag.measured_c = 0.0f;
        context->active_fault_diag.measured_voltage_v = 0.0f;
        context->active_fault_diag.limit_c = 0.0f;
        context->active_fault_diag.ld_pgood_for_ms = 0U;
        context->active_fault_diag.sbdn_not_off_for_ms = 0U;
        context->active_fault_diag.expr[0] = '\0';
    }

    /*
     * Drain the cross-task fault-LATCH clear request from comms.
     * Control task is the sole writer of the fault-state fields — same
     * threading model as `record_fault`. The runtime-status lock is
     * still taken so the telemetry snapshot readers (comms, logger)
     * see a consistent tear-free fault payload across the reset.
     */
    if (clear_fault_latch_requested) {
        portENTER_CRITICAL(&s_runtime_status_lock);
        context->fault_latched = false;
        context->active_fault_code = LASER_CONTROLLER_FAULT_NONE;
        context->active_fault_class = LASER_CONTROLLER_FAULT_CLASS_NONE;
        context->latched_fault_code = LASER_CONTROLLER_FAULT_NONE;
        context->latched_fault_class = LASER_CONTROLLER_FAULT_CLASS_NONE;
        context->active_fault_reason[0] = '\0';
        context->latched_fault_reason[0] = '\0';
        context->last_fault_ms = 0U;
        portEXIT_CRITICAL(&s_runtime_status_lock);
    }

    /*
     * Push the operator-configured TOF LED hard brightness cap into the
     * board layer every tick. Enforced at both illumination entry points
     * (service + runtime) — no LED write can exceed this cap. Default
     * set to 50% in config_load_defaults per user directive 2026-04-15.
     */
    laser_controller_board_set_tof_led_max_duty_pct(
        config_snapshot.thresholds.max_tof_led_duty_cycle_pct);

    laser_controller_board_read_inputs(&context->last_inputs, &config_snapshot);
    laser_controller_bench_copy_status(&bench_status);

    const laser_controller_power_tier_t new_tier =
        laser_controller_classify_power_tier(&config_snapshot, &context->last_inputs);
    if (new_tier != context->power_tier) {
        context->power_tier = new_tier;
        laser_controller_logger_logf(
            now_ms,
            "power",
            "power tier -> %s (%.1f W, contract_valid=%d, host_only=%d)",
            laser_controller_power_tier_name(context->power_tier),
            context->last_inputs.pd_negotiated_power_w,
            context->last_inputs.pd_contract_valid,
            context->last_inputs.pd_source_is_host_only);
    }

    /*
     * USB-Debug Mock Layer (control-task only).
     *
     * Order matters:
     *   1. Tick the mock state machine using the REAL power_tier from the
     *      live PD readback. The mock auto-disables itself if real power
     *      arrives, and signals a SYSTEM_MAJOR fault if so.
     *   2. Apply the mock's synthesized telemetry into `context->last_inputs`
     *      AFTER classify_power_tier so we don't trick the PD logic. The
     *      mock only synthesizes rail PGOOD and TEC/LD telemetry; the PD
     *      tier remains real.
     *   3. If the mock signals a PD-conflict on this tick, record a
     *      SYSTEM_MAJOR fault so the same tick collapses rails safe.
     */
    {
        const bool service_mode_active_now =
            laser_controller_service_mode_requested();
        const bool mock_pd_conflict_latched_now =
            laser_controller_usb_debug_mock_tick(
                service_mode_active_now,
                context->power_tier,
                now_ms);
        if (mock_pd_conflict_latched_now) {
            laser_controller_record_fault(
                context,
                now_ms,
                LASER_CONTROLLER_FAULT_USB_DEBUG_MOCK_PD_CONFLICT,
                LASER_CONTROLLER_FAULT_CLASS_SYSTEM_MAJOR,
                "USB debug mock auto-disabled by real PD power");
        }
        laser_controller_usb_debug_mock_apply_to_inputs(
            &context->last_inputs,
            &context->last_outputs,
            laser_controller_deployment_target_temp_c(context, &bench_status),
            bench_status.high_state_current_a,
            now_ms);
    }

    /*
     * LD-temp settle anchors. Updated AFTER the mock apply so the tracked
     * inputs match what safety_evaluate will see. The SBDN anchor uses the
     * previous-tick committed `last_outputs.sbdn_state`, because the
     * current tick's decision has not yet been evaluated. This introduces
     * a one-tick (5 ms) lag which is immaterial against the 2 s window.
     * User directive 2026-04-15 (late).
     */
    if (context->last_inputs.ld_rail_pgood) {
        if (context->ld_rail_pgood_since_ms == 0U) {
            context->ld_rail_pgood_since_ms = now_ms;
        }
    } else {
        context->ld_rail_pgood_since_ms = 0U;
    }

    if (context->last_outputs.sbdn_state != LASER_CONTROLLER_SBDN_STATE_OFF) {
        if (context->sbdn_not_off_since_ms == 0U) {
            context->sbdn_not_off_since_ms = now_ms;
        }
    } else {
        context->sbdn_not_off_since_ms = 0U;
    }

    if (laser_controller_power_tier_is_operational(previous_tier) &&
        !laser_controller_power_tier_is_operational(context->power_tier)) {
        laser_controller_record_fault(
            context,
            now_ms,
            context->power_tier == LASER_CONTROLLER_POWER_TIER_UNKNOWN ?
                LASER_CONTROLLER_FAULT_PD_LOST :
                LASER_CONTROLLER_FAULT_PD_INSUFFICIENT,
            LASER_CONTROLLER_FAULT_CLASS_SYSTEM_MAJOR,
            "operational PD tier lost");
    }

    laser_controller_deployment_tick(context, &config_snapshot, now_ms);

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.now_ms = now_ms;
    snapshot.boot_complete = context->boot_complete;
    snapshot.config_valid = context->config_valid;
    snapshot.fault_latched = laser_controller_fault_latch_active(context);
    snapshot.service_mode_requested = laser_controller_service_mode_requested();
    snapshot.service_mode_active =
        context->state_machine.current == LASER_CONTROLLER_STATE_SERVICE_MODE;
    snapshot.deployment_active = context->deployment.active;
    snapshot.deployment_running = context->deployment.running;
    snapshot.deployment_ready = context->deployment.ready;
    snapshot.deployment_ready_idle = context->deployment.ready_idle;
    snapshot.interlocks_disabled =
        laser_controller_service_interlocks_disabled();
    snapshot.allow_missing_imu =
        snapshot.service_mode_active &&
        !laser_controller_service_module_expected(LASER_CONTROLLER_MODULE_IMU);
    snapshot.allow_missing_tof =
        snapshot.service_mode_active &&
        !laser_controller_service_module_expected(LASER_CONTROLLER_MODULE_TOF);
    snapshot.allow_missing_buttons =
        snapshot.service_mode_active &&
        !laser_controller_service_module_expected(LASER_CONTROLLER_MODULE_BUTTONS);
    snapshot.last_horizon_blocked = context->last_horizon_blocked;
    snapshot.last_distance_blocked = context->last_distance_blocked;
    snapshot.last_lambda_drift_blocked = context->lambda_drift_filter.state;
    snapshot.last_tec_temp_adc_blocked = context->tec_temp_adc_filter.state;
    /*
     * "driver_operate_expected" is TRUE only when the driver is armed in
     * OPERATE mode (SBDN high, PCN high — `select_driver_low_current`
     * false). It is consumed by the LD_LOOP_BAD check in safety.c so
     * the LPGD fault is only raised when the driver is actually
     * supposed to be operating full-current. In idle-bias (SBDN=ON,
     * PCN=LOW) LPGD may legitimately be off and the check must be
     * suppressed.
     */
    snapshot.driver_operate_expected =
        context->last_outputs.sbdn_state == LASER_CONTROLLER_SBDN_STATE_ON &&
        !context->last_outputs.select_driver_low_current;
    snapshot.power_tier = context->power_tier;
    snapshot.host_request_alignment = bench_status.requested_alignment;
    snapshot.host_request_nir =
        context->deployment.active && context->deployment.ready &&
        bench_status.runtime_mode ==
            LASER_CONTROLLER_RUNTIME_MODE_MODULATED_HOST &&
        bench_status.requested_nir;
    /*
     * Runtime mode + button lockout flow into the safety evaluator. In
     * BINARY_TRIGGER mode the buttons are the NIR request source and the
     * lockout is honored. In MODULATED_HOST the buttons are advisory and
     * the lockout is ignored (stage1 cannot fire NIR anyway).
     */
    snapshot.runtime_mode = bench_status.runtime_mode;
    snapshot.button_nir_lockout = context->button_nir_lockout;
    snapshot.target_lambda_nm = laser_controller_deployment_target_lambda_nm(
        context,
        &bench_status);
    snapshot.actual_lambda_nm = laser_controller_lambda_from_temp(
        &config_snapshot,
        context->last_inputs.tec_temp_c);
    /*
     * LD-temp settle durations. safety_evaluate uses both to gate the
     * LD_OVERTEMP check — see LASER_CONTROLLER_LD_TEMP_SETTLE_MS in
     * laser_controller_safety.c. Durations are computed here from the
     * rising-edge anchors; a 0 anchor means the corresponding condition
     * is not currently met (rail off, or SBDN == OFF), so the duration
     * reports 0 and the gate stays closed.
     */
    snapshot.ld_rail_pgood_for_ms = (context->ld_rail_pgood_since_ms == 0U)
        ? 0U
        : (uint32_t)(now_ms - context->ld_rail_pgood_since_ms);
    snapshot.sbdn_not_off_for_ms = (context->sbdn_not_off_since_ms == 0U)
        ? 0U
        : (uint32_t)(now_ms - context->sbdn_not_off_since_ms);
    snapshot.hw = &context->last_inputs;

    laser_controller_safety_evaluate(&config_snapshot, &snapshot, &decision);
    raw_lambda_drift_blocked = decision.lambda_drift_blocked;
    raw_tec_temp_adc_blocked = decision.tec_temp_adc_blocked;
    decision.lambda_drift_blocked = laser_controller_update_hold_filter(
        &context->lambda_drift_filter,
        raw_lambda_drift_blocked,
        config_snapshot.timeouts.lambda_drift_hold_ms,
        now_ms);
    decision.tec_temp_adc_blocked = laser_controller_update_hold_filter(
        &context->tec_temp_adc_filter,
        raw_tec_temp_adc_blocked,
        config_snapshot.timeouts.tec_temp_adc_hold_ms,
        now_ms);
    if (!decision.fault_present && decision.lambda_drift_blocked) {
        decision.fault_present = true;
        decision.fault_code = LASER_CONTROLLER_FAULT_LAMBDA_DRIFT;
        decision.fault_class = LASER_CONTROLLER_FAULT_CLASS_INTERLOCK_AUTO_CLEAR;
        decision.fault_reason = "lambda drift exceeded allowed window";
    }
    if (!decision.fault_present && decision.tec_temp_adc_blocked) {
        decision.fault_present = true;
        decision.fault_code = LASER_CONTROLLER_FAULT_TEC_TEMP_ADC_HIGH;
        decision.fault_class = LASER_CONTROLLER_FAULT_CLASS_INTERLOCK_AUTO_CLEAR;
        decision.fault_reason = "tec temperature adc voltage high";
    }
    if (decision.lambda_drift_blocked || decision.tec_temp_adc_blocked) {
        decision.allow_nir = false;
        decision.nir_output_enable = false;
        /* Interlock-auto-clear fault → fast shutdown. */
        decision.sbdn_state = LASER_CONTROLLER_SBDN_STATE_OFF;
    }
    context->last_horizon_blocked = decision.horizon_blocked;
    context->last_distance_blocked = decision.distance_blocked;

    laser_controller_latch_fault_if_needed(context, now_ms, &decision);

    /*
     * LD_OVERTEMP diag capture — rising-edge only.
     *
     * Gate on ALL THREE:
     *   1. decision.fault_present && decision.fault_code == LD_OVERTEMP
     *      (the fault actually won the first-fault-wins race inside
     *      safety_evaluate — otherwise LD_OVERTEMP was beaten by a
     *      higher-priority fault and we should not record its diag).
     *   2. decision.ld_overtemp_diag_valid (safety_evaluate saw the
     *      overtemp condition fire this tick — set inside the gated
     *      overtemp check in laser_controller_safety.c).
     *   3. !context->active_fault_diag.valid — only capture on the first
     *      tick. Later ticks may see the same condition but the captured
     *      frame must stay frozen at trip time so operators see the
     *      value that tripped, not a drifted rolling reading.
     *
     * Cleared by `laser_controller_app_clear_fault_latch` when the user
     * explicitly clears the latched fault.
     */
    if (decision.fault_present &&
        decision.fault_code == LASER_CONTROLLER_FAULT_LD_OVERTEMP &&
        decision.ld_overtemp_diag_valid &&
        !context->active_fault_diag.valid) {
        context->active_fault_diag.valid = true;
        context->active_fault_diag.code = LASER_CONTROLLER_FAULT_LD_OVERTEMP;
        context->active_fault_diag.measured_c = decision.ld_overtemp_measured_c;
        context->active_fault_diag.measured_voltage_v =
            decision.ld_overtemp_voltage_v;
        context->active_fault_diag.limit_c = decision.ld_overtemp_limit_c;
        context->active_fault_diag.ld_pgood_for_ms =
            decision.ld_overtemp_pgood_for_ms;
        context->active_fault_diag.sbdn_not_off_for_ms =
            decision.ld_overtemp_sbdn_for_ms;
        (void)snprintf(
            context->active_fault_diag.expr,
            sizeof(context->active_fault_diag.expr),
            "ld_temp_c > %.1f C @ %.1f C, %.3f V",
            (double)decision.ld_overtemp_limit_c,
            (double)decision.ld_overtemp_measured_c,
            (double)decision.ld_overtemp_voltage_v);
        laser_controller_logger_logf(
            now_ms,
            "fault",
            "ld_overtemp diag captured: %s "
            "(ld_pgood_for=%lu ms, sbdn_not_off_for=%lu ms)",
            context->active_fault_diag.expr,
            (unsigned long)context->active_fault_diag.ld_pgood_for_ms,
            (unsigned long)context->active_fault_diag.sbdn_not_off_for_ms);
    }

    context->last_outputs = laser_controller_derive_outputs(context, &decision);
    /*
     * Rail watchdogs latch SYSTEM_MAJOR if a rail enable holds without
     * PGOOD coming up. Skip them when service interlocks are disabled
     * (2026-04-16): the bringup workbench lets operators toggle rail
     * enables to verify the GPIO without bench power present, and the
     * watchdog would always trip in that case. The operator still sees
     * pgood telemetry to know the rail didn't come up; the fault
     * re-arms the moment service mode exits.
     */
    const bool rail_watchdog_enabled =
        !laser_controller_service_interlocks_disabled();
    if (rail_watchdog_enabled &&
        !decision.fault_present &&
        laser_controller_rail_timeout_expired(
            &context->tec_rail_watch,
            context->last_outputs.enable_tec_vin,
            context->last_inputs.tec_rail_pgood,
            config_snapshot.timeouts.rail_good_timeout_ms,
            now_ms)) {
        laser_controller_record_fault(
            context,
            now_ms,
            LASER_CONTROLLER_FAULT_TEC_RAIL_BAD,
            LASER_CONTROLLER_FAULT_CLASS_SYSTEM_MAJOR,
            "tec rail enable asserted without pgood");
        decision.fault_present = true;
        decision.fault_code = LASER_CONTROLLER_FAULT_TEC_RAIL_BAD;
        decision.fault_class = LASER_CONTROLLER_FAULT_CLASS_SYSTEM_MAJOR;
        decision.fault_reason = "tec rail enable asserted without pgood";
    }
    if (rail_watchdog_enabled &&
        !decision.fault_present &&
        laser_controller_rail_timeout_expired(
            &context->ld_rail_watch,
            context->last_outputs.enable_ld_vin,
            context->last_inputs.ld_rail_pgood,
            config_snapshot.timeouts.rail_good_timeout_ms,
            now_ms)) {
        laser_controller_record_fault(
            context,
            now_ms,
            LASER_CONTROLLER_FAULT_LD_RAIL_BAD,
            LASER_CONTROLLER_FAULT_CLASS_SYSTEM_MAJOR,
            "ld rail enable asserted without pgood");
        decision.fault_present = true;
        decision.fault_code = LASER_CONTROLLER_FAULT_LD_RAIL_BAD;
        decision.fault_class = LASER_CONTROLLER_FAULT_CLASS_SYSTEM_MAJOR;
        decision.fault_reason = "ld rail enable asserted without pgood";
    }
    if (decision.fault_present &&
        decision.fault_class == LASER_CONTROLLER_FAULT_CLASS_SYSTEM_MAJOR) {
        /*
         * SYSTEM_MAJOR → fast shutdown. Per user directive green is still
         * ungated — alignment_output_enable keeps mirroring request. But
         * we leave `allow_alignment=true` (set earlier in safety.c). The
         * NIR path is forcibly zeroed and SBDN goes hard LOW.
         */
        decision.allow_nir = false;
        decision.nir_output_enable = false;
        decision.sbdn_state = LASER_CONTROLLER_SBDN_STATE_OFF;
        context->last_outputs = laser_controller_derive_outputs(context, &decision);
    }
    /*
     * Button-board policy runs AFTER all fault overrides have settled so
     * the RGB color decision sees the final fault state (and any rail
     * timeout that fired this tick). It writes the RGB target into
     * `context->last_outputs.rgb_led` so apply_outputs picks it up.
     */
    laser_controller_apply_button_board_policy(
        context, &decision, &bench_status, now_ms);

    context->last_decision = decision;
    laser_controller_board_apply_outputs(&context->last_outputs);
    {
        const laser_controller_celsius_t actuator_target_temp_c =
            context->deployment.active && context->deployment.running &&
                    context->deployment.current_step <
                        LASER_CONTROLLER_DEPLOYMENT_STEP_TEC_SETTLE ?
                LASER_CONTROLLER_DEPLOYMENT_DEFAULT_TEMP_C :
                laser_controller_deployment_target_temp_c(context, &bench_status);
        const laser_controller_amps_t commanded_current_a =
            laser_controller_commanded_laser_current_a(
                context,
                &config_snapshot,
                &bench_status,
                decision.nir_output_enable);

        laser_controller_board_apply_actuator_targets(
        &config_snapshot,
        commanded_current_a,
        actuator_target_temp_c,
        bench_status.modulation_enabled,
        bench_status.modulation_frequency_hz,
        bench_status.modulation_duty_cycle_pct,
        decision.nir_output_enable,
        context->last_outputs.select_driver_low_current);
        /*
         * GPIO6 front-LED ownership (highest priority wins):
         *   1. Service mode: integrate bring-up owns the sideband.
         *   2. Deployment-LED-armed (sticky, set on first ready_idle,
         *      cleared on deployment exit): the LED stays on at
         *      `button_led_brightness_pct` through any fault / interlock.
         *      Brightness source-of-truth shared with side buttons and
         *      the GUI slider via `app_set_button_led_brightness`.
         *   3. Otherwise: bench (operate slider) owns it.
         *
         * `button_led_active` (stage1 pressed) is no longer the gate —
         * it's only used by the RGB armed-color branch above. The LED
         * is on whenever deployment_led_armed, regardless of stage state.
         * (2026-04-17 user directive)
         */
        const bool service_owns =
            laser_controller_service_mode_requested();
        const bool deployment_owns =
            !service_owns &&
            context->deployment.active &&
            context->deployment_led_armed;
        const bool illum_enabled =
            !service_owns &&
            (deployment_owns ? true : bench_status.illumination_enabled);
        const uint32_t illum_duty =
            deployment_owns ?
                context->button_led_brightness_pct :
                bench_status.illumination_duty_cycle_pct;
        const uint32_t illum_freq =
            bench_status.illumination_frequency_hz;
        (void)laser_controller_board_set_runtime_tof_illumination(
            illum_enabled,
            illum_duty,
            illum_freq);
    }

    const laser_controller_state_t next_state =
        laser_controller_derive_state(context, &decision);
    laser_controller_log_state_if_changed(context, next_state, now_ms);
    laser_controller_publish_runtime_status(context, now_ms);

    /*
     * Headless auto-deploy state machine (2026-04-16 user feature).
     *
     * If 5 seconds elapse from boot with no host activity, no fault
     * latched, no deployment already active, AND the boot has fully
     * completed (config valid + state machine past BOOT_INIT), enter
     * deployment mode and run the checklist using the saved NIR
     * current and TEC temp / lambda defaults loaded at boot.
     *
     * One-shot — once any state past IDLE is reached, no future tick
     * re-arms. Operator can still manually exit deployment afterwards;
     * we do NOT re-trigger.
     */
    if (context->auto_deploy_state == LASER_CONTROLLER_AUTO_DEPLOY_IDLE &&
        now_ms >= 5000U &&
        context->last_host_activity_ms == 0U &&
        context->boot_complete &&
        context->config_valid &&
        !context->deployment.active &&
        !context->fault_latched &&
        context->state_machine.current != LASER_CONTROLLER_STATE_BOOT_INIT &&
        context->state_machine.current != LASER_CONTROLLER_STATE_FAULT_LATCHED &&
        context->state_machine.current != LASER_CONTROLLER_STATE_SERVICE_MODE) {
        laser_controller_logger_log(
            now_ms,
            "deploy",
            "headless auto-deploy: 5 s elapsed with no host activity");
        context->auto_deploy_state =
            LASER_CONTROLLER_AUTO_DEPLOY_ENTER_PENDING;
    }
    if (context->auto_deploy_state ==
            LASER_CONTROLLER_AUTO_DEPLOY_ENTER_PENDING) {
        if (laser_controller_app_enter_deployment_mode() == ESP_OK) {
            context->auto_deploy_state =
                LASER_CONTROLLER_AUTO_DEPLOY_RUN_PENDING;
        } else {
            /* Couldn't enter (e.g. fault appeared between checks). Mark
             * done so we don't keep retrying. Operator can issue manually. */
            laser_controller_logger_log(
                now_ms,
                "deploy",
                "headless auto-deploy: enter rejected; deferring to operator");
            context->auto_deploy_state = LASER_CONTROLLER_AUTO_DEPLOY_DONE;
        }
    } else if (context->auto_deploy_state ==
                   LASER_CONTROLLER_AUTO_DEPLOY_RUN_PENDING) {
        if (laser_controller_app_run_deployment_sequence() == ESP_OK) {
            context->auto_deploy_state = LASER_CONTROLLER_AUTO_DEPLOY_DONE;
            laser_controller_logger_log(
                now_ms,
                "deploy",
                "headless auto-deploy: checklist started");
        } else {
            laser_controller_logger_log(
                now_ms,
                "deploy",
                "headless auto-deploy: run-sequence rejected; deferring");
            context->auto_deploy_state = LASER_CONTROLLER_AUTO_DEPLOY_DONE;
        }
    }

    /*
     * Headless fault recovery consumer (2026-04-16 user feature).
     *
     * `button_recovery_requested` is raised inside
     * `apply_button_board_policy` when the operator holds side1+side2
     * for 2 s with `fault_latched` true. We consume the flag HERE at
     * the end of the tick — after outputs have been applied and
     * telemetry has been published — so the tick that detected the
     * hold finishes cleanly on still-faulted state, and the recovery
     * actions land on the NEXT tick.
     *
     * Sequence:
     *   1. Clear the fault latch. If the underlying condition persists
     *      the safety evaluator will re-latch on the next tick, which
     *      is the correct behavior.
     *   2. Enter deployment mode (no-op if already active).
     *   3. Kick off the deployment sequence (re-runs the checklist
     *      using the saved NIR current / TEC defaults).
     *
     * Each step is best-effort; if one fails (e.g. fault re-latched
     * immediately, or deployment already running), we log and give up
     * until the operator releases and re-holds the buttons.
     */
    if (context->button_recovery_requested) {
        context->button_recovery_requested = false;

        /*
         * Direct force-clear: we are in the control task (the SOLE
         * owner of these fields) so we bypass the public
         * `clear_fault_latch` API, which would reject the clear if
         * this tick's `decision.fault_present` is true — which is
         * very likely, since the operator typically holds the
         * buttons WHILE the fault is actively re-firing. The safety
         * evaluator will re-latch on the next tick if the underlying
         * condition persists, which is the correct behavior.
         *
         * Cross-task note: `record_fault` is the only other writer
         * and also runs on the control task, so this read-modify is
         * race-free without any lock. The runtime status lock IS
         * still taken so the telemetry snapshot readers see a
         * consistent tear-free fault payload. active_fault_diag is
         * also owned by control task and can be zeroed directly.
         */
        portENTER_CRITICAL(&s_runtime_status_lock);
        context->fault_latched = false;
        context->active_fault_code = LASER_CONTROLLER_FAULT_NONE;
        context->active_fault_class = LASER_CONTROLLER_FAULT_CLASS_NONE;
        context->latched_fault_code = LASER_CONTROLLER_FAULT_NONE;
        context->latched_fault_class = LASER_CONTROLLER_FAULT_CLASS_NONE;
        context->active_fault_reason[0] = '\0';
        context->latched_fault_reason[0] = '\0';
        context->last_fault_ms = 0U;
        memset(&context->active_fault_diag, 0, sizeof(context->active_fault_diag));
        portEXIT_CRITICAL(&s_runtime_status_lock);

        laser_controller_logger_log(
            now_ms,
            "deploy",
            "headless recovery: fault latch force-cleared");

        if (!context->deployment.active) {
            const esp_err_t enter_rc =
                laser_controller_app_enter_deployment_mode();
            if (enter_rc != ESP_OK) {
                laser_controller_logger_logf(
                    now_ms,
                    "deploy",
                    "headless recovery: enter_deployment rejected (%d)",
                    (int)enter_rc);
            }
        }

        const esp_err_t run_rc =
            laser_controller_app_run_deployment_sequence();
        if (run_rc != ESP_OK) {
            laser_controller_logger_logf(
                now_ms,
                "deploy",
                "headless recovery: run_deployment rejected (%d); "
                "checklist will not auto-restart this cycle",
                (int)run_rc);
        } else {
            laser_controller_logger_log(
                now_ms,
                "deploy",
                "headless recovery: checklist restarted");
        }
    }

    /*
     * Headless button-driven start-of-deployment (2026-04-17 user
     * feature). Distinct from the fault-recovery path above: no fault
     * latch to clear, just enter deployment and run the checklist.
     * Raised by `apply_button_board_policy` at the 1 s mark of a
     * side1+side2 hold when no host GUI is connected and the device is
     * idle.
     */
    if (context->button_deploy_requested) {
        context->button_deploy_requested = false;

        if (!context->deployment.active) {
            const esp_err_t enter_rc =
                laser_controller_app_enter_deployment_mode();
            if (enter_rc != ESP_OK) {
                laser_controller_logger_logf(
                    now_ms,
                    "deploy",
                    "headless deploy: enter_deployment rejected (%d)",
                    (int)enter_rc);
                return;
            }
        }

        const esp_err_t run_rc =
            laser_controller_app_run_deployment_sequence();
        if (run_rc != ESP_OK) {
            laser_controller_logger_logf(
                now_ms,
                "deploy",
                "headless deploy: run_deployment rejected (%d)",
                (int)run_rc);
        } else {
            laser_controller_logger_log(
                now_ms,
                "deploy",
                "headless deploy: checklist started via 1 s button hold");
        }
    }

    /*
     * Auto-deploy-on-boot (2026-04-20 user directive). Same enter+run
     * sequence as the headless button path, but triggered by the
     * AUTO_DEPLOY_ON_BOOT service flag instead of a physical gesture.
     * `auto_deploy_requested` is raised at end-of-tick when the device
     * reaches a clean idle posture for the first time after boot;
     * `auto_deploy_armed` latches so the path fires exactly once per
     * power cycle (manual exit + re-entry remains operator-driven).
     */
    if (context->auto_deploy_requested) {
        context->auto_deploy_requested = false;

        if (!context->deployment.active) {
            const esp_err_t enter_rc =
                laser_controller_app_enter_deployment_mode();
            if (enter_rc != ESP_OK) {
                laser_controller_logger_logf(
                    now_ms,
                    "deploy",
                    "auto deploy: enter_deployment rejected (%d)",
                    (int)enter_rc);
                return;
            }
        }

        const esp_err_t run_rc =
            laser_controller_app_run_deployment_sequence();
        if (run_rc != ESP_OK) {
            laser_controller_logger_logf(
                now_ms,
                "deploy",
                "auto deploy: run_deployment rejected (%d)",
                (int)run_rc);
        } else {
            laser_controller_logger_log(
                now_ms,
                "deploy",
                "auto deploy: checklist started (AUTO_DEPLOY_ON_BOOT service flag)");
        }
    }
}

static void laser_controller_run_slow_cycle(laser_controller_context_t *context)
{
    const laser_controller_time_ms_t now_ms = laser_controller_board_uptime_ms();
    const bool alignment_enabled = context->last_outputs.enable_alignment_laser;
    /*
     * "nir_enabled" in the summary log means the driver is actually armed
     * to operate (SBDN == ON). STANDBY (Hi-Z) is not "enabled" — the
     * driver is idle but not emitting.
     */
    const bool nir_enabled =
        context->last_outputs.sbdn_state == LASER_CONTROLLER_SBDN_STATE_ON;

    laser_controller_maybe_reconcile_pd_runtime(context, now_ms);

    if (context->summary_snapshot_valid &&
        context->last_summary_state == context->state_machine.current &&
        context->last_summary_power_tier == context->power_tier &&
        context->last_summary_fault_code ==
            laser_controller_effective_fault_code(context) &&
        context->last_summary_alignment_enabled == alignment_enabled &&
        context->last_summary_nir_enabled == nir_enabled) {
        return;
    }

    context->summary_snapshot_valid = true;
    context->last_summary_state = context->state_machine.current;
    context->last_summary_power_tier = context->power_tier;
    context->last_summary_fault_code =
        laser_controller_effective_fault_code(context);
    context->last_summary_alignment_enabled = alignment_enabled;
    context->last_summary_nir_enabled = nir_enabled;

    laser_controller_logger_logf(
        now_ms,
        "summary",
        "state=%s power=%s align=%d nir=%d fault=%s",
        laser_controller_state_name(context->state_machine.current),
        laser_controller_power_tier_name(context->power_tier),
        alignment_enabled,
        nir_enabled,
        laser_controller_fault_code_name(
            laser_controller_effective_fault_code(context)));
}

static void laser_controller_control_task(void *argument)
{
    laser_controller_context_t *context = (laser_controller_context_t *)argument;
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t slow_divider = 0U;

    for (;;) {
        laser_controller_run_fast_cycle(context);

        slow_divider++;
        if (slow_divider >= LASER_CONTROLLER_SLOW_DIVIDER) {
            slow_divider = 0U;
            laser_controller_run_slow_cycle(context);
        }

        vTaskDelayUntil(&last_wake, LASER_CONTROLLER_CONTROL_PERIOD_TICKS);
    }
}

esp_err_t laser_controller_app_start(void)
{
    if (s_context.started) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_context, 0, sizeof(s_context));

    laser_controller_board_init_safe_defaults();
    laser_controller_logger_init();
    laser_controller_init_nvs_store(laser_controller_board_uptime_ms());
    laser_controller_bench_init_defaults();
    laser_controller_service_init_defaults();
    laser_controller_config_load_defaults(&s_context.config);
    bool firmware_plan_enabled = false;
    {
        laser_controller_power_policy_t stored_power_policy = s_context.config.power;
        laser_controller_service_get_pd_config(
            &stored_power_policy,
            NULL,
            0U,
            &firmware_plan_enabled);
        s_context.config.power = stored_power_policy;
        if (firmware_plan_enabled) {
            laser_controller_board_force_pd_refresh_with_source(
                LASER_CONTROLLER_PD_SNAPSHOT_SOURCE_BOOT_RECONCILE);
        }
    }
    laser_controller_service_get_runtime_safety_policy(
        &s_context.config.thresholds,
        &s_context.config.timeouts);
    /*
     * Restore the `service_flags` bitmap from its standalone NVS key.
     * Missing or fresh-flash devices return 0, matching the safe default
     * (no auto-behavior enabled).
     */
    s_context.config.service_flags =
        laser_controller_service_load_service_flags();
    s_context.config_valid = laser_controller_config_validate(&s_context.config);

    const laser_controller_time_ms_t now_ms = laser_controller_board_uptime_ms();
    {
        laser_controller_bench_target_mode_t saved_target_mode =
            LASER_CONTROLLER_BENCH_TARGET_MODE_TEMP;
        laser_controller_celsius_t saved_target_temp_c = 25.0f;
        laser_controller_nm_t saved_target_lambda_nm = 785.0f;

        laser_controller_service_get_runtime_target(
            &saved_target_mode,
            &saved_target_temp_c,
            &saved_target_lambda_nm);
        if (saved_target_mode == LASER_CONTROLLER_BENCH_TARGET_MODE_LAMBDA) {
            laser_controller_bench_set_target_lambda_nm(
                &s_context.config,
                saved_target_lambda_nm,
                now_ms);
        } else {
            laser_controller_bench_set_target_temp_c(
                &s_context.config,
                saved_target_temp_c,
                now_ms);
        }
        /*
         * 2026-04-16 user feature: load the persisted deployment-default
         * NIR current and seed bench storage so the headless 5 s
         * auto-deploy fires at the operator's last-saved current.
         * Returns 0.0 if never saved (clean device); bench stays safe-zero.
         */
        const laser_controller_amps_t saved_current_a =
            laser_controller_service_load_deployment_current_a();
        laser_controller_bench_set_laser_current_a(
            &s_context.config,
            saved_current_a,
            now_ms);
    }
    laser_controller_state_machine_init(&s_context.state_machine, now_ms);
    laser_controller_deployment_reset_step_status(
        &s_context,
        LASER_CONTROLLER_DEPLOYMENT_STEP_STATUS_INACTIVE);
    {
        s_context.deployment.phase = LASER_CONTROLLER_DEPLOYMENT_PHASE_INACTIVE;
        s_context.deployment.target.target_mode =
            LASER_CONTROLLER_DEPLOYMENT_TARGET_MODE_TEMP;
        s_context.deployment.target.target_temp_c =
            LASER_CONTROLLER_DEPLOYMENT_DEFAULT_TEMP_C;
        s_context.deployment.target.target_lambda_nm = laser_controller_lambda_from_temp(
            &s_context.config,
            LASER_CONTROLLER_DEPLOYMENT_DEFAULT_TEMP_C);
        s_context.deployment.primary_failure_code = LASER_CONTROLLER_FAULT_NONE;
        s_context.deployment.primary_failure_reason[0] = '\0';
        laser_controller_deployment_reset_secondary_effects(&s_context);
    }
    s_context.deployment.max_laser_current_a =
        s_context.config.thresholds.max_laser_current_a;
    s_context.deployment.max_optical_power_w =
        s_context.deployment.max_laser_current_a;

    s_context.boot_complete = true;
    /*
     * If the persisted config has the AUTO_DEPLOY_ON_BOOT service flag
     * set, arm the pending-request latch. The control task will
     * actually fire the deployment from the fast-cycle end-of-tick
     * handler once it sees a clean-idle state. Not done directly here
     * because `laser_controller_app_start` runs on the main task and
     * deployment_enter / run expect to be called from (or synchronized
     * with) the control task's view of `s_context`.
     */
    s_context.auto_deploy_pending =
        (s_context.config.service_flags &
         LASER_CONTROLLER_SERVICE_FLAG_AUTO_DEPLOY_ON_BOOT) != 0U;
    s_context.auto_deploy_armed = false;
    s_context.auto_deploy_requested = false;
    s_context.power_tier = LASER_CONTROLLER_POWER_TIER_UNKNOWN;
    s_context.active_fault_code = LASER_CONTROLLER_FAULT_NONE;
    s_context.active_fault_class = LASER_CONTROLLER_FAULT_CLASS_NONE;
    s_context.latched_fault_code = LASER_CONTROLLER_FAULT_NONE;
    s_context.latched_fault_class = LASER_CONTROLLER_FAULT_CLASS_NONE;
    s_context.active_fault_reason[0] = '\0';
    s_context.latched_fault_reason[0] = '\0';
    s_context.next_pd_reconcile_ms = firmware_plan_enabled ?
        now_ms + LASER_CONTROLLER_PD_RECONCILE_BOOT_DELAY_MS :
        0U;
    s_context.started = true;

    if (!s_context.config_valid) {
        s_context.fault_latched = true;
        s_context.active_fault_code = LASER_CONTROLLER_FAULT_INVALID_CONFIG;
        s_context.active_fault_class = LASER_CONTROLLER_FAULT_CLASS_SYSTEM_MAJOR;
        s_context.latched_fault_code = LASER_CONTROLLER_FAULT_INVALID_CONFIG;
        s_context.latched_fault_class =
            LASER_CONTROLLER_FAULT_CLASS_SYSTEM_MAJOR;
        laser_controller_logger_log(
            now_ms,
            "boot",
            "config invalid by default; provisioning required before any beam enable");
        laser_controller_service_mark_runtime_status(
            "Normal config invalid; bring-up service mode remains available with beam disabled.",
            now_ms);
    } else {
        laser_controller_logger_log(
            now_ms,
            "boot",
            "config validated with conservative bench defaults; per-unit calibration still required before clinical use");
        laser_controller_service_mark_runtime_status(
            "Bench-safe default configuration loaded. Service bring-up remains the correct path for an unpopulated board.",
            now_ms);
    }

    if ((uint32_t)LASER_CONTROLLER_CONTROL_PERIOD_TICKS * (uint32_t)portTICK_PERIOD_MS !=
        LASER_CONTROLLER_CONTROL_PERIOD_MS) {
        laser_controller_logger_logf(
            now_ms,
            "scheduler",
            "control period quantized from %u ms to %u ms because CONFIG_FREERTOS_HZ=%u",
            (unsigned)LASER_CONTROLLER_CONTROL_PERIOD_MS,
            (unsigned)LASER_CONTROLLER_CONTROL_PERIOD_TICKS * (unsigned)portTICK_PERIOD_MS,
            (unsigned)configTICK_RATE_HZ);
    }

    laser_controller_logger_log(
        now_ms,
        "boot",
        "startup complete; control task entering safe supervision loop");
    laser_controller_publish_runtime_status(&s_context, now_ms);
    laser_controller_comms_start();

    TaskHandle_t handle = xTaskCreateStaticPinnedToCore(
        laser_controller_control_task,
        "laser_ctl",
        LASER_CONTROLLER_CONTROL_STACK_WORDS,
        &s_context,
        LASER_CONTROLLER_CONTROL_PRIORITY,
        s_control_task_stack,
        &s_control_task_tcb,
        tskNO_AFFINITY);

    if (handle == NULL) {
        s_context.started = false;
        return ESP_FAIL;
    }

    return ESP_OK;
}

bool laser_controller_app_copy_status(laser_controller_runtime_status_t *status)
{
    if (status == NULL) {
        return false;
    }

    portENTER_CRITICAL(&s_runtime_status_lock);
    *status = s_runtime_status;
    portEXIT_CRITICAL(&s_runtime_status_lock);
    laser_controller_bench_copy_status(&status->bench);
    laser_controller_service_copy_status(&status->bringup);
    if (status->deployment.active && !status->deployment.ready) {
        status->bench.target_mode =
            status->deployment.target.target_mode ==
                    LASER_CONTROLLER_DEPLOYMENT_TARGET_MODE_TEMP ?
                LASER_CONTROLLER_BENCH_TARGET_MODE_TEMP :
                LASER_CONTROLLER_BENCH_TARGET_MODE_LAMBDA;
        status->bench.target_temp_c = status->deployment.target.target_temp_c;
        status->bench.target_lambda_nm = status->deployment.target.target_lambda_nm;
    }

    status->fault_latched =
        status->latched_fault_code != LASER_CONTROLLER_FAULT_NONE &&
        status->latched_fault_class != LASER_CONTROLLER_FAULT_CLASS_NONE;

    return status->started;
}

esp_err_t laser_controller_app_clear_fault_latch(void)
{
    laser_controller_time_ms_t now_ms;

    if (!s_context.started) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_context.config_valid) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_context.last_decision.fault_present) {
        return ESP_ERR_INVALID_STATE;
    }

    now_ms = laser_controller_board_uptime_ms();

    /*
     * 2026-04-17 threading fix: defer the fault-field zero-out to the
     * control task. Previously this path wrote `fault_latched`,
     * `active_fault_*`, `latched_fault_*`, `last_fault_ms` directly from
     * the comms task under `s_runtime_status_lock`, but `record_fault`
     * writes those same fields from the control task WITHOUT any lock
     * (it is the sole writer). The cross-task direct write was the same
     * data-race pattern that caused the earlier GPIO6 LED injury.
     *
     * Now we set BOTH request flags under `s_context_lock`. The control
     * task drains them at the top of its next tick (<= 5 ms) and
     * performs the zero-out as the sole writer, race-free. Telemetry
     * publishing that happens between this point and the drain still
     * sees the latched state — the clear becomes visible to host within
     * one control-cycle.
     */
    portENTER_CRITICAL(&s_context_lock);
    s_context.clear_fault_latch_request = true;
    s_context.clear_fault_diag_request = true;
    portEXIT_CRITICAL(&s_context_lock);

    /*
     * Also clear the USB-Debug Mock PD-conflict latch. The mock blocks
     * re-enable while the latch is set, so the global fault-clear flow
     * must drop both at the same time. The mock latch is owned by the
     * control task — but this function is normally called from comms,
     * and the next control tick will pick it up before any further mock
     * enable request is processed.
     */
    laser_controller_usb_debug_mock_clear_pd_conflict_latch();

    laser_controller_logger_log(now_ms, "fault", "fault latch clear requested (drain next tick)");
    laser_controller_publish_runtime_status(&s_context, now_ms);
    return ESP_OK;
}

esp_err_t laser_controller_app_set_service_flags(uint32_t service_flags)
{
    if (!s_context.started) {
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL(&s_context_lock);
    s_context.config.service_flags = service_flags;
    portEXIT_CRITICAL(&s_context_lock);

    /*
     * Push the new flags into the service layer so the next NVS save
     * picks them up. The service layer owns the actual write; we just
     * hand it the new value and let its existing touch_profile path
     * schedule the persist.
     */
    laser_controller_service_set_service_flags(service_flags);

    laser_controller_logger_logf(
        laser_controller_board_uptime_ms(),
        "bench",
        "service_flags updated to 0x%08lX",
        (unsigned long)service_flags);
    return ESP_OK;
}

esp_err_t laser_controller_app_set_runtime_safety_policy(
    const laser_controller_runtime_safety_policy_t *policy)
{
    laser_controller_config_t candidate;
    laser_controller_bench_status_t bench_status;
    laser_controller_time_ms_t now_ms;

    if (!s_context.started) {
        return ESP_ERR_INVALID_STATE;
    }

    if (policy == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_context_lock);
    candidate = s_context.config;
    portEXIT_CRITICAL(&s_context_lock);

    candidate.thresholds = policy->thresholds;
    candidate.timeouts = policy->timeouts;

    if (!laser_controller_config_validate_runtime_safety(&candidate)) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_context_lock);
    s_context.config.thresholds = policy->thresholds;
    s_context.config.timeouts = policy->timeouts;
    if (s_context.deployment.active) {
        s_context.deployment.target.target_temp_c =
            laser_controller_deployment_clamp_target_temp_c(
                &candidate,
                s_context.deployment.target.target_temp_c);
        s_context.deployment.target.target_lambda_nm = laser_controller_lambda_from_temp(
            &candidate,
            s_context.deployment.target.target_temp_c);
        laser_controller_deployment_recompute_power_cap(&s_context, &candidate);
    }
    portEXIT_CRITICAL(&s_context_lock);

    now_ms = laser_controller_board_uptime_ms();
    laser_controller_service_set_runtime_safety_policy(
        &policy->thresholds,
        &policy->timeouts,
        now_ms);
    laser_controller_bench_copy_status(&bench_status);
    laser_controller_bench_set_laser_current_a(
        &candidate,
        bench_status.high_state_current_a,
        now_ms);

    laser_controller_logger_logf(
        now_ms,
        "config",
        "runtime safety updated horizon=%.2f deg tof=%.2f..%.2f m lambda=%.2f nm tec_adc=%.3f V",
        policy->thresholds.horizon_threshold_rad * 57.2957795130823208768f,
        policy->thresholds.tof_min_range_m,
        policy->thresholds.tof_max_range_m,
        policy->thresholds.lambda_drift_limit_nm,
        policy->thresholds.tec_temp_adc_trip_v);
    laser_controller_publish_runtime_status(&s_context, now_ms);
    return ESP_OK;
}

esp_err_t laser_controller_app_set_runtime_power_policy(
    const laser_controller_power_policy_t *policy)
{
    laser_controller_config_t candidate;
    laser_controller_time_ms_t now_ms;

    if (!s_context.started) {
        return ESP_ERR_INVALID_STATE;
    }

    if (policy == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_context_lock);
    candidate = s_context.config;
    portEXIT_CRITICAL(&s_context_lock);

    candidate.power = *policy;
    if (!laser_controller_config_validate(&candidate)) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_context_lock);
    s_context.config.power = *policy;
    s_context.power_tier = laser_controller_classify_power_tier(
        &s_context.config,
        &s_context.last_inputs);
    if (s_context.deployment.active) {
        laser_controller_deployment_recompute_power_cap(&s_context, &s_context.config);
    }
    portEXIT_CRITICAL(&s_context_lock);

    now_ms = laser_controller_board_uptime_ms();
    laser_controller_logger_logf(
        now_ms,
        "config",
        "runtime power policy updated host_only<=%.1fW reduced=%.1f..%.1fW full>=%.1fW",
        policy->programming_only_max_w,
        policy->reduced_mode_min_w,
        policy->reduced_mode_max_w,
        policy->full_mode_min_w);
    laser_controller_publish_runtime_status(&s_context, now_ms);
    return ESP_OK;
}

bool laser_controller_deployment_mode_active(void)
{
    bool active = false;

    portENTER_CRITICAL(&s_context_lock);
    active = s_context.deployment.active;
    portEXIT_CRITICAL(&s_context_lock);
    return active;
}

bool laser_controller_deployment_mode_running(void)
{
    bool running = false;

    portENTER_CRITICAL(&s_context_lock);
    running = s_context.deployment.running;
    portEXIT_CRITICAL(&s_context_lock);
    return running;
}

bool laser_controller_deployment_mode_ready(void)
{
    bool ready = false;

    portENTER_CRITICAL(&s_context_lock);
    ready = s_context.deployment.active && s_context.deployment.ready;
    portEXIT_CRITICAL(&s_context_lock);
    return ready;
}

bool laser_controller_deployment_copy_status(
    laser_controller_deployment_status_t *status)
{
    if (status == NULL) {
        return false;
    }

    portENTER_CRITICAL(&s_context_lock);
    *status = s_context.deployment;
    portEXIT_CRITICAL(&s_context_lock);
    return true;
}

esp_err_t laser_controller_app_enter_deployment_mode(void)
{
    const laser_controller_time_ms_t now_ms = laser_controller_board_uptime_ms();

    if (!s_context.started || !s_context.config_valid) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_context.deployment.running ||
        laser_controller_fault_latch_active(&s_context)) {
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * 2026-04-17 (audit round 2, S2): idempotent reject. The previous
     * contract silently re-wrote deployment fields (zero'd sequence_id,
     * reset target to default) whenever deployment.active was already
     * true. A second client hitting `deployment.enter` between the
     * first client's checklist pauses would therefore replace the
     * first client's target behind its back. Reject here so the
     * caller sees a clear error and the first client's state is
     * preserved.
     */
    if (s_context.deployment.active) {
        return ESP_ERR_INVALID_STATE;
    }

    laser_controller_bench_clear_requests(now_ms);
    (void)laser_controller_board_set_runtime_tof_illumination(false, 0U, 20000U);
    laser_controller_force_safe_outputs(&s_context);
    laser_controller_service_prepare_for_deployment(now_ms);

    portENTER_CRITICAL(&s_context_lock);
    s_context.deployment.active = true;
    s_context.deployment.running = false;
    s_context.deployment.ready = false;
    s_context.deployment.ready_idle = false;
    s_context.deployment.ready_qualified = false;
    s_context.deployment.ready_invalidated = false;
    s_context.deployment.failed = false;
    s_context.deployment.sequence_id = 0U;
    s_context.deployment.phase = LASER_CONTROLLER_DEPLOYMENT_PHASE_ENTRY;
    s_context.deployment.current_step = LASER_CONTROLLER_DEPLOYMENT_STEP_NONE;
    s_context.deployment.last_completed_step = LASER_CONTROLLER_DEPLOYMENT_STEP_NONE;
    s_context.deployment.failure_code = LASER_CONTROLLER_FAULT_NONE;
    s_context.deployment.failure_reason[0] = '\0';
    s_context.deployment.primary_failure_code = LASER_CONTROLLER_FAULT_NONE;
    s_context.deployment.primary_failure_reason[0] = '\0';
    laser_controller_deployment_reset_secondary_effects(&s_context);
    s_context.deployment.target.target_mode =
        LASER_CONTROLLER_DEPLOYMENT_TARGET_MODE_TEMP;
    s_context.deployment.target.target_temp_c =
        LASER_CONTROLLER_DEPLOYMENT_DEFAULT_TEMP_C;
    s_context.deployment.target.target_lambda_nm = laser_controller_lambda_from_temp(
        &s_context.config,
        s_context.deployment.target.target_temp_c);
    laser_controller_deployment_reset_step_status(
        &s_context,
        LASER_CONTROLLER_DEPLOYMENT_STEP_STATUS_INACTIVE);
    laser_controller_deployment_recompute_power_cap(&s_context, &s_context.config);
    s_context.deployment_step_started_ms = now_ms;
    s_context.deployment_phase_started_ms = now_ms;
    s_context.deployment_substep = 0U;
    s_context.deployment_step_action_performed = false;
    portEXIT_CRITICAL(&s_context_lock);

    laser_controller_logger_log(
        now_ms,
        "deploy",
        "deployment mode entered; service writes locked and outputs held safe");
    laser_controller_publish_runtime_status(&s_context, now_ms);
    return ESP_OK;
}

esp_err_t laser_controller_app_exit_deployment_mode(void)
{
    const laser_controller_time_ms_t now_ms = laser_controller_board_uptime_ms();
    laser_controller_bench_status_t bench_status = { 0 };

    if (!s_context.started || !s_context.deployment.active) {
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * 2026-04-17 user directive: permit graceful abort while the
     * checklist is running. Previously the handler refused while
     * `deployment.running == true`, forcing operators to either wait
     * for a step timeout or physically pull the USB-C cable. Now we
     * accept the request in any deployment state. `force_safe_outputs`
     * (called below via the bench-clear path + derive_outputs on the
     * next tick) drops all rails and SBDN=OFF synchronously with
     * `deployment.active = false`, so the abort is hardware-safe.
     *
     * If the checklist was in a step that had non-trivial hardware
     * side effects mid-execution (e.g. DAC_SAFE_ZERO, RAIL_SEQUENCE),
     * those writes persist until the next tick's derive_outputs/apply,
     * which is the same 5 ms window as any other state transition.
     */
    const bool was_running = s_context.deployment.running;
    if (was_running) {
        laser_controller_logger_log(
            now_ms,
            "deploy",
            "deployment sequence aborted mid-checklist by operator request");
    }

    laser_controller_service_prepare_for_deployment(now_ms);
    laser_controller_bench_copy_status(&bench_status);
    laser_controller_bench_clear_requests(now_ms);
    (void)laser_controller_board_set_runtime_tof_illumination(false, 0U, 20000U);
    laser_controller_bench_set_modulation(
        false,
        bench_status.modulation_frequency_hz,
        bench_status.modulation_duty_cycle_pct,
        now_ms);
    /*
     * Force PCN LOW (stop any LEDC PWM owning GPIO21) and drive all
     * outputs to the safe posture BEFORE we flip `deployment.active =
     * false`. This makes the abort hardware-deterministic regardless
     * of whether the checklist was mid-step when the operator hit
     * exit. The control task's next derive_outputs will see
     * !deployment.active and keep rails OFF indefinitely.
     */
    laser_controller_board_force_pcn_low();
    laser_controller_force_safe_outputs(&s_context);

    portENTER_CRITICAL(&s_context_lock);
    s_context.deployment.active = false;
    s_context.deployment.running = false;
    s_context.deployment.ready = false;
    s_context.deployment.ready_idle = false;
    s_context.deployment.ready_qualified = false;
    s_context.deployment.ready_invalidated = false;
    s_context.deployment.failed = false;
    /*
     * Sticky LED-armed flag clears with deployment exit so the next
     * deployment cycle re-arms cleanly.
     */
    s_context.deployment_led_armed = false;
    s_context.button_led_initialized = false;
    s_context.deployment.phase = LASER_CONTROLLER_DEPLOYMENT_PHASE_INACTIVE;
    s_context.deployment.current_step = LASER_CONTROLLER_DEPLOYMENT_STEP_NONE;
    s_context.deployment.last_completed_step = LASER_CONTROLLER_DEPLOYMENT_STEP_NONE;
    s_context.deployment.failure_code = LASER_CONTROLLER_FAULT_NONE;
    s_context.deployment.failure_reason[0] = '\0';
    s_context.deployment.primary_failure_code = LASER_CONTROLLER_FAULT_NONE;
    s_context.deployment.primary_failure_reason[0] = '\0';
    laser_controller_deployment_reset_secondary_effects(&s_context);
    s_context.deployment.max_laser_current_a =
        s_context.config.thresholds.max_laser_current_a;
    s_context.deployment.max_optical_power_w =
        s_context.deployment.max_laser_current_a;
    laser_controller_deployment_reset_step_status(
        &s_context,
        LASER_CONTROLLER_DEPLOYMENT_STEP_STATUS_INACTIVE);
    s_context.deployment_step_started_ms = now_ms;
    s_context.deployment_phase_started_ms = now_ms;
    s_context.deployment_substep = 0U;
    s_context.deployment_step_action_performed = false;
    portEXIT_CRITICAL(&s_context_lock);

    /*
     * Auto-clear the fault latch on deployment exit per user directive
     * 2026-04-15 (late). Operators were having to click "Exit deployment"
     * then "Clear faults" separately for every failed deployment attempt.
     * This consolidates the two into a single gesture.
     *
     * 2026-04-17 threading fix: the fault-field zero-out is deferred to
     * the control task via request flags. `record_fault` writes those
     * same fields from the control task without any lock (it is the
     * sole writer), so writing them from the comms task here was a
     * data race — the same pattern that caused the earlier GPIO6 LED
     * injury. Both the latch-clear request AND the diag-clear request
     * are set under `s_context_lock`, and the control task drains them
     * at the top of its next tick (<= 5 ms).
     *
     * Rails drop in `derive_outputs` on the same tick thanks to
     * `deployment.active = false`, so re-latching of rail-gated faults
     * is unlikely even during the brief window before the drain.
     */
    portENTER_CRITICAL(&s_context_lock);
    s_context.clear_fault_latch_request = true;
    s_context.clear_fault_diag_request = true;
    portEXIT_CRITICAL(&s_context_lock);

    laser_controller_usb_debug_mock_clear_pd_conflict_latch();

    laser_controller_logger_log(
        now_ms,
        "deploy",
        "deployment mode exited; controller returned to non-deployed safe supervision (fault latch auto-cleared)");
    laser_controller_publish_runtime_status(&s_context, now_ms);
    return ESP_OK;
}

esp_err_t laser_controller_app_run_deployment_sequence(void)
{
    const laser_controller_time_ms_t now_ms = laser_controller_board_uptime_ms();

    if (!s_context.started || !s_context.config_valid ||
        !s_context.deployment.active || s_context.deployment.running ||
        laser_controller_fault_latch_active(&s_context)) {
        return ESP_ERR_INVALID_STATE;
    }

    laser_controller_bench_clear_requests(now_ms);
    (void)laser_controller_board_set_runtime_tof_illumination(false, 0U, 20000U);
    /*
     * 2026-04-16 user directive: force PCN LOW (LISL idle bias) before
     * the checklist starts. `force_safe_outputs` writes the GPIO level
     * but does NOT stop an active LEDC PWM that may have been modulating
     * PCN moments earlier — the LEDC peripheral keeps owning the pin
     * and gpio_set_level is overridden until the channel stops. This
     * call stops the LEDC explicitly and drives the GPIO low, so the
     * checklist starts from a clean LISL posture.
     */
    laser_controller_board_force_pcn_low();
    laser_controller_force_safe_outputs(&s_context);
    laser_controller_service_prepare_for_deployment(now_ms);

    portENTER_CRITICAL(&s_context_lock);
    s_context.deployment.running = true;
    s_context.deployment.ready = false;
    s_context.deployment.ready_idle = false;
    s_context.deployment.ready_qualified = false;
    s_context.deployment.ready_invalidated = false;
    s_context.deployment.failed = false;
    s_context.deployment.sequence_id++;
    s_context.deployment.phase = LASER_CONTROLLER_DEPLOYMENT_PHASE_CHECKLIST;
    s_context.deployment.failure_code = LASER_CONTROLLER_FAULT_NONE;
    s_context.deployment.failure_reason[0] = '\0';
    s_context.deployment.primary_failure_code = LASER_CONTROLLER_FAULT_NONE;
    s_context.deployment.primary_failure_reason[0] = '\0';
    laser_controller_deployment_reset_secondary_effects(&s_context);
    laser_controller_deployment_reset_step_status(
        &s_context,
        LASER_CONTROLLER_DEPLOYMENT_STEP_STATUS_PENDING);
    laser_controller_deployment_begin_step(
        &s_context,
        LASER_CONTROLLER_DEPLOYMENT_STEP_OWNERSHIP_RECLAIM,
        now_ms);
    portEXIT_CRITICAL(&s_context_lock);

    laser_controller_logger_log(
        now_ms,
        "deploy",
        "deployment sequence started");
    laser_controller_publish_runtime_status(&s_context, now_ms);
    return ESP_OK;
}

esp_err_t laser_controller_app_set_deployment_target(
    const laser_controller_deployment_target_t *target)
{
    laser_controller_deployment_target_t sanitized = { 0 };
    const laser_controller_time_ms_t now_ms = laser_controller_board_uptime_ms();

    if (!s_context.started || !s_context.deployment.active || target == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    sanitized = *target;
    if (sanitized.target_mode == LASER_CONTROLLER_DEPLOYMENT_TARGET_MODE_LAMBDA) {
        sanitized.target_mode = LASER_CONTROLLER_DEPLOYMENT_TARGET_MODE_LAMBDA;
        sanitized.target_temp_c = laser_controller_deployment_clamp_target_temp_c(
            &s_context.config,
            laser_controller_temp_from_lambda(
                &s_context.config,
                sanitized.target_lambda_nm));
        sanitized.target_lambda_nm = laser_controller_lambda_from_temp(
            &s_context.config,
            sanitized.target_temp_c);
    } else {
        sanitized.target_mode = LASER_CONTROLLER_DEPLOYMENT_TARGET_MODE_TEMP;
        sanitized.target_temp_c = laser_controller_deployment_clamp_target_temp_c(
            &s_context.config,
            sanitized.target_temp_c);
        sanitized.target_lambda_nm = laser_controller_lambda_from_temp(
            &s_context.config,
            sanitized.target_temp_c);
    }

    portENTER_CRITICAL(&s_context_lock);
    s_context.deployment.target = sanitized;
    portEXIT_CRITICAL(&s_context_lock);

    laser_controller_service_set_runtime_target(
        sanitized.target_mode == LASER_CONTROLLER_DEPLOYMENT_TARGET_MODE_LAMBDA ?
            LASER_CONTROLLER_BENCH_TARGET_MODE_LAMBDA :
            LASER_CONTROLLER_BENCH_TARGET_MODE_TEMP,
        sanitized.target_temp_c,
        sanitized.target_lambda_nm,
        now_ms);

    laser_controller_logger_logf(
        now_ms,
        "deploy",
        "deployment target -> %s %.2f / %.2f",
        laser_controller_deployment_target_mode_name(sanitized.target_mode),
        sanitized.target_temp_c,
        sanitized.target_lambda_nm);
    laser_controller_publish_runtime_status(&s_context, now_ms);
    return ESP_OK;
}
