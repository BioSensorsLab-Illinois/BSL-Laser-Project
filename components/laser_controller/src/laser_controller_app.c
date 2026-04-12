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
#define LASER_CONTROLLER_DEPLOYMENT_STABILIZE_MS    300U
#define LASER_CONTROLLER_DEPLOYMENT_PD_WAIT_MS      1200U
#define LASER_CONTROLLER_DEPLOYMENT_OWNERSHIP_WAIT_MS 1200U
#define LASER_CONTROLLER_DEPLOYMENT_OUTPUTS_WAIT_MS  800U
#define LASER_CONTROLLER_DEPLOYMENT_DAC_WAIT_MS      800U
#define LASER_CONTROLLER_DEPLOYMENT_PERIPHERAL_WAIT_MS 2500U
#define LASER_CONTROLLER_DEPLOYMENT_READY_WAIT_MS    1200U
#define LASER_CONTROLLER_DEPLOYMENT_STEP_DELAY_MS    300U
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
    bool deployment_force_ld_safe;
    bool deployment_tec_contradiction_reported;
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

static laser_controller_board_outputs_t laser_controller_safe_outputs(void)
{
    return (laser_controller_board_outputs_t){
        .enable_ld_vin = false,
        .enable_tec_vin = false,
        .enable_haptic_driver = false,
        .enable_alignment_laser = false,
        .assert_driver_standby = true,
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
    const float tec_volts_per_c =
        config != NULL && config->analog.tec_command_volts_per_c > 0.0f ?
            config->analog.tec_command_volts_per_c :
            (LASER_CONTROLLER_DEPLOYMENT_DAC_FULL_SCALE_V / 65.0f);
    float voltage_v = target_temp_c * tec_volts_per_c;

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
           context->last_outputs.assert_driver_standby &&
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
           !context->last_outputs.assert_driver_standby &&
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

        if (!context->deployment_force_ld_safe &&
            !context->last_outputs.assert_driver_standby &&
            !context->last_outputs.select_driver_low_current &&
            !context->last_inputs.driver_loop_good) {
            laser_controller_deployment_invalidate_ready(
                context,
                now_ms,
                LASER_CONTROLLER_FAULT_LD_LOOP_BAD,
                "deployment loop-good invalidated");
        }
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
        case LASER_CONTROLLER_DEPLOYMENT_STEP_PERIPHERALS_VERIFY:
            if (context->last_inputs.imu_readback.reachable &&
                context->last_inputs.imu_readback.configured &&
                context->last_inputs.imu_readback.who_am_i == 0x6CU &&
                context->last_inputs.tof_readback.reachable &&
                context->last_inputs.tof_readback.configured &&
                context->last_inputs.tof_readback.boot_state != 0U &&
                context->last_inputs.tof_readback.sensor_id == 0xEACCU &&
                context->last_inputs.haptic_readback.reachable) {
                laser_controller_deployment_complete_current_step(context, now_ms);
            } else if ((now_ms - context->deployment_step_started_ms) >=
                       LASER_CONTROLLER_DEPLOYMENT_PERIPHERAL_WAIT_MS) {
                laser_controller_deployment_fail(
                    context,
                    now_ms,
                    !context->last_inputs.imu_readback.reachable ||
                            context->last_inputs.imu_readback.who_am_i != 0x6CU ?
                        LASER_CONTROLLER_FAULT_IMU_INVALID :
                    !context->last_inputs.tof_readback.reachable ||
                            context->last_inputs.tof_readback.sensor_id != 0xEACCU ?
                        LASER_CONTROLLER_FAULT_TOF_INVALID :
                        LASER_CONTROLLER_FAULT_SERVICE_OVERRIDE_REJECTED,
                    "one or more deployment peripherals failed readback verification");
            }
            break;
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
        case LASER_CONTROLLER_DEPLOYMENT_STEP_READY_POSTURE:
            if (context->deployment_substep == 0U) {
                if (context->last_inputs.ld_rail_pgood &&
                    context->last_inputs.tec_rail_pgood &&
                    context->last_inputs.driver_loop_good &&
                    laser_controller_deployment_pin_is_high(
                        &context->last_inputs,
                        LASER_CONTROLLER_GPIO_LD_SBDN) &&
                    laser_controller_deployment_pin_is_high(
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
                        "deployment ready test could not validate LPGD with driver enabled");
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
    esp_err_t err;

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

    err = laser_controller_service_apply_saved_pd_runtime(now_ms);
    if (err == ESP_OK) {
        laser_controller_board_force_pd_refresh_with_source(
            LASER_CONTROLLER_PD_SNAPSHOT_SOURCE_BOOT_RECONCILE);
    }
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
        context->latched_fault_code = fault_code;
        context->latched_fault_class = fault_class;
        context->fault_latched = true;
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
    s_runtime_status.active_fault_count = context->active_fault_count;
    s_runtime_status.trip_counter = context->trip_counter;
    s_runtime_status.last_fault_ms = context->last_fault_ms;
    s_runtime_status.inputs = context->last_inputs;
    s_runtime_status.outputs = context->last_outputs;
    s_runtime_status.decision = context->last_decision;
    s_runtime_status.config = config_snapshot;
    s_runtime_status.deployment = context->deployment;
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

    if (!context->boot_complete || !context->config_valid) {
        return outputs;
    }

    if (context->state_machine.current == LASER_CONTROLLER_STATE_SERVICE_MODE) {
        laser_controller_service_get_service_output_requests(
            &outputs.enable_ld_vin,
            &outputs.enable_tec_vin,
            &outputs.enable_haptic_driver);
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
                    return outputs;
                case LASER_CONTROLLER_DEPLOYMENT_STEP_TEC_SETTLE:
                    outputs.enable_tec_vin = true;
                    outputs.enable_ld_vin = true;
                    return outputs;
                case LASER_CONTROLLER_DEPLOYMENT_STEP_READY_POSTURE:
                    outputs.enable_tec_vin = true;
                    outputs.enable_ld_vin = true;
                    outputs.assert_driver_standby = false;
                    outputs.select_driver_low_current =
                        context->deployment_substep != 0U;
                    return outputs;
                default:
                    return outputs;
            }
        }

        if (context->deployment_force_ld_safe &&
            context->deployment.ready &&
            !context->deployment.running) {
            outputs.enable_tec_vin = true;
            outputs.enable_ld_vin = false;
            outputs.enable_alignment_laser = false;
            outputs.assert_driver_standby = true;
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
             * transient post-ready faults.
             */
            outputs.enable_tec_vin = true;
            outputs.enable_ld_vin = false;
            outputs.enable_alignment_laser = false;
            outputs.assert_driver_standby = true;
            outputs.select_driver_low_current = true;
            return outputs;
        }

        if (!context->deployment.ready || context->deployment.failed) {
            return outputs;
        }

        outputs.enable_tec_vin = true;
        outputs.enable_ld_vin = true;
        outputs.enable_alignment_laser = decision->alignment_output_enable;
        outputs.assert_driver_standby = false;
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

    if ((!interlocks_disabled && laser_controller_fault_latch_active(context)) ||
        laser_controller_service_mode_requested()) {
        return outputs;
    }

    outputs.enable_alignment_laser = decision->alignment_output_enable;
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
    snapshot.driver_operate_expected =
        !context->last_outputs.assert_driver_standby &&
        !context->last_outputs.select_driver_low_current;
    snapshot.ready_idle_bias_allowed =
        context->deployment.active &&
        context->deployment.ready_idle &&
        context->deployment.ready &&
        context->deployment.ready_truth.sbdn_high &&
        context->deployment.ready_truth.pcn_low;
    snapshot.power_tier = context->power_tier;
    snapshot.host_request_alignment = bench_status.requested_alignment;
    snapshot.host_request_nir =
        context->deployment.active && context->deployment.ready &&
        bench_status.runtime_mode ==
            LASER_CONTROLLER_RUNTIME_MODE_MODULATED_HOST &&
        bench_status.requested_nir;
    snapshot.target_lambda_nm = laser_controller_deployment_target_lambda_nm(
        context,
        &bench_status);
    snapshot.actual_lambda_nm = laser_controller_lambda_from_temp(
        &config_snapshot,
        context->last_inputs.tec_temp_c);
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
        (void)laser_controller_board_set_runtime_tof_illumination(
            !laser_controller_service_mode_requested() &&
                bench_status.illumination_enabled,
            bench_status.illumination_duty_cycle_pct,
            bench_status.illumination_frequency_hz);
    }

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
    s_context.power_tier = LASER_CONTROLLER_POWER_TIER_UNKNOWN;
    s_context.active_fault_code = LASER_CONTROLLER_FAULT_NONE;
    s_context.active_fault_class = LASER_CONTROLLER_FAULT_CLASS_NONE;
    s_context.latched_fault_code = LASER_CONTROLLER_FAULT_NONE;
    s_context.latched_fault_class = LASER_CONTROLLER_FAULT_CLASS_NONE;
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

    portENTER_CRITICAL(&s_runtime_status_lock);
    s_context.fault_latched = false;
    s_context.active_fault_code = LASER_CONTROLLER_FAULT_NONE;
    s_context.active_fault_class = LASER_CONTROLLER_FAULT_CLASS_NONE;
    s_context.latched_fault_code = LASER_CONTROLLER_FAULT_NONE;
    s_context.latched_fault_class = LASER_CONTROLLER_FAULT_CLASS_NONE;
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

    if (!s_context.started || !s_context.deployment.active ||
        s_context.deployment.running) {
        return ESP_ERR_INVALID_STATE;
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

    portENTER_CRITICAL(&s_context_lock);
    s_context.deployment.active = false;
    s_context.deployment.running = false;
    s_context.deployment.ready = false;
    s_context.deployment.ready_idle = false;
    s_context.deployment.ready_qualified = false;
    s_context.deployment.ready_invalidated = false;
    s_context.deployment.failed = false;
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

    laser_controller_logger_log(
        now_ms,
        "deploy",
        "deployment mode exited; controller returned to non-deployed safe supervision");
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
