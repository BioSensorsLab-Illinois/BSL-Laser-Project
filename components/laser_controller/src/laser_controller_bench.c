#include "laser_controller_bench.h"

#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"

#define LASER_CONTROLLER_BENCH_MAX_CURRENT_A          5.0f
#define LASER_CONTROLLER_BENCH_MIN_TEMP_C             5.0f
#define LASER_CONTROLLER_BENCH_MAX_TEMP_C             65.0f
#define LASER_CONTROLLER_BENCH_MIN_LAMBDA_NM          771.2f
#define LASER_CONTROLLER_BENCH_MAX_LAMBDA_NM          790.0f
#define LASER_CONTROLLER_BENCH_DEFAULT_TARGET_TEMP_C  25.0f
#define LASER_CONTROLLER_BENCH_DEFAULT_TARGET_LAMBDA  785.0f
#define LASER_CONTROLLER_BENCH_DEFAULT_PWM_HZ         2000U
#define LASER_CONTROLLER_BENCH_DEFAULT_PWM_DUTY       50U
#define LASER_CONTROLLER_BENCH_MIN_PWM_HZ             0U
#define LASER_CONTROLLER_BENCH_MAX_PWM_HZ             4000U
#define LASER_CONTROLLER_BENCH_MIN_PWM_DUTY           0U
#define LASER_CONTROLLER_BENCH_MAX_PWM_DUTY           100U

static laser_controller_bench_status_t s_bench_status;
static portMUX_TYPE s_bench_lock = portMUX_INITIALIZER_UNLOCKED;

static float laser_controller_bench_clamp(float value, float minimum, float maximum)
{
    if (value < minimum) {
        return minimum;
    }

    if (value > maximum) {
        return maximum;
    }

    return value;
}

static uint32_t laser_controller_bench_clamp_u32(
    uint32_t value,
    uint32_t minimum,
    uint32_t maximum)
{
    if (value < minimum) {
        return minimum;
    }

    if (value > maximum) {
        return maximum;
    }

    return value;
}

static bool laser_controller_bench_config_has_valid_lut(const laser_controller_config_t *config)
{
    return config != NULL && config->wavelength_lut.point_count >= 2U;
}

static laser_controller_nm_t laser_controller_bench_lambda_from_temp(
    const laser_controller_config_t *config,
    laser_controller_celsius_t target_temp_c)
{
    if (laser_controller_bench_config_has_valid_lut(config)) {
        for (uint8_t index = 1U; index < config->wavelength_lut.point_count; ++index) {
            const float temp_low = config->wavelength_lut.target_temp_c[index - 1U];
            const float temp_high = config->wavelength_lut.target_temp_c[index];

            if (target_temp_c <= temp_high) {
                const float lambda_low = config->wavelength_lut.wavelength_nm[index - 1U];
                const float lambda_high = config->wavelength_lut.wavelength_nm[index];
                const float span = temp_high - temp_low;
                const float ratio = span > 0.0f ? (target_temp_c - temp_low) / span : 0.0f;
                return lambda_low + ((lambda_high - lambda_low) * ratio);
            }
        }

        return config->wavelength_lut.wavelength_nm[config->wavelength_lut.point_count - 1U];
    }

    return LASER_CONTROLLER_BENCH_MIN_LAMBDA_NM +
           ((target_temp_c - LASER_CONTROLLER_BENCH_MIN_TEMP_C) *
            (LASER_CONTROLLER_BENCH_MAX_LAMBDA_NM - LASER_CONTROLLER_BENCH_MIN_LAMBDA_NM) /
            (LASER_CONTROLLER_BENCH_MAX_TEMP_C - LASER_CONTROLLER_BENCH_MIN_TEMP_C));
}

