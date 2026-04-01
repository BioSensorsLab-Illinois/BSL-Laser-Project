#include "laser_controller_app.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
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

#define LASER_CONTROLLER_CONTROL_PERIOD_MS 5U
#define LASER_CONTROLLER_SLOW_DIVIDER 10U
#define LASER_CONTROLLER_CONTROL_STACK_WORDS 4096U
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
    laser_controller_power_tier_t power_tier;
    laser_controller_state_t last_summary_state;
    laser_controller_power_tier_t last_summary_power_tier;
    laser_controller_fault_code_t last_summary_fault_code;
    bool last_summary_alignment_enabled;
    bool last_summary_nir_enabled;
    laser_controller_time_ms_t next_pd_reconcile_ms;
    laser_controller_config_t config;
    laser_controller_state_machine_t state_machine;
    laser_controller_board_inputs_t last_inputs;
    laser_controller_board_outputs_t last_outputs;
    laser_controller_safety_decision_t last_decision;
} laser_controller_context_t;

static laser_controller_context_t s_context;
static laser_controller_runtime_status_t s_runtime_status;
static StaticTask_t s_control_task_tcb;
static StackType_t s_control_task_stack[LASER_CONTROLLER_CONTROL_STACK_WORDS];
static portMUX_TYPE s_context_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_runtime_status_lock = portMUX_INITIALIZER_UNLOCKED;

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
    esp_err_t err;

    if (context == NULL ||
        now_ms < context->next_pd_reconcile_ms ||
        !laser_controller_service_module_expected(LASER_CONTROLLER_MODULE_PD)) {
        return;
    }

    laser_controller_service_get_pd_config(
        &stored_power_policy,
        stored_profiles,
        LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT,
        &firmware_plan_enabled);

    if (!firmware_plan_enabled) {
        return;
    }

    if ((!context->last_inputs.pd_contract_valid &&
         !context->last_inputs.pd_source_is_host_only) ||
        context->last_inputs.pd_sink_profile_count == 0U) {
        return;
    }

    if (laser_controller_pd_profiles_match(
            stored_profiles,
            context->last_inputs.pd_sink_profiles,
            context->last_inputs.pd_sink_profile_count)) {
        return;
    }

    err = laser_controller_service_apply_saved_pd_runtime(now_ms);
    laser_controller_board_force_pd_refresh();
    context->next_pd_reconcile_ms =
        now_ms + (err == ESP_OK ?
                      LASER_CONTROLLER_PD_RECONCILE_SUCCESS_MS :
                      LASER_CONTROLLER_PD_RECONCILE_RETRY_MS);
    if (err == ESP_OK) {
        laser_controller_logger_log(
            now_ms,
            "pd",
            "saved PDO plan differed from STUSB runtime state; reconcile applied");
    } else {
        laser_controller_logger_logf(
            now_ms,
            "pd",
            "saved PDO plan differed from STUSB runtime state; reconcile failed (%s)",
            esp_err_to_name(err));
    }
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

    if (fault_class != LASER_CONTROLLER_FAULT_CLASS_INTERLOCK_AUTO_CLEAR) {
        context->fault_latched = true;
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

    if (context->active_fault_class == LASER_CONTROLLER_FAULT_CLASS_INTERLOCK_AUTO_CLEAR &&
        context->active_fault_code != LASER_CONTROLLER_FAULT_NONE) {
        laser_controller_logger_logf(
            now_ms,
            "fault",
            "auto-clear interlock released: %s",
            laser_controller_fault_code_name(context->active_fault_code));
        context->active_fault_code = LASER_CONTROLLER_FAULT_NONE;
        context->active_fault_class = LASER_CONTROLLER_FAULT_CLASS_NONE;
    }
}

static void laser_controller_publish_runtime_status(
    const laser_controller_context_t *context,
    laser_controller_time_ms_t now_ms)
{
    laser_controller_runtime_status_t status;

    memset(&status, 0, sizeof(status));
    status.started = context->started;
    status.boot_complete = context->boot_complete;
    status.config_valid = context->config_valid;
    status.fault_latched = context->fault_latched;
    status.uptime_ms = now_ms;
    status.state = context->state_machine.current;
    status.power_tier = context->power_tier;
    status.active_fault_code = context->active_fault_code;
    status.active_fault_class = context->active_fault_class;
    status.active_fault_count = context->active_fault_count;
    status.trip_counter = context->trip_counter;
    status.last_fault_ms = context->last_fault_ms;
    status.inputs = context->last_inputs;
    status.outputs = context->last_outputs;
    status.decision = context->last_decision;
    portENTER_CRITICAL(&s_context_lock);
    status.config = context->config;
    portEXIT_CRITICAL(&s_context_lock);
    laser_controller_bench_copy_status(&status.bench);
    laser_controller_service_copy_status(&status.bringup);

    portENTER_CRITICAL(&s_runtime_status_lock);
    s_runtime_status = status;
    portEXIT_CRITICAL(&s_runtime_status_lock);
}