static laser_controller_celsius_t laser_controller_bench_temp_from_lambda(
    const laser_controller_config_t *config,
    laser_controller_nm_t target_lambda_nm)
{
    if (laser_controller_bench_config_has_valid_lut(config)) {
        for (uint8_t index = 1U; index < config->wavelength_lut.point_count; ++index) {
            const float lambda_low = config->wavelength_lut.wavelength_nm[index - 1U];
            const float lambda_high = config->wavelength_lut.wavelength_nm[index];

            if (target_lambda_nm <= lambda_high) {
                const float temp_low = config->wavelength_lut.target_temp_c[index - 1U];
                const float temp_high = config->wavelength_lut.target_temp_c[index];
                const float span = lambda_high - lambda_low;
                const float ratio = fabsf(span) > 0.0f ?
                    (target_lambda_nm - lambda_low) / span : 0.0f;
                return temp_low + ((temp_high - temp_low) * ratio);
            }
        }

        return config->wavelength_lut.target_temp_c[config->wavelength_lut.point_count - 1U];
    }

    return LASER_CONTROLLER_BENCH_MIN_TEMP_C +
           ((target_lambda_nm - LASER_CONTROLLER_BENCH_MIN_LAMBDA_NM) *
            (LASER_CONTROLLER_BENCH_MAX_TEMP_C - LASER_CONTROLLER_BENCH_MIN_TEMP_C) /
            (LASER_CONTROLLER_BENCH_MAX_LAMBDA_NM - LASER_CONTROLLER_BENCH_MIN_LAMBDA_NM));
}

static laser_controller_amps_t laser_controller_bench_max_current_allowed(
    const laser_controller_config_t *config)
{
    if (config != NULL &&
        config->thresholds.max_laser_current_a > 0.0f &&
        config->thresholds.max_laser_current_a < LASER_CONTROLLER_BENCH_MAX_CURRENT_A) {
        return config->thresholds.max_laser_current_a;
    }

    return LASER_CONTROLLER_BENCH_MAX_CURRENT_A;
}

void laser_controller_bench_init_defaults(void)
{
    portENTER_CRITICAL(&s_bench_lock);
    memset(&s_bench_status, 0, sizeof(s_bench_status));
    s_bench_status.target_mode = LASER_CONTROLLER_BENCH_TARGET_MODE_LAMBDA;
    s_bench_status.runtime_mode = LASER_CONTROLLER_RUNTIME_MODE_MODULATED_HOST;
    s_bench_status.modulation_frequency_hz = LASER_CONTROLLER_BENCH_DEFAULT_PWM_HZ;
    s_bench_status.modulation_duty_cycle_pct = LASER_CONTROLLER_BENCH_DEFAULT_PWM_DUTY;
    s_bench_status.target_temp_c = LASER_CONTROLLER_BENCH_DEFAULT_TARGET_TEMP_C;
    s_bench_status.target_lambda_nm = LASER_CONTROLLER_BENCH_DEFAULT_TARGET_LAMBDA;
    portEXIT_CRITICAL(&s_bench_lock);
}

void laser_controller_bench_copy_status(laser_controller_bench_status_t *status)
{
    if (status == NULL) {
        return;
    }

    portENTER_CRITICAL(&s_bench_lock);
    *status = s_bench_status;
    portEXIT_CRITICAL(&s_bench_lock);
}

void laser_controller_bench_clear_requests(laser_controller_time_ms_t now_ms)
{
    (void)now_ms;
    portENTER_CRITICAL(&s_bench_lock);
    s_bench_status.requested_alignment = false;
    s_bench_status.requested_nir = false;
    portEXIT_CRITICAL(&s_bench_lock);
}

void laser_controller_bench_set_alignment_requested(
    bool enable,
    laser_controller_time_ms_t now_ms)
{
    (void)now_ms;
    portENTER_CRITICAL(&s_bench_lock);
    s_bench_status.requested_alignment = enable;
    if (enable) {
        s_bench_status.requested_nir = false;
    }
    portEXIT_CRITICAL(&s_bench_lock);
}

void laser_controller_bench_set_nir_requested(
    bool enable,
    laser_controller_time_ms_t now_ms)
{
    (void)now_ms;
    portENTER_CRITICAL(&s_bench_lock);
    s_bench_status.requested_nir = enable;
    if (enable) {
        s_bench_status.requested_alignment = false;
    }
    portEXIT_CRITICAL(&s_bench_lock);
}

void laser_controller_bench_set_laser_current_a(
    const laser_controller_config_t *config,
    laser_controller_amps_t current_a,
    laser_controller_time_ms_t now_ms)
{
    (void)now_ms;
    portENTER_CRITICAL(&s_bench_lock);
    s_bench_status.high_state_current_a = laser_controller_bench_clamp(
        current_a,
        0.0f,
        laser_controller_bench_max_current_allowed(config));
    if (s_bench_status.low_state_current_a > s_bench_status.high_state_current_a) {
        s_bench_status.low_state_current_a = s_bench_status.high_state_current_a;
    }
    portEXIT_CRITICAL(&s_bench_lock);
}

void laser_controller_bench_set_target_temp_c(
    const laser_controller_config_t *config,
    laser_controller_celsius_t target_temp_c,
    laser_controller_time_ms_t now_ms)
{
    (void)now_ms;
    portENTER_CRITICAL(&s_bench_lock);
    s_bench_status.target_mode = LASER_CONTROLLER_BENCH_TARGET_MODE_TEMP;
    s_bench_status.target_temp_c = laser_controller_bench_clamp(
        target_temp_c,
        LASER_CONTROLLER_BENCH_MIN_TEMP_C,
        LASER_CONTROLLER_BENCH_MAX_TEMP_C);
    s_bench_status.target_lambda_nm = laser_controller_bench_lambda_from_temp(
        config,
        s_bench_status.target_temp_c);
    portEXIT_CRITICAL(&s_bench_lock);
}

void laser_controller_bench_set_target_lambda_nm(
    const laser_controller_config_t *config,
    laser_controller_nm_t target_lambda_nm,
    laser_controller_time_ms_t now_ms)
{
    (void)now_ms;
    portENTER_CRITICAL(&s_bench_lock);
    s_bench_status.target_mode = LASER_CONTROLLER_BENCH_TARGET_MODE_LAMBDA;
    s_bench_status.target_lambda_nm = laser_controller_bench_clamp(
        target_lambda_nm,
        LASER_CONTROLLER_BENCH_MIN_LAMBDA_NM,
        LASER_CONTROLLER_BENCH_MAX_LAMBDA_NM);
    s_bench_status.target_temp_c = laser_controller_bench_temp_from_lambda(
        config,
        s_bench_status.target_lambda_nm);
    portEXIT_CRITICAL(&s_bench_lock);
}

void laser_controller_bench_set_modulation(
    bool enabled,
    uint32_t frequency_hz,
    uint32_t duty_cycle_pct,
    laser_controller_time_ms_t now_ms)
{
    (void)now_ms;
    portENTER_CRITICAL(&s_bench_lock);
    s_bench_status.modulation_enabled = enabled;
    s_bench_status.modulation_frequency_hz = laser_controller_bench_clamp_u32(
        frequency_hz,
        LASER_CONTROLLER_BENCH_MIN_PWM_HZ,
        LASER_CONTROLLER_BENCH_MAX_PWM_HZ);
    s_bench_status.modulation_duty_cycle_pct = laser_controller_bench_clamp_u32(
        duty_cycle_pct,
        LASER_CONTROLLER_BENCH_MIN_PWM_DUTY,
        LASER_CONTROLLER_BENCH_MAX_PWM_DUTY);
    s_bench_status.low_state_current_a = 0.0f;
    portEXIT_CRITICAL(&s_bench_lock);
}

void laser_controller_bench_set_runtime_mode(
    laser_controller_runtime_mode_t runtime_mode,
    laser_controller_time_ms_t now_ms)
{
    (void)now_ms;
    portENTER_CRITICAL(&s_bench_lock);
    s_bench_status.runtime_mode =
        runtime_mode == LASER_CONTROLLER_RUNTIME_MODE_BINARY_TRIGGER ?
            LASER_CONTROLLER_RUNTIME_MODE_BINARY_TRIGGER :
            LASER_CONTROLLER_RUNTIME_MODE_MODULATED_HOST;
    s_bench_status.requested_alignment = false;
    s_bench_status.requested_nir = false;
    if (s_bench_status.runtime_mode == LASER_CONTROLLER_RUNTIME_MODE_BINARY_TRIGGER) {
        s_bench_status.modulation_enabled = false;
        s_bench_status.low_state_current_a = 0.0f;
    }
    portEXIT_CRITICAL(&s_bench_lock);
}

const char *laser_controller_bench_target_mode_name(
    laser_controller_bench_target_mode_t target_mode)
{
    switch (target_mode) {
        case LASER_CONTROLLER_BENCH_TARGET_MODE_TEMP:
            return "temp";
        case LASER_CONTROLLER_BENCH_TARGET_MODE_LAMBDA:
            return "lambda";
        default:
            return "lambda";
    }
}

const char *laser_controller_runtime_mode_name(
    laser_controller_runtime_mode_t runtime_mode)
{
    switch (runtime_mode) {
        case LASER_CONTROLLER_RUNTIME_MODE_BINARY_TRIGGER:
            return "binary_trigger";
        case LASER_CONTROLLER_RUNTIME_MODE_MODULATED_HOST:
            return "modulated_host";
        default:
            return "modulated_host";
    }
}