static laser_controller_board_outputs_t laser_controller_derive_outputs(
    const laser_controller_context_t *context,
    const laser_controller_safety_decision_t *decision)
{
    laser_controller_service_status_t service_status;
    laser_controller_board_outputs_t outputs = {
        .enable_ld_vin = false,
        .enable_tec_vin = false,
        .enable_haptic_driver = false,
        .enable_alignment_laser = false,
        .assert_driver_standby = true,
        .select_driver_low_current = true,
    };

    if (!context->boot_complete || !context->config_valid) {
        return outputs;
    }

    laser_controller_service_copy_status(&service_status);

    if (context->state_machine.current == LASER_CONTROLLER_STATE_SERVICE_MODE) {
        outputs.enable_ld_vin = service_status.ld_rail_debug_enabled;
        outputs.enable_tec_vin = service_status.tec_rail_debug_enabled;
        outputs.enable_haptic_driver =
            service_status.haptic_driver_enable_requested;
        return outputs;
    }

    if (context->fault_latched || laser_controller_service_mode_requested()) {
        return outputs;
    }

    if (context->power_tier == LASER_CONTROLLER_POWER_TIER_REDUCED ||
        context->power_tier == LASER_CONTROLLER_POWER_TIER_FULL) {
        outputs.enable_tec_vin = true;
    }

    if (context->power_tier == LASER_CONTROLLER_POWER_TIER_FULL &&
        context->last_inputs.tec_rail_pgood &&
        context->last_inputs.tec_temp_good) {
        outputs.enable_ld_vin = true;
    }

    outputs.enable_alignment_laser = decision->alignment_output_enable;
    outputs.assert_driver_standby = !decision->nir_output_enable;
    outputs.select_driver_low_current = !decision->nir_output_enable;

    if (decision->fault_present &&
        decision->fault_class == LASER_CONTROLLER_FAULT_CLASS_SYSTEM_MAJOR) {
        outputs.enable_ld_vin = false;
        outputs.enable_tec_vin = false;
        outputs.enable_alignment_laser = false;
        outputs.assert_driver_standby = true;
    }

    return outputs;
}

static laser_controller_state_t laser_controller_derive_state(
    const laser_controller_context_t *context,
    const laser_controller_safety_decision_t *decision)
{
    if (laser_controller_service_mode_requested()) {
        return LASER_CONTROLLER_STATE_SERVICE_MODE;
    }

    if (context->fault_latched) {
        return LASER_CONTROLLER_STATE_FAULT_LATCHED;
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
            context->active_fault_code)) {
        laser_controller_record_fault(
            context,
            now_ms,
            LASER_CONTROLLER_FAULT_UNEXPECTED_STATE,
            LASER_CONTROLLER_FAULT_CLASS_SAFETY_LATCHED,
            "illegal state transition blocked");
        (void)laser_controller_state_transition(
            &context->state_machine,
            LASER_CONTROLLER_STATE_FAULT_LATCHED,
            now_ms,
            context->active_fault_code);
        return;
    }

    laser_controller_logger_logf(
        now_ms,
        "state",
        "%s -> %s",
        laser_controller_state_name(context->state_machine.previous),
        laser_controller_state_name(context->state_machine.current));
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
    const laser_controller_power_tier_t previous_tier = context->power_tier;

    portENTER_CRITICAL(&s_context_lock);
    config_snapshot = context->config;
    portEXIT_CRITICAL(&s_context_lock);

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

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.now_ms = now_ms;
    snapshot.boot_complete = context->boot_complete;
    snapshot.config_valid = context->config_valid;
    snapshot.fault_latched = context->fault_latched;
    snapshot.service_mode_requested = laser_controller_service_mode_requested();
    snapshot.service_mode_active =
        context->state_machine.current == LASER_CONTROLLER_STATE_SERVICE_MODE;
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
    snapshot.power_tier = context->power_tier;
    snapshot.target_lambda_nm = bench_status.target_lambda_nm;
    snapshot.actual_lambda_nm = laser_controller_lambda_from_temp(
        &config_snapshot,
        context->last_inputs.tec_temp_c);
    snapshot.hw = context->last_inputs;

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
        decision.driver_standby_asserted = true;
    }
    context->last_horizon_blocked = decision.horizon_blocked;
    context->last_distance_blocked = decision.distance_blocked;

    laser_controller_latch_fault_if_needed(context, now_ms, &decision);

    context->last_outputs = laser_controller_derive_outputs(context, &decision);
    if (!decision.fault_present &&
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
    if (!decision.fault_present &&
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
        decision.allow_alignment = false;
        decision.allow_nir = false;
        decision.alignment_output_enable = false;
        decision.nir_output_enable = false;
        decision.driver_standby_asserted = true;
        context->last_outputs = laser_controller_derive_outputs(context, &decision);
    }
    context->last_decision = decision;
    laser_controller_board_apply_outputs(&context->last_outputs);
    laser_controller_board_apply_actuator_targets(
        &config_snapshot,
        bench_status.high_state_current_a,
        bench_status.target_temp_c,
        bench_status.modulation_enabled,
        bench_status.modulation_frequency_hz,
        bench_status.modulation_duty_cycle_pct,
        decision.nir_output_enable,
        context->last_outputs.select_driver_low_current);

    const laser_controller_state_t next_state =
        laser_controller_derive_state(context, &decision);
    laser_controller_log_state_if_changed(context, next_state, now_ms);
    laser_controller_publish_runtime_status(context, now_ms);
}

static void laser_controller_run_slow_cycle(laser_controller_context_t *context)
{
    const laser_controller_time_ms_t now_ms = laser_controller_board_uptime_ms();
    const bool alignment_enabled = context->last_outputs.enable_alignment_laser;
    const bool nir_enabled = !context->last_outputs.assert_driver_standby;

    laser_controller_maybe_reconcile_pd_runtime(context, now_ms);

    if (context->summary_snapshot_valid &&
        context->last_summary_state == context->state_machine.current &&
        context->last_summary_power_tier == context->power_tier &&
        context->last_summary_fault_code == context->active_fault_code &&
        context->last_summary_alignment_enabled == alignment_enabled &&
        context->last_summary_nir_enabled == nir_enabled) {
        return;
    }

    context->summary_snapshot_valid = true;
    context->last_summary_state = context->state_machine.current;
    context->last_summary_power_tier = context->power_tier;
    context->last_summary_fault_code = context->active_fault_code;
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
        laser_controller_fault_code_name(context->active_fault_code));
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
    {
        laser_controller_power_policy_t stored_power_policy = s_context.config.power;
        laser_controller_service_get_pd_config(
            &stored_power_policy,
            NULL,
            0U,
            NULL);
        s_context.config.power = stored_power_policy;
    }
    s_context.config_valid = laser_controller_config_validate(&s_context.config);

    const laser_controller_time_ms_t now_ms = laser_controller_board_uptime_ms();
    laser_controller_state_machine_init(&s_context.state_machine, now_ms);

    s_context.boot_complete = true;
    s_context.power_tier = LASER_CONTROLLER_POWER_TIER_UNKNOWN;
    s_context.active_fault_code = LASER_CONTROLLER_FAULT_NONE;
    s_context.active_fault_class = LASER_CONTROLLER_FAULT_CLASS_NONE;
    s_context.next_pd_reconcile_ms =
        now_ms + LASER_CONTROLLER_PD_RECONCILE_BOOT_DELAY_MS;
    s_context.started = true;

    if (!s_context.config_valid) {
        s_context.fault_latched = true;
        s_context.active_fault_code = LASER_CONTROLLER_FAULT_INVALID_CONFIG;
        s_context.active_fault_class = LASER_CONTROLLER_FAULT_CLASS_SYSTEM_MAJOR;
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

    portENTER_CRITICAL(&s_runtime_status_lock);
    s_context.fault_latched = false;
    s_context.active_fault_code = LASER_CONTROLLER_FAULT_NONE;
    s_context.active_fault_class = LASER_CONTROLLER_FAULT_CLASS_NONE;
    s_context.last_fault_ms = 0U;
    portEXIT_CRITICAL(&s_runtime_status_lock);

    laser_controller_logger_log(now_ms, "fault", "fault latch cleared by explicit request");
    laser_controller_publish_runtime_status(&s_context, now_ms);
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
    portEXIT_CRITICAL(&s_context_lock);

    now_ms = laser_controller_board_uptime_ms();
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
