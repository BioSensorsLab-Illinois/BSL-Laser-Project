#include "laser_controller_service.h"

#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"

#include "laser_controller_board.h"
#include "laser_controller_deployment.h"

#define LASER_CONTROLLER_STUSB4500_ADDR 0x28U
#define LASER_CONTROLLER_DAC80502_ADDR  0x48U
#define LASER_CONTROLLER_DRV2605_ADDR   0x5AU
#define LASER_CONTROLLER_LSM6DSO_WHOAMI 0x6CU
#define LASER_CONTROLLER_SERVICE_NVS_NAMESPACE "laser_ctrl"
#define LASER_CONTROLLER_SERVICE_NVS_KEY       "svc_profile"
#define LASER_CONTROLLER_SERVICE_PROFILE_VER   6U
#define LASER_CONTROLLER_SERVICE_PROFILE_VER_MIN 3U
#define LASER_CONTROLLER_SERVICE_DAC_MAX_V     2.5f
#define LASER_CONTROLLER_SERVICE_DEFAULT_TEC_CHANNEL_V 0.821104f
#define LASER_CONTROLLER_SERVICE_TOF_ILLUMINATION_PWM_HZ 20000U
#define LASER_CONTROLLER_SERVICE_HAPTIC_PATTERN_MAX_PULSES 12U
#define LASER_CONTROLLER_SERVICE_HAPTIC_PATTERN_MIN_MS     10U
#define LASER_CONTROLLER_SERVICE_HAPTIC_PATTERN_MAX_MS     600U

typedef struct {
    bool expected_present;
    bool debug_enabled;
} laser_controller_service_persisted_module_t;

typedef struct {
    uint32_t version;
    char profile_name[LASER_CONTROLLER_SERVICE_PROFILE_NAME_LEN];
    laser_controller_service_persisted_module_t
        modules[LASER_CONTROLLER_MODULE_COUNT];
    float dac_ld_channel_v;
    float dac_tec_channel_v;
    laser_controller_service_dac_reference_t dac_reference;
    bool dac_gain_2x;
    bool dac_ref_div;
    laser_controller_service_dac_sync_t dac_sync_mode;
    uint32_t imu_odr_hz;
    uint32_t imu_accel_range_g;
    uint32_t imu_gyro_range_dps;
    bool imu_gyro_enabled;
    bool imu_lpf2_enabled;
    bool imu_timestamp_enabled;
    bool imu_bdu_enabled;
    bool imu_if_inc_enabled;
    bool imu_i2c_disabled;
    float tof_min_range_m;
    float tof_max_range_m;
    uint32_t tof_stale_timeout_ms;
    laser_controller_service_pd_profile_t
        pd_profiles[LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT];
    float pd_programming_only_max_w;
    float pd_reduced_mode_min_w;
    float pd_reduced_mode_max_w;
    float pd_full_mode_min_w;
    bool pd_firmware_plan_enabled;
    uint32_t haptic_effect_id;
    laser_controller_service_haptic_mode_t haptic_mode;
    uint32_t haptic_library;
    laser_controller_service_haptic_actuator_t haptic_actuator;
    uint32_t haptic_rtp_level;
} laser_controller_service_persisted_profile_v3_t;

typedef struct {
    uint32_t version;
    char profile_name[LASER_CONTROLLER_SERVICE_PROFILE_NAME_LEN];
    bool ld_rail_debug_enabled;
    bool tec_rail_debug_enabled;
    laser_controller_service_persisted_module_t
        modules[LASER_CONTROLLER_MODULE_COUNT];
    float dac_ld_channel_v;
    float dac_tec_channel_v;
    laser_controller_service_dac_reference_t dac_reference;
    bool dac_gain_2x;
    bool dac_ref_div;
    laser_controller_service_dac_sync_t dac_sync_mode;
    uint32_t imu_odr_hz;
    uint32_t imu_accel_range_g;
    uint32_t imu_gyro_range_dps;
    bool imu_gyro_enabled;
    bool imu_lpf2_enabled;
    bool imu_timestamp_enabled;
    bool imu_bdu_enabled;
    bool imu_if_inc_enabled;
    bool imu_i2c_disabled;
    float tof_min_range_m;
    float tof_max_range_m;
    uint32_t tof_stale_timeout_ms;
    laser_controller_service_pd_profile_t
        pd_profiles[LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT];
    float pd_programming_only_max_w;
    float pd_reduced_mode_min_w;
    float pd_reduced_mode_max_w;
    float pd_full_mode_min_w;
    bool pd_firmware_plan_enabled;
    uint32_t haptic_effect_id;
    laser_controller_service_haptic_mode_t haptic_mode;
    uint32_t haptic_library;
    laser_controller_service_haptic_actuator_t haptic_actuator;
    uint32_t haptic_rtp_level;
} laser_controller_service_persisted_profile_v4_t;

typedef struct {
    uint32_t version;
    char profile_name[LASER_CONTROLLER_SERVICE_PROFILE_NAME_LEN];
    bool ld_rail_debug_enabled;
    bool tec_rail_debug_enabled;
    laser_controller_service_persisted_module_t
        modules[LASER_CONTROLLER_MODULE_COUNT];
    float dac_ld_channel_v;
    float dac_tec_channel_v;
    laser_controller_service_dac_reference_t dac_reference;
    bool dac_gain_2x;
    bool dac_ref_div;
    laser_controller_service_dac_sync_t dac_sync_mode;
    uint32_t imu_odr_hz;
    uint32_t imu_accel_range_g;
    uint32_t imu_gyro_range_dps;
    bool imu_gyro_enabled;
    bool imu_lpf2_enabled;
    bool imu_timestamp_enabled;
    bool imu_bdu_enabled;
    bool imu_if_inc_enabled;
    bool imu_i2c_disabled;
    float tof_min_range_m;
    float tof_max_range_m;
    uint32_t tof_stale_timeout_ms;
    laser_controller_service_pd_profile_t
        pd_profiles[LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT];
    float pd_programming_only_max_w;
    float pd_reduced_mode_min_w;
    float pd_reduced_mode_max_w;
    float pd_full_mode_min_w;
    bool pd_firmware_plan_enabled;
    uint32_t haptic_effect_id;
    laser_controller_service_haptic_mode_t haptic_mode;
    uint32_t haptic_library;
    laser_controller_service_haptic_actuator_t haptic_actuator;
    uint32_t haptic_rtp_level;
    laser_controller_safety_thresholds_t safety_thresholds;
    laser_controller_timeout_policy_t safety_timeouts;
} laser_controller_service_persisted_profile_v5_t;

typedef struct {
    uint32_t version;
    char profile_name[LASER_CONTROLLER_SERVICE_PROFILE_NAME_LEN];
    bool ld_rail_debug_enabled;
    bool tec_rail_debug_enabled;
    laser_controller_service_persisted_module_t
        modules[LASER_CONTROLLER_MODULE_COUNT];
    float dac_ld_channel_v;
    float dac_tec_channel_v;
    laser_controller_service_dac_reference_t dac_reference;
    bool dac_gain_2x;
    bool dac_ref_div;
    laser_controller_service_dac_sync_t dac_sync_mode;
    uint32_t imu_odr_hz;
    uint32_t imu_accel_range_g;
    uint32_t imu_gyro_range_dps;
    bool imu_gyro_enabled;
    bool imu_lpf2_enabled;
    bool imu_timestamp_enabled;
    bool imu_bdu_enabled;
    bool imu_if_inc_enabled;
    bool imu_i2c_disabled;
    float tof_min_range_m;
    float tof_max_range_m;
    uint32_t tof_stale_timeout_ms;
    laser_controller_service_pd_profile_t
        pd_profiles[LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT];
    float pd_programming_only_max_w;
    float pd_reduced_mode_min_w;
    float pd_reduced_mode_max_w;
    float pd_full_mode_min_w;
    bool pd_firmware_plan_enabled;
    uint32_t haptic_effect_id;
    laser_controller_service_haptic_mode_t haptic_mode;
    uint32_t haptic_library;
    laser_controller_service_haptic_actuator_t haptic_actuator;
    uint32_t haptic_rtp_level;
    laser_controller_safety_thresholds_t safety_thresholds;
    laser_controller_timeout_policy_t safety_timeouts;
    laser_controller_bench_target_mode_t runtime_target_mode;
    laser_controller_celsius_t runtime_target_temp_c;
    laser_controller_nm_t runtime_target_lambda_nm;
} laser_controller_service_persisted_profile_t;

typedef struct {
    laser_controller_service_status_t status;
    uint8_t stusb4500_regs[256];
    uint8_t dac80502_regs[256];
    uint8_t drv2605_regs[256];
    uint8_t imu_regs[256];
} laser_controller_service_context_t;

static laser_controller_service_context_t s_service;
static portMUX_TYPE s_service_lock = portMUX_INITIALIZER_UNLOCKED;

static float laser_controller_service_clamp_float(float value, float minimum, float maximum)
{
    if (value < minimum) {
        return minimum;
    }

    if (value > maximum) {
        return maximum;
    }

    return value;
}

static void laser_controller_service_load_default_runtime_safety(
    laser_controller_safety_thresholds_t *thresholds,
    laser_controller_timeout_policy_t *timeouts)
{
    laser_controller_config_t config = { 0 };

    laser_controller_config_load_defaults(&config);
    if (thresholds != NULL) {
        *thresholds = config.thresholds;
    }
    if (timeouts != NULL) {
        *timeouts = config.timeouts;
    }
}

static void laser_controller_service_load_default_runtime_target(
    laser_controller_bench_target_mode_t *target_mode,
    laser_controller_celsius_t *target_temp_c,
    laser_controller_nm_t *target_lambda_nm)
{
    if (target_mode != NULL) {
        *target_mode = LASER_CONTROLLER_BENCH_TARGET_MODE_TEMP;
    }
    if (target_temp_c != NULL) {
        *target_temp_c = 25.0f;
    }
    if (target_lambda_nm != NULL) {
        *target_lambda_nm = 785.0f;
    }
}

static bool laser_controller_service_pd_config_valid(
    const laser_controller_power_policy_t *power_policy,
    const laser_controller_service_pd_profile_t *profiles,
    size_t profile_count);
static void laser_controller_service_sync_shadow_regs_locked(void);
static void laser_controller_service_migrate_v3_profile(
    const laser_controller_service_persisted_profile_v3_t *legacy,
    laser_controller_service_persisted_profile_t *profile);
static void laser_controller_service_migrate_v4_profile(
    const laser_controller_service_persisted_profile_v4_t *legacy,
    laser_controller_service_persisted_profile_t *profile);
static void laser_controller_service_migrate_v5_profile(
    const laser_controller_service_persisted_profile_v5_t *legacy,
    laser_controller_service_persisted_profile_t *profile);
static void laser_controller_service_normalize_pd_profiles(
    const laser_controller_service_pd_profile_t *profiles,
    laser_controller_service_pd_profile_t *normalized_profiles);
static void laser_controller_service_sanitize_dac_settings_locked(void);
static void laser_controller_service_refresh_gpio_override_count_locked(void);

static bool laser_controller_service_i2c_address_to_module(
    uint32_t address,
    laser_controller_module_t *module)
{
    if (module == NULL) {
        return false;
    }

    switch (address) {
        case LASER_CONTROLLER_STUSB4500_ADDR:
            *module = LASER_CONTROLLER_MODULE_PD;
            return true;
        case LASER_CONTROLLER_DAC80502_ADDR:
            *module = LASER_CONTROLLER_MODULE_DAC;
            return true;
        case LASER_CONTROLLER_DRV2605_ADDR:
            *module = LASER_CONTROLLER_MODULE_HAPTIC;
            return true;
        default:
            return false;
    }
}

static void laser_controller_service_update_module_probe_locked(
    uint32_t address,
    bool acknowledged)
{
    laser_controller_module_t module;

    if (!laser_controller_service_i2c_address_to_module(address, &module)) {
        return;
    }

    s_service.status.modules[module].detected = acknowledged;
    s_service.status.modules[module].healthy = acknowledged;
}

static esp_err_t laser_controller_service_i2c_probe(uint32_t address)
{
    return laser_controller_board_i2c_probe(address);
}

static esp_err_t laser_controller_service_i2c_read_reg_u8(
    uint32_t address,
    uint32_t reg,
    uint8_t *value)
{
    const uint8_t register_address = (uint8_t)(reg & 0xFFU);

    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return laser_controller_board_i2c_write_read(
        address,
        &register_address,
        1U,
        value,
        1U);
}

static esp_err_t laser_controller_service_i2c_write_reg_u8(
    uint32_t address,
    uint32_t reg,
    uint32_t value)
{
    uint8_t payload[2];

    payload[0] = (uint8_t)(reg & 0xFFU);
    payload[1] = (uint8_t)(value & 0xFFU);

    return laser_controller_board_i2c_write(
        address,
        payload,
        sizeof(payload));
}

static esp_err_t laser_controller_service_dac_read_reg(
    uint32_t reg,
    uint16_t *value)
{
    const uint8_t command = (uint8_t)(reg & 0xFFU);
    uint8_t rx[2] = { 0 };
    esp_err_t err;

    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    err = laser_controller_board_i2c_write_read(
        LASER_CONTROLLER_DAC80502_ADDR,
        &command,
        1U,
        rx,
        sizeof(rx));
    if (err != ESP_OK) {
        return err;
    }

    *value = (uint16_t)(((uint16_t)rx[0] << 8U) | rx[1]);
    return ESP_OK;
}

static esp_err_t laser_controller_service_dac_write_reg(
    uint32_t reg,
    uint32_t value)
{
    uint8_t tx[3];

    tx[0] = (uint8_t)(reg & 0xFFU);
    tx[1] = (uint8_t)((value >> 8U) & 0xFFU);
    tx[2] = (uint8_t)(value & 0xFFU);

    return laser_controller_board_i2c_write(
        LASER_CONTROLLER_DAC80502_ADDR,
        tx,
        sizeof(tx));
}

static bool laser_controller_service_pd_profile_valid(
    const laser_controller_service_pd_profile_t *profile)
{
    if (profile == NULL) {
        return false;
    }

    if (!profile->enabled) {
        return true;
    }

    return profile->voltage_v > 0.0f &&
           profile->voltage_v <= 20.0f &&
           profile->current_a > 0.0f &&
           profile->current_a <= 5.0f;
}

static bool laser_controller_service_pd_config_valid(
    const laser_controller_power_policy_t *power_policy,
    const laser_controller_service_pd_profile_t *profiles,
    size_t profile_count)
{
    if (power_policy == NULL || profiles == NULL ||
        profile_count != LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT) {
        return false;
    }

    if (power_policy->programming_only_max_w <= 0.0f) {
        return false;
    }

    if (power_policy->reduced_mode_min_w <
        power_policy->programming_only_max_w) {
        return false;
    }

    if (power_policy->reduced_mode_max_w <
        power_policy->reduced_mode_min_w) {
        return false;
    }

    if (power_policy->full_mode_min_w <=
        power_policy->reduced_mode_max_w) {
        return false;
    }

    for (size_t index = 0U; index < profile_count; ++index) {
        if (!laser_controller_service_pd_profile_valid(&profiles[index])) {
            return false;
        }
    }

    return true;
}

static void laser_controller_service_normalize_pd_profiles(
    const laser_controller_service_pd_profile_t *profiles,
    laser_controller_service_pd_profile_t *normalized_profiles)
{
    if (normalized_profiles == NULL) {
        return;
    }

    memset(
        normalized_profiles,
        0,
        sizeof(*normalized_profiles) * LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT);

    normalized_profiles[0].enabled = true;
    normalized_profiles[0].voltage_v = 5.0f;
    normalized_profiles[0].current_a =
        profiles != NULL && profiles[0].current_a > 0.0f ?
            profiles[0].current_a :
            3.0f;

    if (profiles != NULL && profiles[1].enabled) {
        normalized_profiles[1] = profiles[1];
    }

    if (profiles != NULL && normalized_profiles[1].enabled && profiles[2].enabled) {
        normalized_profiles[2] = profiles[2];
    }
}

static uint8_t laser_controller_service_haptic_mode_reg_value(
    laser_controller_service_haptic_mode_t mode)
{
    switch (mode) {
        case LASER_CONTROLLER_SERVICE_HAPTIC_MODE_INTERNAL_TRIGGER:
            return 0x00U;
        case LASER_CONTROLLER_SERVICE_HAPTIC_MODE_EXTERNAL_EDGE:
            return 0x01U;
        case LASER_CONTROLLER_SERVICE_HAPTIC_MODE_EXTERNAL_LEVEL:
            return 0x02U;
        case LASER_CONTROLLER_SERVICE_HAPTIC_MODE_PWM_ANALOG:
            return 0x03U;
        case LASER_CONTROLLER_SERVICE_HAPTIC_MODE_AUDIO_TO_VIBE:
            return 0x04U;
        case LASER_CONTROLLER_SERVICE_HAPTIC_MODE_RTP:
            return 0x05U;
        case LASER_CONTROLLER_SERVICE_HAPTIC_MODE_DIAGNOSTICS:
            return 0x06U;
        case LASER_CONTROLLER_SERVICE_HAPTIC_MODE_AUTO_CAL:
            return 0x07U;
        default:
            return 0x00U;
    }
}

static void laser_controller_service_copy_text(
    char *destination,
    size_t destination_len,
    const char *source)
{
    if (destination == NULL || destination_len == 0U) {
        return;
    }

    if (source == NULL) {
        destination[0] = '\0';
        return;
    }

    (void)snprintf(destination, destination_len, "%s", source);
}

static void laser_controller_service_write_action_locked(
    const char *message,
    laser_controller_time_ms_t now_ms)
{
    (void)now_ms;
    laser_controller_service_copy_text(
        s_service.status.last_action,
        sizeof(s_service.status.last_action),
        message);
}

static void laser_controller_service_refresh_gpio_override_count_locked(void)
{
    uint32_t count = 0U;

    for (uint32_t gpio_num = 0U; gpio_num < LASER_CONTROLLER_SERVICE_GPIO_COUNT; ++gpio_num) {
        if (s_service.status.gpio_overrides[gpio_num].active) {
            ++count;
        }
    }

    s_service.status.gpio_override_count = count;
}

static void laser_controller_service_sanitize_dac_settings_locked(void)
{
    if (s_service.status.dac_reference ==
            LASER_CONTROLLER_SERVICE_DAC_REFERENCE_INTERNAL &&
        !s_service.status.dac_ref_div) {
        s_service.status.dac_ref_div = true;
    }

    s_service.status.dac_ld_channel_v =
        laser_controller_service_clamp_float(
            s_service.status.dac_ld_channel_v,
            0.0f,
            LASER_CONTROLLER_SERVICE_DAC_MAX_V);
    s_service.status.dac_tec_channel_v =
        laser_controller_service_clamp_float(
            s_service.status.dac_tec_channel_v,
            0.0f,
            LASER_CONTROLLER_SERVICE_DAC_MAX_V);
}

static void laser_controller_service_refresh_modules_locked(void)
{
    for (uint32_t index = 0U; index < LASER_CONTROLLER_MODULE_COUNT; ++index) {
        laser_controller_module_status_t *module = &s_service.status.modules[index];

        /*
         * Planning edits must never erase the last known live module truth.
         * Expected/debug flags describe operator intent; detected/healthy are
         * runtime observations that should only change after an actual probe or
         * live-readback path reports new information.
         */
        if (!module->detected) {
            module->healthy = false;
        }
    }
}

static void laser_controller_service_seed_mock_registers_locked(void)
{
    memset(s_service.stusb4500_regs, 0, sizeof(s_service.stusb4500_regs));
    memset(s_service.dac80502_regs, 0, sizeof(s_service.dac80502_regs));
    memset(s_service.drv2605_regs, 0, sizeof(s_service.drv2605_regs));
    memset(s_service.imu_regs, 0, sizeof(s_service.imu_regs));

    s_service.imu_regs[0x0FU] = LASER_CONTROLLER_LSM6DSO_WHOAMI;
    s_service.imu_regs[0x10U] = 0x4CU;
    s_service.imu_regs[0x11U] = 0x58U;
    s_service.drv2605_regs[0x00U] = 0xE0U;
    s_service.drv2605_regs[0x01U] = 0x00U;
    s_service.stusb4500_regs[0x06U] = 0x21U;
    s_service.stusb4500_regs[0x07U] = 0x45U;
}

static void laser_controller_service_migrate_v3_profile(
    const laser_controller_service_persisted_profile_v3_t *legacy,
    laser_controller_service_persisted_profile_t *profile)
{
    if (legacy == NULL || profile == NULL) {
        return;
    }

    memset(profile, 0, sizeof(*profile));
    profile->version = LASER_CONTROLLER_SERVICE_PROFILE_VER;
    laser_controller_service_copy_text(
        profile->profile_name,
        sizeof(profile->profile_name),
        legacy->profile_name);
    memcpy(profile->modules, legacy->modules, sizeof(profile->modules));
    profile->dac_ld_channel_v = legacy->dac_ld_channel_v;
    profile->dac_tec_channel_v = legacy->dac_tec_channel_v;
    profile->dac_reference = legacy->dac_reference;
    profile->dac_gain_2x = legacy->dac_gain_2x;
    profile->dac_ref_div = legacy->dac_ref_div;
    profile->dac_sync_mode = legacy->dac_sync_mode;
    profile->imu_odr_hz = legacy->imu_odr_hz;
    profile->imu_accel_range_g = legacy->imu_accel_range_g;
    profile->imu_gyro_range_dps = legacy->imu_gyro_range_dps;
    profile->imu_gyro_enabled = legacy->imu_gyro_enabled;
    profile->imu_lpf2_enabled = legacy->imu_lpf2_enabled;
    profile->imu_timestamp_enabled = legacy->imu_timestamp_enabled;
    profile->imu_bdu_enabled = legacy->imu_bdu_enabled;
    profile->imu_if_inc_enabled = legacy->imu_if_inc_enabled;
    profile->imu_i2c_disabled = legacy->imu_i2c_disabled;
    profile->tof_min_range_m = legacy->tof_min_range_m;
    profile->tof_max_range_m = legacy->tof_max_range_m;
    profile->tof_stale_timeout_ms = legacy->tof_stale_timeout_ms;
    memcpy(profile->pd_profiles, legacy->pd_profiles, sizeof(profile->pd_profiles));
    profile->pd_programming_only_max_w = legacy->pd_programming_only_max_w;
    profile->pd_reduced_mode_min_w = legacy->pd_reduced_mode_min_w;
    profile->pd_reduced_mode_max_w = legacy->pd_reduced_mode_max_w;
    profile->pd_full_mode_min_w = legacy->pd_full_mode_min_w;
    profile->pd_firmware_plan_enabled = legacy->pd_firmware_plan_enabled;
    profile->haptic_effect_id = legacy->haptic_effect_id;
    profile->haptic_mode = legacy->haptic_mode;
    profile->haptic_library = legacy->haptic_library;
    profile->haptic_actuator = legacy->haptic_actuator;
    profile->haptic_rtp_level = legacy->haptic_rtp_level;
    profile->ld_rail_debug_enabled = false;
    profile->tec_rail_debug_enabled = false;
    laser_controller_service_load_default_runtime_safety(
        &profile->safety_thresholds,
        &profile->safety_timeouts);
}

static void laser_controller_service_migrate_v4_profile(
    const laser_controller_service_persisted_profile_v4_t *legacy,
    laser_controller_service_persisted_profile_t *profile)
{
    if (legacy == NULL || profile == NULL) {
        return;
    }

    memset(profile, 0, sizeof(*profile));
    profile->version = LASER_CONTROLLER_SERVICE_PROFILE_VER;
    laser_controller_service_copy_text(
        profile->profile_name,
        sizeof(profile->profile_name),
        legacy->profile_name);
    profile->ld_rail_debug_enabled = legacy->ld_rail_debug_enabled;
    profile->tec_rail_debug_enabled = legacy->tec_rail_debug_enabled;
    memcpy(profile->modules, legacy->modules, sizeof(profile->modules));
    profile->dac_ld_channel_v = legacy->dac_ld_channel_v;
    profile->dac_tec_channel_v = legacy->dac_tec_channel_v;
    profile->dac_reference = legacy->dac_reference;
    profile->dac_gain_2x = legacy->dac_gain_2x;
    profile->dac_ref_div = legacy->dac_ref_div;
    profile->dac_sync_mode = legacy->dac_sync_mode;
    profile->imu_odr_hz = legacy->imu_odr_hz;
    profile->imu_accel_range_g = legacy->imu_accel_range_g;
    profile->imu_gyro_range_dps = legacy->imu_gyro_range_dps;
    profile->imu_gyro_enabled = legacy->imu_gyro_enabled;
    profile->imu_lpf2_enabled = legacy->imu_lpf2_enabled;
    profile->imu_timestamp_enabled = legacy->imu_timestamp_enabled;
    profile->imu_bdu_enabled = legacy->imu_bdu_enabled;
    profile->imu_if_inc_enabled = legacy->imu_if_inc_enabled;
    profile->imu_i2c_disabled = legacy->imu_i2c_disabled;
    profile->tof_min_range_m = legacy->tof_min_range_m;
    profile->tof_max_range_m = legacy->tof_max_range_m;
    profile->tof_stale_timeout_ms = legacy->tof_stale_timeout_ms;
    memcpy(profile->pd_profiles, legacy->pd_profiles, sizeof(profile->pd_profiles));
    profile->pd_programming_only_max_w = legacy->pd_programming_only_max_w;
    profile->pd_reduced_mode_min_w = legacy->pd_reduced_mode_min_w;
    profile->pd_reduced_mode_max_w = legacy->pd_reduced_mode_max_w;
    profile->pd_full_mode_min_w = legacy->pd_full_mode_min_w;
    profile->pd_firmware_plan_enabled = legacy->pd_firmware_plan_enabled;
    profile->haptic_effect_id = legacy->haptic_effect_id;
    profile->haptic_mode = legacy->haptic_mode;
    profile->haptic_library = legacy->haptic_library;
    profile->haptic_actuator = legacy->haptic_actuator;
    profile->haptic_rtp_level = legacy->haptic_rtp_level;
    laser_controller_service_load_default_runtime_safety(
        &profile->safety_thresholds,
        &profile->safety_timeouts);
    laser_controller_service_load_default_runtime_target(
        &profile->runtime_target_mode,
        &profile->runtime_target_temp_c,
        &profile->runtime_target_lambda_nm);
}

static void laser_controller_service_migrate_v5_profile(
    const laser_controller_service_persisted_profile_v5_t *legacy,
    laser_controller_service_persisted_profile_t *profile)
{
    if (legacy == NULL || profile == NULL) {
        return;
    }

    memset(profile, 0, sizeof(*profile));
    profile->version = LASER_CONTROLLER_SERVICE_PROFILE_VER;
    laser_controller_service_copy_text(
        profile->profile_name,
        sizeof(profile->profile_name),
        legacy->profile_name);
    profile->ld_rail_debug_enabled = legacy->ld_rail_debug_enabled;
    profile->tec_rail_debug_enabled = legacy->tec_rail_debug_enabled;
    memcpy(profile->modules, legacy->modules, sizeof(profile->modules));
    profile->dac_ld_channel_v = legacy->dac_ld_channel_v;
    profile->dac_tec_channel_v = legacy->dac_tec_channel_v;
    profile->dac_reference = legacy->dac_reference;
    profile->dac_gain_2x = legacy->dac_gain_2x;
    profile->dac_ref_div = legacy->dac_ref_div;
    profile->dac_sync_mode = legacy->dac_sync_mode;
    profile->imu_odr_hz = legacy->imu_odr_hz;
    profile->imu_accel_range_g = legacy->imu_accel_range_g;
    profile->imu_gyro_range_dps = legacy->imu_gyro_range_dps;
    profile->imu_gyro_enabled = legacy->imu_gyro_enabled;
    profile->imu_lpf2_enabled = legacy->imu_lpf2_enabled;
    profile->imu_timestamp_enabled = legacy->imu_timestamp_enabled;
    profile->imu_bdu_enabled = legacy->imu_bdu_enabled;
    profile->imu_if_inc_enabled = legacy->imu_if_inc_enabled;
    profile->imu_i2c_disabled = legacy->imu_i2c_disabled;
    profile->tof_min_range_m = legacy->tof_min_range_m;
    profile->tof_max_range_m = legacy->tof_max_range_m;
    profile->tof_stale_timeout_ms = legacy->tof_stale_timeout_ms;
    memcpy(profile->pd_profiles, legacy->pd_profiles, sizeof(profile->pd_profiles));
    profile->pd_programming_only_max_w = legacy->pd_programming_only_max_w;
    profile->pd_reduced_mode_min_w = legacy->pd_reduced_mode_min_w;
    profile->pd_reduced_mode_max_w = legacy->pd_reduced_mode_max_w;
    profile->pd_full_mode_min_w = legacy->pd_full_mode_min_w;
    profile->pd_firmware_plan_enabled = legacy->pd_firmware_plan_enabled;
    profile->haptic_effect_id = legacy->haptic_effect_id;
    profile->haptic_mode = legacy->haptic_mode;
    profile->haptic_library = legacy->haptic_library;
    profile->haptic_actuator = legacy->haptic_actuator;
    profile->haptic_rtp_level = legacy->haptic_rtp_level;
    profile->safety_thresholds = legacy->safety_thresholds;
    profile->safety_timeouts = legacy->safety_timeouts;
    laser_controller_service_load_default_runtime_target(
        &profile->runtime_target_mode,
        &profile->runtime_target_temp_c,
        &profile->runtime_target_lambda_nm);
}

static void laser_controller_service_export_persisted_locked(
    laser_controller_service_persisted_profile_t *profile)
{
    if (profile == NULL) {
        return;
    }

    memset(profile, 0, sizeof(*profile));
    profile->version = LASER_CONTROLLER_SERVICE_PROFILE_VER;
    laser_controller_service_copy_text(
        profile->profile_name,
        sizeof(profile->profile_name),
        s_service.status.profile_name);
    profile->ld_rail_debug_enabled = s_service.status.ld_rail_debug_enabled;
    profile->tec_rail_debug_enabled = s_service.status.tec_rail_debug_enabled;

    for (uint32_t index = 0U; index < LASER_CONTROLLER_MODULE_COUNT; ++index) {
        profile->modules[index].expected_present =
            s_service.status.modules[index].expected_present;
        profile->modules[index].debug_enabled =
            s_service.status.modules[index].debug_enabled;
    }

    profile->dac_ld_channel_v = s_service.status.dac_ld_channel_v;
    profile->dac_tec_channel_v = s_service.status.dac_tec_channel_v;
    profile->dac_reference = s_service.status.dac_reference;
    profile->dac_gain_2x = s_service.status.dac_gain_2x;
    profile->dac_ref_div = s_service.status.dac_ref_div;
    profile->dac_sync_mode = s_service.status.dac_sync_mode;
    profile->imu_odr_hz = s_service.status.imu_odr_hz;
    profile->imu_accel_range_g = s_service.status.imu_accel_range_g;
    profile->imu_gyro_range_dps = s_service.status.imu_gyro_range_dps;
    profile->imu_gyro_enabled = s_service.status.imu_gyro_enabled;
    profile->imu_lpf2_enabled = s_service.status.imu_lpf2_enabled;
    profile->imu_timestamp_enabled = s_service.status.imu_timestamp_enabled;
    profile->imu_bdu_enabled = s_service.status.imu_bdu_enabled;
    profile->imu_if_inc_enabled = s_service.status.imu_if_inc_enabled;
    profile->imu_i2c_disabled = s_service.status.imu_i2c_disabled;
    profile->tof_min_range_m = s_service.status.tof_min_range_m;
    profile->tof_max_range_m = s_service.status.tof_max_range_m;
    profile->tof_stale_timeout_ms = s_service.status.tof_stale_timeout_ms;
    memcpy(
        profile->pd_profiles,
        s_service.status.pd_profiles,
        sizeof(profile->pd_profiles));
    profile->pd_programming_only_max_w =
        s_service.status.pd_programming_only_max_w;
    profile->pd_reduced_mode_min_w =
        s_service.status.pd_reduced_mode_min_w;
    profile->pd_reduced_mode_max_w =
        s_service.status.pd_reduced_mode_max_w;
    profile->pd_full_mode_min_w =
        s_service.status.pd_full_mode_min_w;
    profile->pd_firmware_plan_enabled =
        s_service.status.pd_firmware_plan_enabled;
    profile->haptic_effect_id = s_service.status.haptic_effect_id;
    profile->haptic_mode = s_service.status.haptic_mode;
    profile->haptic_library = s_service.status.haptic_library;
    profile->haptic_actuator = s_service.status.haptic_actuator;
    profile->haptic_rtp_level = s_service.status.haptic_rtp_level;
    profile->safety_thresholds = s_service.status.safety_thresholds;
    profile->safety_timeouts = s_service.status.safety_timeouts;
    profile->runtime_target_mode = s_service.status.runtime_target_mode;
    profile->runtime_target_temp_c = s_service.status.runtime_target_temp_c;
    profile->runtime_target_lambda_nm = s_service.status.runtime_target_lambda_nm;
}

static bool laser_controller_service_profile_is_valid(
    const laser_controller_service_persisted_profile_t *profile)
{
    laser_controller_config_t candidate = { 0 };

    if (profile == NULL ||
        profile->version < LASER_CONTROLLER_SERVICE_PROFILE_VER_MIN ||
        profile->version > LASER_CONTROLLER_SERVICE_PROFILE_VER ||
        profile->profile_name[0] == '\0') {
        return false;
    }

    if (!laser_controller_service_pd_config_valid(
            &(laser_controller_power_policy_t){
                .programming_only_max_w = profile->pd_programming_only_max_w,
                .reduced_mode_min_w = profile->pd_reduced_mode_min_w,
                .reduced_mode_max_w = profile->pd_reduced_mode_max_w,
                .full_mode_min_w = profile->pd_full_mode_min_w,
            },
            profile->pd_profiles,
            LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT)) {
        return false;
    }

    laser_controller_config_load_defaults(&candidate);
    candidate.thresholds = profile->safety_thresholds;
    candidate.timeouts = profile->safety_timeouts;
    if (!laser_controller_config_validate_runtime_safety(&candidate)) {
        return false;
    }

    return (profile->runtime_target_mode == LASER_CONTROLLER_BENCH_TARGET_MODE_TEMP ||
            profile->runtime_target_mode == LASER_CONTROLLER_BENCH_TARGET_MODE_LAMBDA) &&
           profile->runtime_target_temp_c >= 5.0f &&
           profile->runtime_target_temp_c <= 65.0f &&
           profile->runtime_target_lambda_nm >= 771.2f &&
           profile->runtime_target_lambda_nm <= 790.0f;
}

static void laser_controller_service_apply_persisted_locked(
    const laser_controller_service_persisted_profile_t *profile)
{
    laser_controller_service_pd_profile_t
        normalized_pd_profiles[LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT];

    if (profile == NULL) {
        return;
    }

    laser_controller_service_normalize_pd_profiles(
        profile->pd_profiles,
        normalized_pd_profiles);

    laser_controller_service_copy_text(
        s_service.status.profile_name,
        sizeof(s_service.status.profile_name),
        profile->profile_name);
    s_service.status.ld_rail_debug_enabled = profile->ld_rail_debug_enabled;
    s_service.status.tec_rail_debug_enabled = profile->tec_rail_debug_enabled;
    for (uint32_t index = 0U; index < LASER_CONTROLLER_MODULE_COUNT; ++index) {
        s_service.status.modules[index].expected_present =
            profile->modules[index].expected_present;
        s_service.status.modules[index].debug_enabled =
            profile->modules[index].debug_enabled;
    }

    s_service.status.dac_ld_channel_v = profile->dac_ld_channel_v;
    s_service.status.dac_tec_channel_v = profile->dac_tec_channel_v;
    s_service.status.dac_reference = profile->dac_reference;
    s_service.status.dac_gain_2x = profile->dac_gain_2x;
    s_service.status.dac_ref_div = profile->dac_ref_div;
    s_service.status.dac_sync_mode = profile->dac_sync_mode;
    s_service.status.imu_odr_hz = profile->imu_odr_hz;
    s_service.status.imu_accel_range_g = profile->imu_accel_range_g;
    s_service.status.imu_gyro_range_dps = profile->imu_gyro_range_dps;
    s_service.status.imu_gyro_enabled = profile->imu_gyro_enabled;
    s_service.status.imu_lpf2_enabled = profile->imu_lpf2_enabled;
    s_service.status.imu_timestamp_enabled = profile->imu_timestamp_enabled;
    s_service.status.imu_bdu_enabled = profile->imu_bdu_enabled;
    s_service.status.imu_if_inc_enabled = profile->imu_if_inc_enabled;
    s_service.status.imu_i2c_disabled = profile->imu_i2c_disabled;
    s_service.status.tof_min_range_m = profile->tof_min_range_m;
    s_service.status.tof_max_range_m = profile->tof_max_range_m;
    s_service.status.tof_stale_timeout_ms = profile->tof_stale_timeout_ms;
    memcpy(
        s_service.status.pd_profiles,
        normalized_pd_profiles,
        sizeof(s_service.status.pd_profiles));
    s_service.status.pd_programming_only_max_w =
        profile->pd_programming_only_max_w;
    s_service.status.pd_reduced_mode_min_w =
        profile->pd_reduced_mode_min_w;
    s_service.status.pd_reduced_mode_max_w =
        profile->pd_reduced_mode_max_w;
    s_service.status.pd_full_mode_min_w =
        profile->pd_full_mode_min_w;
    s_service.status.pd_firmware_plan_enabled =
        profile->pd_firmware_plan_enabled;
    s_service.status.haptic_effect_id = profile->haptic_effect_id;
    s_service.status.haptic_mode = profile->haptic_mode;
    s_service.status.haptic_library = profile->haptic_library;
    s_service.status.haptic_actuator = profile->haptic_actuator;
    s_service.status.haptic_rtp_level = profile->haptic_rtp_level;
    s_service.status.safety_thresholds = profile->safety_thresholds;
    s_service.status.safety_timeouts = profile->safety_timeouts;
    s_service.status.runtime_target_mode = profile->runtime_target_mode;
    s_service.status.runtime_target_temp_c = profile->runtime_target_temp_c;
    s_service.status.runtime_target_lambda_nm = profile->runtime_target_lambda_nm;
    memset(
        s_service.status.gpio_overrides,
        0,
        sizeof(s_service.status.gpio_overrides));
    s_service.status.gpio_override_count = 0U;
    laser_controller_service_sanitize_dac_settings_locked();
    laser_controller_service_sync_shadow_regs_locked();
    laser_controller_service_refresh_modules_locked();
    laser_controller_service_refresh_gpio_override_count_locked();
}

static bool laser_controller_service_load_profile_locked(void)
{
    nvs_handle_t handle = 0;
    laser_controller_service_persisted_profile_t profile;
    laser_controller_service_persisted_profile_v5_t profile_v5;
    laser_controller_service_persisted_profile_v4_t profile_v4;
    laser_controller_service_persisted_profile_v3_t legacy_profile;
    size_t size = 0U;
    esp_err_t err;

    err = nvs_open(
        LASER_CONTROLLER_SERVICE_NVS_NAMESPACE,
        NVS_READWRITE,
        &handle);
    if (err != ESP_OK) {
        s_service.status.persistence_available = false;
        return false;
    }

    s_service.status.persistence_available = true;
    err = nvs_get_blob(handle, LASER_CONTROLLER_SERVICE_NVS_KEY, NULL, &size);
    if (err != ESP_OK) {
        nvs_close(handle);
        return false;
    }

    if (size == sizeof(profile)) {
        err = nvs_get_blob(handle, LASER_CONTROLLER_SERVICE_NVS_KEY, &profile, &size);
    } else if (size == sizeof(profile_v5)) {
        err = nvs_get_blob(handle, LASER_CONTROLLER_SERVICE_NVS_KEY, &profile_v5, &size);
        if (err == ESP_OK) {
            laser_controller_service_migrate_v5_profile(&profile_v5, &profile);
            size = sizeof(profile);
        }
    } else if (size == sizeof(profile_v4)) {
        err = nvs_get_blob(handle, LASER_CONTROLLER_SERVICE_NVS_KEY, &profile_v4, &size);
        if (err == ESP_OK) {
            laser_controller_service_migrate_v4_profile(&profile_v4, &profile);
            size = sizeof(profile);
        }
    } else if (size == sizeof(legacy_profile)) {
        err = nvs_get_blob(
            handle,
            LASER_CONTROLLER_SERVICE_NVS_KEY,
            &legacy_profile,
            &size);
        if (err == ESP_OK) {
            laser_controller_service_migrate_v3_profile(&legacy_profile, &profile);
            size = sizeof(profile);
        }
    } else {
        err = ESP_ERR_INVALID_SIZE;
    }
    nvs_close(handle);
    if (err != ESP_OK || size != sizeof(profile) ||
        !laser_controller_service_profile_is_valid(&profile)) {
        return false;
    }

    laser_controller_service_apply_persisted_locked(&profile);
    s_service.status.persistence_dirty = false;
    s_service.status.last_save_ok = true;
    s_service.status.last_save_at_ms = 0U;
    s_service.status.profile_revision = 1U;
    laser_controller_service_write_action_locked(
        "Stored bring-up profile loaded from NVS.",
        0U);
    return true;
}

static void laser_controller_service_touch_profile_locked(void)
{
    s_service.status.profile_revision++;
    s_service.status.persistence_dirty = true;
    s_service.status.last_save_ok = false;
}

static void laser_controller_service_apply_core_preset_locked(const char *profile_name)
{
    static const laser_controller_service_pd_profile_t kDefaultPdProfiles[
        LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT] = {
        { .enabled = true, .voltage_v = 5.0f, .current_a = 3.0f },
        { .enabled = true, .voltage_v = 15.0f, .current_a = 2.0f },
        { .enabled = true, .voltage_v = 20.0f, .current_a = 2.25f },
    };

    memset(&s_service.status, 0, sizeof(s_service.status));
    s_service.status.persistence_available = false;
    s_service.status.persistence_dirty = false;
    s_service.status.last_save_ok = false;
    s_service.status.last_save_at_ms = 0U;
    s_service.status.profile_revision = 1U;
    laser_controller_service_copy_text(
        s_service.status.profile_name,
        sizeof(s_service.status.profile_name),
        profile_name);
    s_service.status.ld_rail_debug_enabled = false;
    s_service.status.tec_rail_debug_enabled = false;
    memset(
        s_service.status.gpio_overrides,
        0,
        sizeof(s_service.status.gpio_overrides));
    s_service.status.gpio_override_count = 0U;

    s_service.status.modules[LASER_CONTROLLER_MODULE_IMU].expected_present = true;
    s_service.status.modules[LASER_CONTROLLER_MODULE_IMU].debug_enabled = true;
    s_service.status.modules[LASER_CONTROLLER_MODULE_DAC].expected_present = true;
    s_service.status.modules[LASER_CONTROLLER_MODULE_DAC].debug_enabled = true;
    s_service.status.modules[LASER_CONTROLLER_MODULE_PD].expected_present = true;
    s_service.status.modules[LASER_CONTROLLER_MODULE_PD].debug_enabled = true;

    s_service.status.dac_ld_channel_v = 0.0f;
    s_service.status.dac_tec_channel_v = LASER_CONTROLLER_SERVICE_DEFAULT_TEC_CHANNEL_V;
    s_service.status.dac_reference = LASER_CONTROLLER_SERVICE_DAC_REFERENCE_INTERNAL;
    s_service.status.dac_gain_2x = true;
    s_service.status.dac_ref_div = true;
    s_service.status.dac_sync_mode = LASER_CONTROLLER_SERVICE_DAC_SYNC_ASYNC;
    s_service.status.imu_odr_hz = 208U;
    s_service.status.imu_accel_range_g = 4U;
    s_service.status.imu_gyro_range_dps = 500U;
    s_service.status.imu_gyro_enabled = true;
    s_service.status.imu_lpf2_enabled = false;
    s_service.status.imu_timestamp_enabled = true;
    s_service.status.imu_bdu_enabled = true;
    s_service.status.imu_if_inc_enabled = true;
    s_service.status.imu_i2c_disabled = true;
    s_service.status.tof_min_range_m = 0.20f;
    s_service.status.tof_max_range_m = 1.00f;
    s_service.status.tof_stale_timeout_ms = 150U;
    s_service.status.tof_illumination_enabled = false;
    s_service.status.tof_illumination_duty_cycle_pct = 0U;
    s_service.status.tof_illumination_frequency_hz =
        LASER_CONTROLLER_SERVICE_TOF_ILLUMINATION_PWM_HZ;
    memcpy(
        s_service.status.pd_profiles,
        kDefaultPdProfiles,
        sizeof(kDefaultPdProfiles));
    s_service.status.pd_programming_only_max_w = 30.0f;
    s_service.status.pd_reduced_mode_min_w = 30.0f;
    s_service.status.pd_reduced_mode_max_w = 35.0f;
    s_service.status.pd_full_mode_min_w = 35.1f;
    s_service.status.pd_firmware_plan_enabled = false;
    s_service.status.haptic_effect_id = 47U;
    s_service.status.haptic_mode =
        LASER_CONTROLLER_SERVICE_HAPTIC_MODE_INTERNAL_TRIGGER;
    s_service.status.haptic_library = 1U;
    s_service.status.haptic_actuator =
        LASER_CONTROLLER_SERVICE_HAPTIC_ACTUATOR_ERM;
    s_service.status.haptic_rtp_level = 96U;
    laser_controller_service_load_default_runtime_safety(
        &s_service.status.safety_thresholds,
        &s_service.status.safety_timeouts);
    laser_controller_service_load_default_runtime_target(
        &s_service.status.runtime_target_mode,
        &s_service.status.runtime_target_temp_c,
        &s_service.status.runtime_target_lambda_nm);
    laser_controller_service_sanitize_dac_settings_locked();
    laser_controller_service_copy_text(
        s_service.status.last_i2c_scan,
        sizeof(s_service.status.last_i2c_scan),
        "No scan run yet.");
    laser_controller_service_copy_text(
        s_service.status.last_i2c_op,
        sizeof(s_service.status.last_i2c_op),
        "No I2C transaction yet.");
    laser_controller_service_copy_text(
        s_service.status.last_spi_op,
        sizeof(s_service.status.last_spi_op),
        "No SPI transaction yet.");
    laser_controller_service_copy_text(
        s_service.status.last_action,
        sizeof(s_service.status.last_action),
        "Bring-up defaults loaded.");
    laser_controller_service_refresh_modules_locked();
    laser_controller_service_refresh_gpio_override_count_locked();
    laser_controller_service_seed_mock_registers_locked();
}

static void laser_controller_service_apply_manual_defaults_locked(const char *profile_name)
{
    laser_controller_service_apply_core_preset_locked(profile_name);
    s_service.status.interlocks_disabled = false;

    for (uint32_t index = 0U; index < LASER_CONTROLLER_MODULE_COUNT; ++index) {
        s_service.status.modules[index].expected_present = false;
        s_service.status.modules[index].debug_enabled = false;
    }

    laser_controller_service_copy_text(
        s_service.status.last_action,
        sizeof(s_service.status.last_action),
        "Manual bring-up defaults loaded. Pick one module and declare only the hardware that is physically installed.");
    laser_controller_service_refresh_modules_locked();
}

static uint8_t *laser_controller_service_i2c_device_regs_locked(uint32_t address)
{
    switch (address) {
        case LASER_CONTROLLER_STUSB4500_ADDR:
            return s_service.stusb4500_regs;
        case LASER_CONTROLLER_DAC80502_ADDR:
            return s_service.dac80502_regs;
        case LASER_CONTROLLER_DRV2605_ADDR:
            return s_service.drv2605_regs;
        default:
            return NULL;
    }
}

static bool laser_controller_service_spi_device_available_locked(const char *device)
{
    if (device == NULL) {
        return false;
    }

    if (strcmp(device, "imu") == 0) {
        return s_service.status.modules[LASER_CONTROLLER_MODULE_IMU].expected_present;
    }

    return false;
}

static void laser_controller_service_sync_shadow_regs_locked(void)
{
    laser_controller_service_pd_profile_t
        normalized_pd_profiles[LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT];
    const uint32_t ld_code =
        (uint32_t)(s_service.status.dac_ld_channel_v * 1000.0f);
    const uint32_t tec_code =
        (uint32_t)(s_service.status.dac_tec_channel_v * 1000.0f);
    uint8_t ctrl3_c = 0x00U;
    uint8_t ctrl4_c = 0x00U;
    uint8_t dac_sync_reg = 0x0300U >> 8U;
    uint8_t dac_config_reg = 0x0000U;
    uint8_t dac_gain_reg = 0x0000U;
    uint8_t haptic_mode_reg = 0x00U;
    uint8_t feedback_reg = 0x00U;
    uint8_t pd_count = 1U;

    s_service.dac80502_regs[0x08U] = (uint8_t)((ld_code >> 8U) & 0xFFU);
    s_service.dac80502_regs[0x09U] = (uint8_t)(ld_code & 0xFFU);
    s_service.dac80502_regs[0x0AU] = (uint8_t)((tec_code >> 8U) & 0xFFU);
    s_service.dac80502_regs[0x0BU] = (uint8_t)(tec_code & 0xFFU);

    if (s_service.status.dac_sync_mode ==
        LASER_CONTROLLER_SERVICE_DAC_SYNC_SYNC) {
        dac_sync_reg = 0x0003U;
    }
    if (s_service.status.dac_reference ==
        LASER_CONTROLLER_SERVICE_DAC_REFERENCE_EXTERNAL) {
        dac_config_reg |= 0x0001U;
    }
    if (s_service.status.dac_gain_2x) {
        dac_gain_reg |= 0x0003U;
    }
    if (s_service.status.dac_ref_div) {
        dac_gain_reg |= 0x0100U;
    }

    s_service.dac80502_regs[0x02U] = (uint8_t)(dac_sync_reg & 0xFFU);
    s_service.dac80502_regs[0x03U] = dac_config_reg;
    s_service.dac80502_regs[0x04U] = dac_gain_reg;

    s_service.imu_regs[0x10U] = (uint8_t)(s_service.status.imu_odr_hz & 0xFFU);
    s_service.imu_regs[0x11U] = s_service.status.imu_gyro_enabled
                                    ? (uint8_t)(s_service.status.imu_gyro_range_dps & 0xFFU)
                                    : 0x00U;

    if (s_service.status.imu_bdu_enabled) {
        ctrl3_c |= 0x40U;
    }
    if (s_service.status.imu_if_inc_enabled) {
        ctrl3_c |= 0x04U;
    }
    if (s_service.status.imu_i2c_disabled) {
        ctrl4_c |= 0x04U;
    }
    if (s_service.status.imu_lpf2_enabled) {
        s_service.imu_regs[0x10U] |= 0x02U;
    }
    if (s_service.status.imu_timestamp_enabled) {
        s_service.imu_regs[0x19U] = 0x20U;
    } else {
        s_service.imu_regs[0x19U] = 0x00U;
    }
    s_service.imu_regs[0x12U] = ctrl3_c;
    s_service.imu_regs[0x13U] = ctrl4_c;

    laser_controller_service_normalize_pd_profiles(
        s_service.status.pd_profiles,
        normalized_pd_profiles);
    if (normalized_pd_profiles[2].enabled) {
        pd_count = 3U;
    } else if (normalized_pd_profiles[1].enabled) {
        pd_count = 2U;
    }
    s_service.stusb4500_regs[0x70U] = pd_count;
    for (uint32_t index = 0U; index < LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT; ++index) {
        const uint32_t voltage_steps =
            (uint32_t)(normalized_pd_profiles[index].voltage_v / 0.05f);
        const uint32_t current_steps =
            (uint32_t)(normalized_pd_profiles[index].current_a / 0.01f);
        const uint32_t word =
            normalized_pd_profiles[index].enabled ?
                (((voltage_steps & 0x3FFU) << 10U) | (current_steps & 0x3FFU)) :
                0U;
        const size_t offset = 0x85U + (index * 4U);

        s_service.stusb4500_regs[offset + 0U] = (uint8_t)(word & 0xFFU);
        s_service.stusb4500_regs[offset + 1U] = (uint8_t)((word >> 8U) & 0xFFU);
        s_service.stusb4500_regs[offset + 2U] = (uint8_t)((word >> 16U) & 0xFFU);
        s_service.stusb4500_regs[offset + 3U] = (uint8_t)((word >> 24U) & 0xFFU);
    }

    haptic_mode_reg = laser_controller_service_haptic_mode_reg_value(
        s_service.status.haptic_mode);
    feedback_reg = s_service.status.haptic_actuator ==
                           LASER_CONTROLLER_SERVICE_HAPTIC_ACTUATOR_LRA
                       ? 0x80U
                       : 0x00U;
    s_service.drv2605_regs[0x01U] = haptic_mode_reg;
    s_service.drv2605_regs[0x02U] =
        (uint8_t)(s_service.status.haptic_rtp_level & 0xFFU);
    s_service.drv2605_regs[0x03U] =
        (uint8_t)(s_service.status.haptic_library & 0x07U);
    s_service.drv2605_regs[0x04U] =
        (uint8_t)(s_service.status.haptic_effect_id & 0x7FU);
    s_service.drv2605_regs[0x1AU] = feedback_reg;
}

void laser_controller_service_init_defaults(void)
{
    laser_controller_service_status_t status;

    portENTER_CRITICAL(&s_service_lock);
    laser_controller_service_apply_manual_defaults_locked("manual-bringup");
    (void)laser_controller_service_load_profile_locked();
    status = s_service.status;
    portEXIT_CRITICAL(&s_service_lock);

    (void)laser_controller_board_configure_dac_debug(
        status.dac_reference,
        status.dac_gain_2x,
        status.dac_ref_div,
        status.dac_sync_mode);
    (void)laser_controller_board_configure_imu_runtime(
        status.imu_odr_hz,
        status.imu_accel_range_g,
        status.imu_gyro_range_dps,
        status.imu_gyro_enabled,
        status.imu_lpf2_enabled,
        status.imu_timestamp_enabled,
        status.imu_bdu_enabled,
        status.imu_if_inc_enabled,
        status.imu_i2c_disabled);
    (void)laser_controller_board_configure_haptic_debug(
        status.haptic_effect_id,
        status.haptic_mode,
        status.haptic_library,
        status.haptic_actuator,
        status.haptic_rtp_level);
    (void)laser_controller_board_set_tof_illumination(
        false,
        0U,
        status.tof_illumination_frequency_hz > 0U ?
            status.tof_illumination_frequency_hz :
            LASER_CONTROLLER_SERVICE_TOF_ILLUMINATION_PWM_HZ);
}

void laser_controller_service_copy_status(laser_controller_service_status_t *status)
{
    if (status == NULL) {
        return;
    }

    portENTER_CRITICAL(&s_service_lock);
    *status = s_service.status;
    portEXIT_CRITICAL(&s_service_lock);
}

bool laser_controller_service_mode_requested(void)
{
    bool requested;

    portENTER_CRITICAL(&s_service_lock);
    requested = s_service.status.service_mode_requested;
    portEXIT_CRITICAL(&s_service_lock);
    return requested;
}

bool laser_controller_service_interlocks_disabled(void)
{
    bool enabled;

    portENTER_CRITICAL(&s_service_lock);
    enabled = s_service.status.interlocks_disabled;
    portEXIT_CRITICAL(&s_service_lock);
    return enabled;
}

bool laser_controller_service_module_expected(laser_controller_module_t module)
{
    bool expected = false;

    if (module >= LASER_CONTROLLER_MODULE_COUNT) {
        return false;
    }

    if (laser_controller_deployment_mode_active() &&
        laser_controller_deployment_module_required(module)) {
        return true;
    }

    portENTER_CRITICAL(&s_service_lock);
    expected = s_service.status.modules[module].expected_present;
    portEXIT_CRITICAL(&s_service_lock);
    return expected;
}

bool laser_controller_service_module_write_enabled(laser_controller_module_t module)
{
    bool enabled = false;

    if (module >= LASER_CONTROLLER_MODULE_COUNT) {
        return false;
    }

    if (laser_controller_deployment_mode_active() &&
        laser_controller_deployment_module_required(module)) {
        return true;
    }

    portENTER_CRITICAL(&s_service_lock);
    enabled = s_service.status.modules[module].expected_present ||
              s_service.status.modules[module].debug_enabled;
    portEXIT_CRITICAL(&s_service_lock);
    return enabled;
}

void laser_controller_service_get_dac_runtime(
    laser_controller_service_dac_runtime_t *runtime)
{
    if (runtime == NULL) {
        return;
    }

    portENTER_CRITICAL(&s_service_lock);
    runtime->service_mode_requested = s_service.status.service_mode_requested;
    runtime->dac_ld_channel_v = s_service.status.dac_ld_channel_v;
    runtime->dac_tec_channel_v = s_service.status.dac_tec_channel_v;
    portEXIT_CRITICAL(&s_service_lock);
}

void laser_controller_service_get_dac_config(
    laser_controller_service_dac_config_t *config)
{
    if (config == NULL) {
        return;
    }

    portENTER_CRITICAL(&s_service_lock);
    config->reference = s_service.status.dac_reference;
    config->gain_2x = s_service.status.dac_gain_2x;
    config->ref_div = s_service.status.dac_ref_div;
    config->sync_mode = s_service.status.dac_sync_mode;
    portEXIT_CRITICAL(&s_service_lock);
}

void laser_controller_service_get_imu_config(
    laser_controller_service_imu_config_t *config)
{
    if (config == NULL) {
        return;
    }

    portENTER_CRITICAL(&s_service_lock);
    config->odr_hz = s_service.status.imu_odr_hz;
    config->accel_range_g = s_service.status.imu_accel_range_g;
    config->gyro_range_dps = s_service.status.imu_gyro_range_dps;
    config->gyro_enabled = s_service.status.imu_gyro_enabled;
    config->lpf2_enabled = s_service.status.imu_lpf2_enabled;
    config->timestamp_enabled = s_service.status.imu_timestamp_enabled;
    config->bdu_enabled = s_service.status.imu_bdu_enabled;
    config->if_inc_enabled = s_service.status.imu_if_inc_enabled;
    config->i2c_disabled = s_service.status.imu_i2c_disabled;
    portEXIT_CRITICAL(&s_service_lock);
}

void laser_controller_service_get_haptic_config(
    laser_controller_service_haptic_config_t *config)
{
    if (config == NULL) {
        return;
    }

    portENTER_CRITICAL(&s_service_lock);
    config->effect_id = s_service.status.haptic_effect_id;
    config->mode = s_service.status.haptic_mode;
    config->library = s_service.status.haptic_library;
    config->actuator = s_service.status.haptic_actuator;
    config->rtp_level = s_service.status.haptic_rtp_level;
    portEXIT_CRITICAL(&s_service_lock);
}

void laser_controller_service_get_tof_illumination_config(
    laser_controller_service_tof_illumination_t *config)
{
    if (config == NULL) {
        return;
    }

    portENTER_CRITICAL(&s_service_lock);
    config->enabled = s_service.status.tof_illumination_enabled;
    config->duty_cycle_pct = s_service.status.tof_illumination_duty_cycle_pct;
    config->frequency_hz = s_service.status.tof_illumination_frequency_hz;
    portEXIT_CRITICAL(&s_service_lock);
}

void laser_controller_service_get_runtime_safety_policy(
    laser_controller_safety_thresholds_t *thresholds,
    laser_controller_timeout_policy_t *timeouts)
{
    portENTER_CRITICAL(&s_service_lock);
    if (thresholds != NULL) {
        *thresholds = s_service.status.safety_thresholds;
    }
    if (timeouts != NULL) {
        *timeouts = s_service.status.safety_timeouts;
    }
    portEXIT_CRITICAL(&s_service_lock);
}

void laser_controller_service_get_runtime_target(
    laser_controller_bench_target_mode_t *target_mode,
    laser_controller_celsius_t *target_temp_c,
    laser_controller_nm_t *target_lambda_nm)
{
    portENTER_CRITICAL(&s_service_lock);
    if (target_mode != NULL) {
        *target_mode = s_service.status.runtime_target_mode;
    }
    if (target_temp_c != NULL) {
        *target_temp_c = s_service.status.runtime_target_temp_c;
    }
    if (target_lambda_nm != NULL) {
        *target_lambda_nm = s_service.status.runtime_target_lambda_nm;
    }
    portEXIT_CRITICAL(&s_service_lock);
}

void laser_controller_service_report_module_probe(
    laser_controller_module_t module,
    bool detected,
    bool healthy)
{
    if (module >= LASER_CONTROLLER_MODULE_COUNT) {
        return;
    }

    portENTER_CRITICAL(&s_service_lock);
    s_service.status.modules[module].detected = detected;
    s_service.status.modules[module].healthy = detected && healthy;
    portEXIT_CRITICAL(&s_service_lock);
}

void laser_controller_service_set_mode_requested(
    bool enable,
    laser_controller_time_ms_t now_ms)
{
    bool reset_gpio_debug = false;

    portENTER_CRITICAL(&s_service_lock);
    s_service.status.service_mode_requested = enable;
    if (!enable) {
        s_service.status.haptic_driver_enable_requested = false;
        s_service.status.tof_illumination_enabled = false;
        memset(
            s_service.status.gpio_overrides,
            0,
            sizeof(s_service.status.gpio_overrides));
        laser_controller_service_refresh_gpio_override_count_locked();
        reset_gpio_debug = true;
    }
    laser_controller_service_write_action_locked(
        enable ? "Service mode requested from host." :
                 "Service mode request cleared from host.",
        now_ms);
    portEXIT_CRITICAL(&s_service_lock);

    if (reset_gpio_debug) {
        (void)laser_controller_board_set_tof_illumination(
            false,
            0U,
            LASER_CONTROLLER_SERVICE_TOF_ILLUMINATION_PWM_HZ);
        laser_controller_board_reset_gpio_debug_state();
    }
}

void laser_controller_service_prepare_for_deployment(
    laser_controller_time_ms_t now_ms)
{
    bool reset_gpio_debug = false;

    portENTER_CRITICAL(&s_service_lock);
    s_service.status.service_mode_requested = false;
    s_service.status.interlocks_disabled = false;
    s_service.status.ld_rail_debug_enabled = false;
    s_service.status.tec_rail_debug_enabled = false;
    s_service.status.haptic_driver_enable_requested = false;
    s_service.status.tof_illumination_enabled = false;
    s_service.status.tof_illumination_duty_cycle_pct = 0U;
    if (s_service.status.tof_illumination_frequency_hz == 0U) {
        s_service.status.tof_illumination_frequency_hz =
            LASER_CONTROLLER_SERVICE_TOF_ILLUMINATION_PWM_HZ;
    }
    memset(
        s_service.status.gpio_overrides,
        0,
        sizeof(s_service.status.gpio_overrides));
    laser_controller_service_refresh_gpio_override_count_locked();
    laser_controller_service_write_action_locked(
        "Deployment mode reclaimed ownership from service mode and cleared bench overrides.",
        now_ms);
    reset_gpio_debug = true;
    portEXIT_CRITICAL(&s_service_lock);

    if (reset_gpio_debug) {
        (void)laser_controller_board_set_tof_illumination(
            false,
            0U,
            LASER_CONTROLLER_SERVICE_TOF_ILLUMINATION_PWM_HZ);
        laser_controller_board_reset_gpio_debug_state();
    }
}

void laser_controller_service_set_interlocks_disabled(
    bool enabled,
    laser_controller_time_ms_t now_ms)
{
    portENTER_CRITICAL(&s_service_lock);
    s_service.status.interlocks_disabled = enabled;
    laser_controller_service_write_action_locked(
        enabled ?
            "All beam interlocks disabled by explicit bench override." :
            "All beam interlocks restored to normal controller supervision.",
        now_ms);
    portEXIT_CRITICAL(&s_service_lock);
}

bool laser_controller_service_apply_preset(
    const char *preset_name,
    laser_controller_time_ms_t now_ms)
{
    bool applied = true;

    if (preset_name == NULL) {
        return false;
    }

    portENTER_CRITICAL(&s_service_lock);

    if (strcmp(preset_name, "soc_imu_dac") == 0) {
        laser_controller_service_apply_core_preset_locked("soc-imu-dac");
    } else if (strcmp(preset_name, "add_haptic") == 0) {
        laser_controller_service_apply_core_preset_locked("imu-dac-haptic");
        s_service.status.modules[LASER_CONTROLLER_MODULE_HAPTIC].expected_present = true;
        s_service.status.modules[LASER_CONTROLLER_MODULE_HAPTIC].debug_enabled = true;
        laser_controller_service_refresh_modules_locked();
        laser_controller_service_touch_profile_locked();
        laser_controller_service_write_action_locked(
            "Bring-up preset applied: add haptic driver.",
            now_ms);
    } else if (strcmp(preset_name, "add_tof") == 0) {
        laser_controller_service_apply_core_preset_locked("imu-dac-tof");
        s_service.status.modules[LASER_CONTROLLER_MODULE_TOF].expected_present = true;
        s_service.status.modules[LASER_CONTROLLER_MODULE_TOF].debug_enabled = true;
        laser_controller_service_refresh_modules_locked();
        laser_controller_service_touch_profile_locked();
        laser_controller_service_write_action_locked(
            "Bring-up preset applied: add ToF interlock sensor.",
            now_ms);
    } else if (strcmp(preset_name, "full_stack") == 0) {
        laser_controller_service_apply_core_preset_locked("full-stack");
        for (uint32_t index = 0U; index < LASER_CONTROLLER_MODULE_COUNT; ++index) {
            s_service.status.modules[index].expected_present = true;
            s_service.status.modules[index].debug_enabled = true;
        }
        laser_controller_service_refresh_modules_locked();
        laser_controller_service_touch_profile_locked();
        laser_controller_service_write_action_locked(
            "Bring-up preset applied: full populated stack.",
            now_ms);
    } else {
        applied = false;
        laser_controller_service_write_action_locked(
            "Unknown bring-up preset rejected.",
            now_ms);
    }

    portEXIT_CRITICAL(&s_service_lock);
    return applied;
}

bool laser_controller_service_set_profile_name(
    const char *profile_name,
    laser_controller_time_ms_t now_ms)
{
    if (profile_name == NULL || profile_name[0] == '\0') {
        return false;
    }

    portENTER_CRITICAL(&s_service_lock);
    laser_controller_service_copy_text(
        s_service.status.profile_name,
        sizeof(s_service.status.profile_name),
        profile_name);
    laser_controller_service_touch_profile_locked();
    laser_controller_service_write_action_locked(
        "Bring-up profile renamed.",
        now_ms);
    portEXIT_CRITICAL(&s_service_lock);
    return true;
}

bool laser_controller_service_set_module_state(
    laser_controller_module_t module,
    bool expected_present,
    bool debug_enabled,
    laser_controller_time_ms_t now_ms)
{
    bool disable_tof_illumination = false;

    if (module >= LASER_CONTROLLER_MODULE_COUNT) {
        return false;
    }

    portENTER_CRITICAL(&s_service_lock);
    s_service.status.modules[module].expected_present = expected_present;
    s_service.status.modules[module].debug_enabled = debug_enabled;
    if (module == LASER_CONTROLLER_MODULE_TOF &&
        !expected_present &&
        !debug_enabled &&
        s_service.status.tof_illumination_enabled) {
        s_service.status.tof_illumination_enabled = false;
        disable_tof_illumination = true;
    }
    laser_controller_service_refresh_modules_locked();
    laser_controller_service_touch_profile_locked();
    laser_controller_service_write_action_locked(
        "Bring-up module expectations updated.",
        now_ms);
    portEXIT_CRITICAL(&s_service_lock);

    if (disable_tof_illumination) {
        (void)laser_controller_board_set_tof_illumination(
            false,
            0U,
            LASER_CONTROLLER_SERVICE_TOF_ILLUMINATION_PWM_HZ);
    }
    return true;
}

bool laser_controller_service_set_supply_enable(
    laser_controller_service_supply_t supply,
    bool enabled,
    laser_controller_time_ms_t now_ms)
{
    if (supply != LASER_CONTROLLER_SERVICE_SUPPLY_LD &&
        supply != LASER_CONTROLLER_SERVICE_SUPPLY_TEC) {
        return false;
    }

    portENTER_CRITICAL(&s_service_lock);
    if (supply == LASER_CONTROLLER_SERVICE_SUPPLY_LD) {
        s_service.status.ld_rail_debug_enabled = enabled;
    } else {
        s_service.status.tec_rail_debug_enabled = enabled;
    }
    laser_controller_service_touch_profile_locked();
    laser_controller_service_write_action_locked(
        enabled ?
            (supply == LASER_CONTROLLER_SERVICE_SUPPLY_LD ?
                 "Service rail request set: LD supply enabled." :
                 "Service rail request set: TEC supply enabled.") :
            (supply == LASER_CONTROLLER_SERVICE_SUPPLY_LD ?
                 "Service rail request cleared: LD supply disabled." :
                 "Service rail request cleared: TEC supply disabled."),
        now_ms);
    portEXIT_CRITICAL(&s_service_lock);
    return true;
}

void laser_controller_service_set_haptic_driver_enable(
    bool enabled,
    laser_controller_time_ms_t now_ms)
{
    portENTER_CRITICAL(&s_service_lock);
    s_service.status.haptic_driver_enable_requested = enabled;
    laser_controller_service_write_action_locked(
        enabled ?
            "ERM driver enable requested on GPIO48 in service mode." :
            "ERM driver enable cleared; GPIO48 forced low.",
        now_ms);
    portEXIT_CRITICAL(&s_service_lock);
}

void laser_controller_service_get_service_output_requests(
    bool *ld_rail_enabled,
    bool *tec_rail_enabled,
    bool *haptic_driver_enabled)
{
    portENTER_CRITICAL(&s_service_lock);
    if (ld_rail_enabled != NULL) {
        *ld_rail_enabled = s_service.status.ld_rail_debug_enabled;
    }
    if (tec_rail_enabled != NULL) {
        *tec_rail_enabled = s_service.status.tec_rail_debug_enabled;
    }
    if (haptic_driver_enabled != NULL) {
        *haptic_driver_enabled =
            s_service.status.haptic_driver_enable_requested;
    }
    portEXIT_CRITICAL(&s_service_lock);
}

bool laser_controller_service_set_gpio_override(
    uint32_t gpio_num,
    laser_controller_service_gpio_mode_t mode,
    bool level_high,
    bool pullup_enabled,
    bool pulldown_enabled,
    laser_controller_time_ms_t now_ms)
{
    if (!laser_controller_board_gpio_inspector_has_pin(gpio_num) ||
        mode > LASER_CONTROLLER_SERVICE_GPIO_MODE_OUTPUT) {
        return false;
    }

    portENTER_CRITICAL(&s_service_lock);
    if (mode == LASER_CONTROLLER_SERVICE_GPIO_MODE_FIRMWARE) {
        memset(
            &s_service.status.gpio_overrides[gpio_num],
            0,
            sizeof(s_service.status.gpio_overrides[gpio_num]));
        laser_controller_service_write_action_locked(
            "GPIO override released; firmware regains ownership.",
            now_ms);
    } else {
        s_service.status.gpio_overrides[gpio_num].active = true;
        s_service.status.gpio_overrides[gpio_num].mode = mode;
        s_service.status.gpio_overrides[gpio_num].level_high = level_high;
        s_service.status.gpio_overrides[gpio_num].pullup_enabled = pullup_enabled;
        s_service.status.gpio_overrides[gpio_num].pulldown_enabled = pulldown_enabled;
        laser_controller_service_write_action_locked(
            mode == LASER_CONTROLLER_SERVICE_GPIO_MODE_INPUT ?
                "GPIO override set to input mode." :
                (level_high ?
                     "GPIO override set to output high." :
                     "GPIO override set to output low."),
            now_ms);
    }
    laser_controller_service_refresh_gpio_override_count_locked();
    portEXIT_CRITICAL(&s_service_lock);

    laser_controller_board_apply_debug_gpio_state_now();
    return true;
}

void laser_controller_service_clear_gpio_overrides(laser_controller_time_ms_t now_ms)
{
    portENTER_CRITICAL(&s_service_lock);
    memset(
        s_service.status.gpio_overrides,
        0,
        sizeof(s_service.status.gpio_overrides));
    laser_controller_service_refresh_gpio_override_count_locked();
    laser_controller_service_write_action_locked(
        "All GPIO overrides cleared; returning pin ownership to firmware.",
        now_ms);
    portEXIT_CRITICAL(&s_service_lock);

    laser_controller_board_reset_gpio_debug_state();
}

bool laser_controller_service_get_gpio_override(
    uint32_t gpio_num,
    laser_controller_service_gpio_override_t *override_config)
{
    if (override_config == NULL ||
        gpio_num >= LASER_CONTROLLER_SERVICE_GPIO_COUNT) {
        return false;
    }

    portENTER_CRITICAL(&s_service_lock);
    *override_config = s_service.status.gpio_overrides[gpio_num];
    portEXIT_CRITICAL(&s_service_lock);
    return true;
}

void laser_controller_service_mark_runtime_status(
    const char *message,
    laser_controller_time_ms_t now_ms)
{
    portENTER_CRITICAL(&s_service_lock);
    laser_controller_service_write_action_locked(message, now_ms);
    portEXIT_CRITICAL(&s_service_lock);
}

esp_err_t laser_controller_service_set_dac_shadow(
    bool tec_channel,
    float voltage_v,
    laser_controller_time_ms_t now_ms)
{
    const float clamped_v =
        laser_controller_service_clamp_float(
            voltage_v,
            0.0f,
            LASER_CONTROLLER_SERVICE_DAC_MAX_V);
    const esp_err_t err =
        laser_controller_board_set_dac_debug_output(tec_channel, clamped_v);

    portENTER_CRITICAL(&s_service_lock);
    if (tec_channel) {
        s_service.status.dac_tec_channel_v = clamped_v;
    } else {
        s_service.status.dac_ld_channel_v = clamped_v;
    }
    laser_controller_service_sanitize_dac_settings_locked();
    laser_controller_service_sync_shadow_regs_locked();
    laser_controller_service_touch_profile_locked();
    laser_controller_service_write_action_locked(
        err == ESP_OK ?
            (tec_channel ? "DAC TEC output staged and applied." :
                           "DAC laser output staged and applied.") :
            (tec_channel ? "DAC TEC output staged; hardware write unavailable." :
                           "DAC laser output staged; hardware write unavailable."),
        now_ms);
    portEXIT_CRITICAL(&s_service_lock);
    return err;
}

esp_err_t laser_controller_service_set_dac_config(
    laser_controller_service_dac_reference_t reference,
    bool gain_2x,
    bool ref_div,
    laser_controller_service_dac_sync_t sync_mode,
    laser_controller_time_ms_t now_ms)
{
    const bool effective_ref_div =
        reference == LASER_CONTROLLER_SERVICE_DAC_REFERENCE_INTERNAL ? true : ref_div;
    const esp_err_t err =
        laser_controller_board_configure_dac_debug(
            reference,
            gain_2x,
            effective_ref_div,
            sync_mode);

    portENTER_CRITICAL(&s_service_lock);
    if (err == ESP_OK) {
        s_service.status.dac_reference = reference;
        s_service.status.dac_gain_2x = gain_2x;
        s_service.status.dac_ref_div = effective_ref_div;
        s_service.status.dac_sync_mode = sync_mode;
        laser_controller_service_sanitize_dac_settings_locked();
    }
    laser_controller_service_sync_shadow_regs_locked();
    laser_controller_service_touch_profile_locked();
    laser_controller_service_write_action_locked(
        err == ESP_OK ?
            "DAC reference and update configuration applied." :
            "DAC reference and update configuration staged; hardware write unavailable.",
        now_ms);
    portEXIT_CRITICAL(&s_service_lock);
    return err;
}

void laser_controller_service_set_imu_config(
    uint32_t odr_hz,
    uint32_t accel_range_g,
    uint32_t gyro_range_dps,
    bool gyro_enabled,
    bool lpf2_enabled,
    bool timestamp_enabled,
    bool bdu_enabled,
    bool if_inc_enabled,
    bool i2c_disabled,
    laser_controller_time_ms_t now_ms)
{
    const esp_err_t err =
        laser_controller_board_configure_imu_runtime(
            odr_hz,
            accel_range_g,
            gyro_range_dps,
            gyro_enabled,
            lpf2_enabled,
            timestamp_enabled,
            bdu_enabled,
            if_inc_enabled,
            i2c_disabled);

    portENTER_CRITICAL(&s_service_lock);
    s_service.status.imu_odr_hz = odr_hz;
    s_service.status.imu_accel_range_g = accel_range_g;
    s_service.status.imu_gyro_range_dps = gyro_range_dps;
    s_service.status.imu_gyro_enabled = gyro_enabled;
    s_service.status.imu_lpf2_enabled = lpf2_enabled;
    s_service.status.imu_timestamp_enabled = timestamp_enabled;
    s_service.status.imu_bdu_enabled = bdu_enabled;
    s_service.status.imu_if_inc_enabled = if_inc_enabled;
    s_service.status.imu_i2c_disabled = i2c_disabled;
    laser_controller_service_sync_shadow_regs_locked();
    laser_controller_service_touch_profile_locked();
    laser_controller_service_write_action_locked(
        err == ESP_OK ?
            "IMU runtime configuration applied." :
            "IMU runtime configuration staged; hardware write unavailable.",
        now_ms);
    portEXIT_CRITICAL(&s_service_lock);
}

void laser_controller_service_set_tof_config(
    float min_range_m,
    float max_range_m,
    uint32_t stale_timeout_ms,
    laser_controller_time_ms_t now_ms)
{
    portENTER_CRITICAL(&s_service_lock);
    s_service.status.tof_min_range_m = min_range_m;
    s_service.status.tof_max_range_m = max_range_m;
    s_service.status.tof_stale_timeout_ms = stale_timeout_ms;
    laser_controller_service_touch_profile_locked();
    laser_controller_service_write_action_locked(
        "ToF threshold tuning staged.",
        now_ms);
    portEXIT_CRITICAL(&s_service_lock);
}

void laser_controller_service_set_runtime_safety_policy(
    const laser_controller_safety_thresholds_t *thresholds,
    const laser_controller_timeout_policy_t *timeouts,
    laser_controller_time_ms_t now_ms)
{
    if (thresholds == NULL || timeouts == NULL) {
        return;
    }

    portENTER_CRITICAL(&s_service_lock);
    s_service.status.safety_thresholds = *thresholds;
    s_service.status.safety_timeouts = *timeouts;
    laser_controller_service_touch_profile_locked();
    laser_controller_service_write_action_locked(
        "Runtime safety policy staged for profile persistence.",
        now_ms);
    portEXIT_CRITICAL(&s_service_lock);
}

void laser_controller_service_set_runtime_target(
    laser_controller_bench_target_mode_t target_mode,
    laser_controller_celsius_t target_temp_c,
    laser_controller_nm_t target_lambda_nm,
    laser_controller_time_ms_t now_ms)
{
    portENTER_CRITICAL(&s_service_lock);
    s_service.status.runtime_target_mode = target_mode;
    s_service.status.runtime_target_temp_c = target_temp_c;
    s_service.status.runtime_target_lambda_nm = target_lambda_nm;
    laser_controller_service_touch_profile_locked();
    laser_controller_service_write_action_locked(
        "Runtime target staged for profile persistence.",
        now_ms);
    portEXIT_CRITICAL(&s_service_lock);
}

esp_err_t laser_controller_service_set_tof_illumination(
    bool enabled,
    uint32_t duty_cycle_pct,
    uint32_t frequency_hz,
    laser_controller_time_ms_t now_ms)
{
    const uint32_t clamped_duty_cycle_pct =
        duty_cycle_pct > 100U ? 100U : duty_cycle_pct;
    const uint32_t requested_frequency_hz =
        frequency_hz > 0U ?
            frequency_hz :
            LASER_CONTROLLER_SERVICE_TOF_ILLUMINATION_PWM_HZ;
    esp_err_t err = ESP_OK;

    if (enabled &&
        !laser_controller_service_module_write_enabled(LASER_CONTROLLER_MODULE_TOF)) {
        return ESP_ERR_INVALID_STATE;
    }

    err = laser_controller_board_set_tof_illumination(
        enabled,
        clamped_duty_cycle_pct,
        requested_frequency_hz);
    if (err != ESP_OK) {
        return err;
    }

    portENTER_CRITICAL(&s_service_lock);
    s_service.status.tof_illumination_enabled =
        enabled && clamped_duty_cycle_pct > 0U;
    s_service.status.tof_illumination_duty_cycle_pct = clamped_duty_cycle_pct;
    s_service.status.tof_illumination_frequency_hz = requested_frequency_hz;
    laser_controller_service_write_action_locked(
        enabled && clamped_duty_cycle_pct > 0U ?
            "Front illumination staged on GPIO6 in service mode." :
            "Front illumination forced low on GPIO6.",
        now_ms);
    portEXIT_CRITICAL(&s_service_lock);

    return ESP_OK;
}

esp_err_t laser_controller_service_set_pd_config(
    const laser_controller_power_policy_t *power_policy,
    const laser_controller_service_pd_profile_t *profiles,
    size_t profile_count,
    laser_controller_time_ms_t now_ms)
{
    laser_controller_service_pd_profile_t
        normalized_profiles[LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT];
    esp_err_t err = ESP_OK;

    if (!laser_controller_service_pd_config_valid(
            power_policy,
            profiles,
            profile_count)) {
        return ESP_ERR_INVALID_ARG;
    }

    laser_controller_service_normalize_pd_profiles(profiles, normalized_profiles);
    err = laser_controller_board_configure_pd_debug(
        normalized_profiles,
        LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT);

    portENTER_CRITICAL(&s_service_lock);
    memcpy(
        s_service.status.pd_profiles,
        normalized_profiles,
        sizeof(s_service.status.pd_profiles));
    s_service.status.pd_programming_only_max_w =
        power_policy->programming_only_max_w;
    s_service.status.pd_reduced_mode_min_w =
        power_policy->reduced_mode_min_w;
    s_service.status.pd_reduced_mode_max_w =
        power_policy->reduced_mode_max_w;
    s_service.status.pd_full_mode_min_w =
        power_policy->full_mode_min_w;
    laser_controller_service_sync_shadow_regs_locked();
    laser_controller_service_touch_profile_locked();
    laser_controller_service_write_action_locked(
        err == ESP_OK ?
            "USB-PD runtime PDOs applied. PDO1 is kept at 5 V fallback and thresholds are updated." :
            "USB-PD thresholds saved, but the STUSB4500 runtime PDO write did not complete.",
        now_ms);
    laser_controller_service_copy_text(
        s_service.status.last_i2c_op,
        sizeof(s_service.status.last_i2c_op),
        err == ESP_OK ?
            "STUSB4500 runtime PDO registers updated, verified, and renegotiation requested." :
            "STUSB4500 runtime PDO write or renegotiation failed.");
    portEXIT_CRITICAL(&s_service_lock);
    return err;
}

void laser_controller_service_get_pd_config(
    laser_controller_power_policy_t *power_policy,
    laser_controller_service_pd_profile_t *profiles,
    size_t profile_count,
    bool *firmware_plan_enabled)
{
    portENTER_CRITICAL(&s_service_lock);
    if (power_policy != NULL) {
        power_policy->programming_only_max_w =
            s_service.status.pd_programming_only_max_w;
        power_policy->reduced_mode_min_w =
            s_service.status.pd_reduced_mode_min_w;
        power_policy->reduced_mode_max_w =
            s_service.status.pd_reduced_mode_max_w;
        power_policy->full_mode_min_w =
            s_service.status.pd_full_mode_min_w;
    }
    if (profiles != NULL &&
        profile_count == LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT) {
        memcpy(
            profiles,
            s_service.status.pd_profiles,
            sizeof(s_service.status.pd_profiles));
    }
    if (firmware_plan_enabled != NULL) {
        *firmware_plan_enabled = s_service.status.pd_firmware_plan_enabled;
    }
    portEXIT_CRITICAL(&s_service_lock);
}

void laser_controller_service_set_pd_firmware_plan_enabled(
    bool enabled,
    laser_controller_time_ms_t now_ms)
{
    portENTER_CRITICAL(&s_service_lock);
    if (s_service.status.pd_firmware_plan_enabled != enabled) {
        s_service.status.pd_firmware_plan_enabled = enabled;
        laser_controller_service_touch_profile_locked();
    }
    laser_controller_service_write_action_locked(
        enabled ?
            "Firmware-owned PDO reconcile enabled. Saved PDO plan will be compared against STUSB runtime state online." :
            "Firmware-owned PDO reconcile disabled. STUSB runtime PDOs will only change on explicit host apply.",
        now_ms);
    portEXIT_CRITICAL(&s_service_lock);
}

esp_err_t laser_controller_service_apply_saved_pd_runtime(
    laser_controller_time_ms_t now_ms)
{
    laser_controller_service_pd_profile_t
        profiles[LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT];
    bool firmware_plan_enabled = false;
    esp_err_t err;

    portENTER_CRITICAL(&s_service_lock);
    firmware_plan_enabled = s_service.status.pd_firmware_plan_enabled;
    memcpy(profiles, s_service.status.pd_profiles, sizeof(profiles));
    portEXIT_CRITICAL(&s_service_lock);

    if (!firmware_plan_enabled) {
        return ESP_ERR_INVALID_STATE;
    }

    err = laser_controller_board_configure_pd_debug(
        profiles,
        LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT);

    portENTER_CRITICAL(&s_service_lock);
    laser_controller_service_write_action_locked(
        err == ESP_OK ?
            "Firmware-owned PDO reconcile wrote the saved plan into STUSB runtime registers." :
            "Firmware-owned PDO reconcile could not update STUSB runtime registers.",
        now_ms);
    laser_controller_service_copy_text(
        s_service.status.last_i2c_op,
        sizeof(s_service.status.last_i2c_op),
        err == ESP_OK ?
            "Firmware PDO reconcile updated STUSB4500 runtime registers and requested renegotiation." :
            "Firmware PDO reconcile could not verify or apply the STUSB4500 runtime registers.");
    portEXIT_CRITICAL(&s_service_lock);

    return err;
}

esp_err_t laser_controller_service_burn_pd_nvm(
    const laser_controller_service_pd_profile_t *profiles,
    size_t profile_count,
    laser_controller_time_ms_t now_ms)
{
    laser_controller_service_pd_profile_t
        normalized_profiles[LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT];
    esp_err_t err = ESP_OK;

    if (profiles == NULL ||
        profile_count != LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    laser_controller_service_normalize_pd_profiles(profiles, normalized_profiles);
    err = laser_controller_board_burn_pd_nvm(
        normalized_profiles,
        LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT);

    portENTER_CRITICAL(&s_service_lock);
    laser_controller_service_write_action_locked(
        err == ESP_OK ?
            "STUSB4500 NVM burn completed and raw NVM readback matched the requested PDO bytes." :
            "STUSB4500 NVM burn failed before raw NVM readback could be verified.",
        now_ms);
    laser_controller_service_copy_text(
        s_service.status.last_i2c_op,
        sizeof(s_service.status.last_i2c_op),
        err == ESP_OK ?
            "STUSB4500 NVM PDO bytes written and verified against raw NVM readback." :
            "STUSB4500 NVM write failed or raw readback did not match.");
    portEXIT_CRITICAL(&s_service_lock);

    return err;
}

void laser_controller_service_set_haptic_config(
    uint32_t effect_id,
    laser_controller_service_haptic_mode_t mode,
    uint32_t library,
    laser_controller_service_haptic_actuator_t actuator,
    uint32_t rtp_level,
    laser_controller_time_ms_t now_ms)
{
    const esp_err_t err =
        laser_controller_board_configure_haptic_debug(
            effect_id,
            mode,
            library,
            actuator,
            rtp_level);

    portENTER_CRITICAL(&s_service_lock);
    s_service.status.haptic_effect_id = effect_id;
    s_service.status.haptic_mode = mode;
    s_service.status.haptic_library = library;
    s_service.status.haptic_actuator = actuator;
    s_service.status.haptic_rtp_level = rtp_level;
    laser_controller_service_sync_shadow_regs_locked();
    laser_controller_service_touch_profile_locked();
    laser_controller_service_write_action_locked(
        err == ESP_OK ?
            "DRV2605 configuration applied." :
            "DRV2605 configuration staged; hardware write unavailable.",
        now_ms);
    portEXIT_CRITICAL(&s_service_lock);
}

void laser_controller_service_fire_haptic_test(laser_controller_time_ms_t now_ms)
{
    const esp_err_t err = laser_controller_board_fire_haptic_test();

    portENTER_CRITICAL(&s_service_lock);
    laser_controller_service_copy_text(
        s_service.status.last_i2c_op,
        sizeof(s_service.status.last_i2c_op),
        err == ESP_OK ?
            "DRV2605 GO pulse applied." :
            (err == ESP_ERR_INVALID_STATE ?
                 "DRV2605 GO blocked: assert ERM EN on GPIO48 first." :
                 "DRV2605 GO pulse unavailable on hardware."));
    laser_controller_service_write_action_locked(
        err == ESP_OK ?
            "Haptic test fired in service mode." :
            (err == ESP_ERR_INVALID_STATE ?
                 "Haptic test blocked: ERM EN is low on GPIO48." :
                 "Haptic test request staged but hardware did not acknowledge."),
        now_ms);
    portEXIT_CRITICAL(&s_service_lock);
}

esp_err_t laser_controller_service_fire_haptic_trigger_pattern(
    uint32_t pulse_count,
    uint32_t high_ms,
    uint32_t low_ms,
    bool release_after,
    laser_controller_time_ms_t now_ms)
{
    laser_controller_service_haptic_config_t haptic_config;
    bool haptic_driver_enabled = false;
    esp_err_t err = ESP_OK;

    if (pulse_count == 0U ||
        pulse_count > LASER_CONTROLLER_SERVICE_HAPTIC_PATTERN_MAX_PULSES ||
        high_ms < LASER_CONTROLLER_SERVICE_HAPTIC_PATTERN_MIN_MS ||
        high_ms > LASER_CONTROLLER_SERVICE_HAPTIC_PATTERN_MAX_MS ||
        low_ms < LASER_CONTROLLER_SERVICE_HAPTIC_PATTERN_MIN_MS ||
        low_ms > LASER_CONTROLLER_SERVICE_HAPTIC_PATTERN_MAX_MS) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!laser_controller_service_module_write_enabled(LASER_CONTROLLER_MODULE_HAPTIC)) {
        return ESP_ERR_INVALID_STATE;
    }

    laser_controller_service_get_service_output_requests(
        NULL,
        NULL,
        &haptic_driver_enabled);
    if (!haptic_driver_enabled) {
        return ESP_ERR_INVALID_STATE;
    }

    laser_controller_service_get_haptic_config(&haptic_config);
    if (haptic_config.mode != LASER_CONTROLLER_SERVICE_HAPTIC_MODE_EXTERNAL_EDGE &&
        haptic_config.mode != LASER_CONTROLLER_SERVICE_HAPTIC_MODE_EXTERNAL_LEVEL) {
        return ESP_ERR_INVALID_STATE;
    }

    for (uint32_t pulse_index = 0U; pulse_index < pulse_count; ++pulse_index) {
        if (!laser_controller_service_set_gpio_override(
                LASER_CONTROLLER_GPIO_ERM_TRIG_GN_LD_EN,
                LASER_CONTROLLER_SERVICE_GPIO_MODE_OUTPUT,
                true,
                false,
                false,
                now_ms)) {
            err = ESP_ERR_INVALID_STATE;
            break;
        }
        laser_controller_board_apply_debug_gpio_state_now();
        vTaskDelay(pdMS_TO_TICKS(high_ms));

        if (!laser_controller_service_set_gpio_override(
                LASER_CONTROLLER_GPIO_ERM_TRIG_GN_LD_EN,
                LASER_CONTROLLER_SERVICE_GPIO_MODE_OUTPUT,
                false,
                false,
                false,
                now_ms)) {
            err = ESP_ERR_INVALID_STATE;
            break;
        }
        laser_controller_board_apply_debug_gpio_state_now();

        if ((pulse_index + 1U) < pulse_count) {
            vTaskDelay(pdMS_TO_TICKS(low_ms));
        }
    }

    if (release_after || err != ESP_OK) {
        (void)laser_controller_service_set_gpio_override(
            LASER_CONTROLLER_GPIO_ERM_TRIG_GN_LD_EN,
            LASER_CONTROLLER_SERVICE_GPIO_MODE_FIRMWARE,
            false,
            false,
            false,
            now_ms);
        laser_controller_board_apply_debug_gpio_state_now();
    }

    portENTER_CRITICAL(&s_service_lock);
    laser_controller_service_copy_text(
        s_service.status.last_action,
        sizeof(s_service.status.last_action),
        err == ESP_OK ?
            (release_after ?
                 "External ERM trigger burst completed and IO37 returned to firmware." :
                 "External ERM trigger burst completed; IO37 held low under service override.") :
            "External ERM trigger burst failed before completion.");
    portEXIT_CRITICAL(&s_service_lock);

    return err;
}

void laser_controller_service_save_profile(laser_controller_time_ms_t now_ms)
{
    nvs_handle_t handle = 0;
    laser_controller_service_persisted_profile_t profile;
    esp_err_t err;

    portENTER_CRITICAL(&s_service_lock);
    laser_controller_service_export_persisted_locked(&profile);
    portEXIT_CRITICAL(&s_service_lock);

    err = nvs_open(
        LASER_CONTROLLER_SERVICE_NVS_NAMESPACE,
        NVS_READWRITE,
        &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(
            handle,
            LASER_CONTROLLER_SERVICE_NVS_KEY,
            &profile,
            sizeof(profile));
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    if (handle != 0) {
        nvs_close(handle);
    }

    portENTER_CRITICAL(&s_service_lock);
    s_service.status.persistence_available = (err == ESP_OK);
    s_service.status.last_save_ok = (err == ESP_OK);
    s_service.status.persistence_dirty = (err != ESP_OK);
    s_service.status.last_save_at_ms = err == ESP_OK ? now_ms : 0U;
    laser_controller_service_write_action_locked(
        err == ESP_OK ?
            "Bring-up profile saved to NVS." :
            "Bring-up profile save failed; keeping host/local draft as fallback.",
        now_ms);
    portEXIT_CRITICAL(&s_service_lock);
}

void laser_controller_service_i2c_scan(laser_controller_time_ms_t now_ms)
{
    char buffer[LASER_CONTROLLER_SERVICE_TEXT_LEN];
    size_t offset = 0U;
    static const uint32_t kProbeAddresses[] = {
        LASER_CONTROLLER_STUSB4500_ADDR,
        LASER_CONTROLLER_DAC80502_ADDR,
        LASER_CONTROLLER_DRV2605_ADDR,
    };
    esp_err_t first_error = ESP_OK;
    bool sda_high = false;
    bool scl_high = false;

    buffer[0] = '\0';
    for (size_t index = 0U; index < sizeof(kProbeAddresses) / sizeof(kProbeAddresses[0]); ++index) {
        const uint32_t address = kProbeAddresses[index];
        const esp_err_t err = laser_controller_service_i2c_probe(address);

        if (err == ESP_OK && offset < sizeof(buffer)) {
            offset += (size_t)snprintf(
                &buffer[offset],
                sizeof(buffer) - offset,
                "%s0x%02lX",
                offset > 0U ? " " : "",
                (unsigned long)address);
        } else if (err != ESP_ERR_NOT_FOUND && first_error == ESP_OK) {
            first_error = err;
        }

        portENTER_CRITICAL(&s_service_lock);
        laser_controller_service_update_module_probe_locked(address, err == ESP_OK);
        portEXIT_CRITICAL(&s_service_lock);
    }

    if (offset == 0U) {
        if (first_error != ESP_OK && first_error != ESP_ERR_NOT_FOUND) {
            laser_controller_board_get_shared_i2c_line_levels(&sda_high, &scl_high);
            (void)snprintf(
                buffer,
                sizeof(buffer),
                "Shared I2C unavailable (%s, SDA=%u, SCL=%u).",
                esp_err_to_name(first_error),
                sda_high ? 1U : 0U,
                scl_high ? 1U : 0U);
        } else {
            laser_controller_service_copy_text(
                buffer,
                sizeof(buffer),
                "No known shared-I2C devices acknowledged.");
        }
    }

    portENTER_CRITICAL(&s_service_lock);

    laser_controller_service_copy_text(
        s_service.status.last_i2c_scan,
        sizeof(s_service.status.last_i2c_scan),
        buffer);
    laser_controller_service_write_action_locked(
        "I2C scan completed on the shared bench bus.",
        now_ms);

    portEXIT_CRITICAL(&s_service_lock);
}

void laser_controller_service_i2c_read(
    uint32_t address,
    uint32_t reg,
    laser_controller_time_ms_t now_ms)
{
    char buffer[LASER_CONTROLLER_SERVICE_TEXT_LEN];
    uint8_t value = 0U;
    uint16_t dac_value = 0U;
    uint8_t *regs = NULL;
    esp_err_t err;

    if (address == LASER_CONTROLLER_DAC80502_ADDR) {
        err = laser_controller_service_dac_read_reg(reg, &dac_value);
    } else {
        err = laser_controller_service_i2c_read_reg_u8(address, reg, &value);
    }

    if (err == ESP_OK) {
        if (address == LASER_CONTROLLER_DAC80502_ADDR) {
            regs = s_service.dac80502_regs;
            if (regs != NULL) {
                regs[reg & 0xFFU] = (uint8_t)((dac_value >> 8U) & 0xFFU);
                regs[(reg + 1U) & 0xFFU] = (uint8_t)(dac_value & 0xFFU);
            }
            (void)snprintf(
                buffer,
                sizeof(buffer),
                "read 0x%02lX cmd 0x%02lX -> 0x%04X",
                (unsigned long)address,
                (unsigned long)reg,
                (unsigned)dac_value);
        } else {
            regs = laser_controller_service_i2c_device_regs_locked(address);
            if (regs != NULL) {
                regs[reg & 0xFFU] = value;
            }
            (void)snprintf(
                buffer,
                sizeof(buffer),
                "read 0x%02lX reg 0x%02lX -> 0x%02X",
                (unsigned long)address,
                (unsigned long)reg,
                value);
        }
    } else if (err == ESP_ERR_NOT_FOUND || err == ESP_ERR_INVALID_RESPONSE) {
        (void)snprintf(
            buffer,
            sizeof(buffer),
            "read 0x%02lX %s 0x%02lX -> no-ack",
            (unsigned long)address,
            address == LASER_CONTROLLER_DAC80502_ADDR ? "cmd" : "reg",
            (unsigned long)reg);
    } else {
        (void)snprintf(
            buffer,
            sizeof(buffer),
            "read 0x%02lX %s 0x%02lX -> %s",
            (unsigned long)address,
            address == LASER_CONTROLLER_DAC80502_ADDR ? "cmd" : "reg",
            (unsigned long)reg,
            esp_err_to_name(err));
    }

    portENTER_CRITICAL(&s_service_lock);
    laser_controller_service_update_module_probe_locked(address, err == ESP_OK);

    laser_controller_service_copy_text(
        s_service.status.last_i2c_op,
        sizeof(s_service.status.last_i2c_op),
        buffer);
    laser_controller_service_write_action_locked(
        "I2C register read completed.",
        now_ms);

    portEXIT_CRITICAL(&s_service_lock);
}

void laser_controller_service_i2c_write(
    uint32_t address,
    uint32_t reg,
    uint32_t value,
    laser_controller_time_ms_t now_ms)
{
    char buffer[LASER_CONTROLLER_SERVICE_TEXT_LEN];
    uint8_t *regs = NULL;
    const esp_err_t err =
        address == LASER_CONTROLLER_DAC80502_ADDR ?
            laser_controller_service_dac_write_reg(reg, value) :
            laser_controller_service_i2c_write_reg_u8(address, reg, value);

    if (err == ESP_OK) {
        if (address == LASER_CONTROLLER_DAC80502_ADDR) {
            regs = s_service.dac80502_regs;
            if (regs != NULL) {
                regs[reg & 0xFFU] = (uint8_t)((value >> 8U) & 0xFFU);
                regs[(reg + 1U) & 0xFFU] = (uint8_t)(value & 0xFFU);
            }
            (void)snprintf(
                buffer,
                sizeof(buffer),
                "write 0x%02lX cmd 0x%02lX <- 0x%04lX",
                (unsigned long)address,
                (unsigned long)reg,
                (unsigned long)(value & 0xFFFFU));
        } else {
            regs = laser_controller_service_i2c_device_regs_locked(address);
            if (regs != NULL) {
                regs[reg & 0xFFU] = (uint8_t)(value & 0xFFU);
            }
            (void)snprintf(
                buffer,
                sizeof(buffer),
                "write 0x%02lX reg 0x%02lX <- 0x%02lX",
                (unsigned long)address,
                (unsigned long)reg,
                (unsigned long)(value & 0xFFU));
        }
    } else if (err == ESP_ERR_NOT_FOUND || err == ESP_ERR_INVALID_RESPONSE) {
        (void)snprintf(
            buffer,
            sizeof(buffer),
            "write 0x%02lX %s 0x%02lX <- no-ack",
            (unsigned long)address,
            address == LASER_CONTROLLER_DAC80502_ADDR ? "cmd" : "reg",
            (unsigned long)reg);
    } else {
        (void)snprintf(
            buffer,
            sizeof(buffer),
            "write 0x%02lX %s 0x%02lX <- %s",
            (unsigned long)address,
            address == LASER_CONTROLLER_DAC80502_ADDR ? "cmd" : "reg",
            (unsigned long)reg,
            esp_err_to_name(err));
    }

    portENTER_CRITICAL(&s_service_lock);
    laser_controller_service_update_module_probe_locked(address, err == ESP_OK);

    laser_controller_service_copy_text(
        s_service.status.last_i2c_op,
        sizeof(s_service.status.last_i2c_op),
        buffer);
    laser_controller_service_touch_profile_locked();
    laser_controller_service_write_action_locked(
        "I2C register write completed.",
        now_ms);

    portEXIT_CRITICAL(&s_service_lock);
}

void laser_controller_service_spi_read(
    const char *device,
    uint32_t reg,
    laser_controller_time_ms_t now_ms)
{
    char buffer[LASER_CONTROLLER_SERVICE_TEXT_LEN];
    uint8_t value = 0U;
    esp_err_t err = ESP_ERR_NOT_SUPPORTED;

    portENTER_CRITICAL(&s_service_lock);

    if (!laser_controller_service_spi_device_available_locked(device)) {
        (void)snprintf(
            buffer,
            sizeof(buffer),
            "%s reg 0x%02lX -> unavailable",
            device != NULL ? device : "unknown",
            (unsigned long)reg);
    } else {
        portEXIT_CRITICAL(&s_service_lock);
        err = laser_controller_board_imu_spi_read((uint8_t)(reg & 0xFFU), &value);
        portENTER_CRITICAL(&s_service_lock);
        if (err == ESP_OK) {
            s_service.imu_regs[reg & 0xFFU] = value;
            (void)snprintf(
                buffer,
                sizeof(buffer),
                "%s reg 0x%02lX -> 0x%02X",
                device,
                (unsigned long)reg,
                value);
            s_service.status.modules[LASER_CONTROLLER_MODULE_IMU].detected = true;
            s_service.status.modules[LASER_CONTROLLER_MODULE_IMU].healthy = true;
        } else {
            (void)snprintf(
                buffer,
                sizeof(buffer),
                "%s reg 0x%02lX -> %s",
                device,
                (unsigned long)reg,
                esp_err_to_name(err));
            s_service.status.modules[LASER_CONTROLLER_MODULE_IMU].detected = false;
            s_service.status.modules[LASER_CONTROLLER_MODULE_IMU].healthy = false;
        }
    }

    laser_controller_service_copy_text(
        s_service.status.last_spi_op,
        sizeof(s_service.status.last_spi_op),
        buffer);
    laser_controller_service_write_action_locked(
        "SPI register read completed.",
        now_ms);

    portEXIT_CRITICAL(&s_service_lock);
}

void laser_controller_service_spi_write(
    const char *device,
    uint32_t reg,
    uint32_t value,
    laser_controller_time_ms_t now_ms)
{
    char buffer[LASER_CONTROLLER_SERVICE_TEXT_LEN];
    esp_err_t err = ESP_ERR_NOT_SUPPORTED;

    portENTER_CRITICAL(&s_service_lock);

    if (!laser_controller_service_spi_device_available_locked(device)) {
        (void)snprintf(
            buffer,
            sizeof(buffer),
            "%s reg 0x%02lX <- unavailable",
            device != NULL ? device : "unknown",
            (unsigned long)reg);
    } else {
        portEXIT_CRITICAL(&s_service_lock);
        err = laser_controller_board_imu_spi_write(
            (uint8_t)(reg & 0xFFU),
            (uint8_t)(value & 0xFFU));
        portENTER_CRITICAL(&s_service_lock);
        if (err == ESP_OK) {
            s_service.imu_regs[reg & 0xFFU] = (uint8_t)(value & 0xFFU);
            s_service.status.modules[LASER_CONTROLLER_MODULE_IMU].detected = true;
            s_service.status.modules[LASER_CONTROLLER_MODULE_IMU].healthy = true;
            (void)snprintf(
                buffer,
                sizeof(buffer),
                "%s reg 0x%02lX <- 0x%02lX",
                device,
                (unsigned long)reg,
                (unsigned long)(value & 0xFFU));
        } else {
            s_service.status.modules[LASER_CONTROLLER_MODULE_IMU].detected = false;
            s_service.status.modules[LASER_CONTROLLER_MODULE_IMU].healthy = false;
            (void)snprintf(
                buffer,
                sizeof(buffer),
                "%s reg 0x%02lX <- %s",
                device,
                (unsigned long)reg,
                esp_err_to_name(err));
        }
    }

    laser_controller_service_copy_text(
        s_service.status.last_spi_op,
        sizeof(s_service.status.last_spi_op),
        buffer);
    laser_controller_service_touch_profile_locked();
    laser_controller_service_write_action_locked(
        "SPI register write completed.",
        now_ms);

    portEXIT_CRITICAL(&s_service_lock);
}

bool laser_controller_service_parse_module(
    const char *name,
    laser_controller_module_t *module)
{
    if (name == NULL || module == NULL) {
        return false;
    }

    if (strcmp(name, "imu") == 0) {
        *module = LASER_CONTROLLER_MODULE_IMU;
    } else if (strcmp(name, "dac") == 0) {
        *module = LASER_CONTROLLER_MODULE_DAC;
    } else if (strcmp(name, "haptic") == 0) {
        *module = LASER_CONTROLLER_MODULE_HAPTIC;
    } else if (strcmp(name, "tof") == 0) {
        *module = LASER_CONTROLLER_MODULE_TOF;
    } else if (strcmp(name, "buttons") == 0) {
        *module = LASER_CONTROLLER_MODULE_BUTTONS;
    } else if (strcmp(name, "pd") == 0) {
        *module = LASER_CONTROLLER_MODULE_PD;
    } else if (strcmp(name, "laser_driver") == 0) {
        *module = LASER_CONTROLLER_MODULE_LASER_DRIVER;
    } else if (strcmp(name, "tec") == 0) {
        *module = LASER_CONTROLLER_MODULE_TEC;
    } else {
        return false;
    }

    return true;
}

const char *laser_controller_service_module_name(laser_controller_module_t module)
{
    switch (module) {
        case LASER_CONTROLLER_MODULE_IMU:
            return "imu";
        case LASER_CONTROLLER_MODULE_DAC:
            return "dac";
        case LASER_CONTROLLER_MODULE_HAPTIC:
            return "haptic";
        case LASER_CONTROLLER_MODULE_TOF:
            return "tof";
        case LASER_CONTROLLER_MODULE_BUTTONS:
            return "buttons";
        case LASER_CONTROLLER_MODULE_PD:
            return "pd";
        case LASER_CONTROLLER_MODULE_LASER_DRIVER:
            return "laser_driver";
        case LASER_CONTROLLER_MODULE_TEC:
            return "tec";
        default:
            return "unknown";
    }
}

bool laser_controller_service_parse_supply(
    const char *name,
    laser_controller_service_supply_t *supply)
{
    if (name == NULL || supply == NULL) {
        return false;
    }

    if (strcmp(name, "ld") == 0) {
        *supply = LASER_CONTROLLER_SERVICE_SUPPLY_LD;
        return true;
    }
    if (strcmp(name, "tec") == 0) {
        *supply = LASER_CONTROLLER_SERVICE_SUPPLY_TEC;
        return true;
    }

    return false;
}

const char *laser_controller_service_supply_name(
    laser_controller_service_supply_t supply)
{
    switch (supply) {
        case LASER_CONTROLLER_SERVICE_SUPPLY_LD:
            return "ld";
        case LASER_CONTROLLER_SERVICE_SUPPLY_TEC:
            return "tec";
        default:
            return "unknown";
    }
}

bool laser_controller_service_parse_dac_reference(
    const char *name,
    laser_controller_service_dac_reference_t *reference)
{
    if (name == NULL || reference == NULL) {
        return false;
    }

    if (strcmp(name, "internal") == 0) {
        *reference = LASER_CONTROLLER_SERVICE_DAC_REFERENCE_INTERNAL;
        return true;
    }
    if (strcmp(name, "external") == 0) {
        *reference = LASER_CONTROLLER_SERVICE_DAC_REFERENCE_EXTERNAL;
        return true;
    }

    return false;
}

const char *laser_controller_service_dac_reference_name(
    laser_controller_service_dac_reference_t reference)
{
    switch (reference) {
        case LASER_CONTROLLER_SERVICE_DAC_REFERENCE_INTERNAL:
            return "internal";
        case LASER_CONTROLLER_SERVICE_DAC_REFERENCE_EXTERNAL:
            return "external";
        default:
            return "unknown";
    }
}

bool laser_controller_service_parse_dac_sync_mode(
    const char *name,
    laser_controller_service_dac_sync_t *sync_mode)
{
    if (name == NULL || sync_mode == NULL) {
        return false;
    }

    if (strcmp(name, "async") == 0) {
        *sync_mode = LASER_CONTROLLER_SERVICE_DAC_SYNC_ASYNC;
        return true;
    }
    if (strcmp(name, "sync") == 0) {
        *sync_mode = LASER_CONTROLLER_SERVICE_DAC_SYNC_SYNC;
        return true;
    }

    return false;
}

const char *laser_controller_service_dac_sync_mode_name(
    laser_controller_service_dac_sync_t sync_mode)
{
    switch (sync_mode) {
        case LASER_CONTROLLER_SERVICE_DAC_SYNC_ASYNC:
            return "async";
        case LASER_CONTROLLER_SERVICE_DAC_SYNC_SYNC:
            return "sync";
        default:
            return "unknown";
    }
}

bool laser_controller_service_parse_haptic_mode(
    const char *name,
    laser_controller_service_haptic_mode_t *mode)
{
    if (name == NULL || mode == NULL) {
        return false;
    }

    if (strcmp(name, "internal_trigger") == 0) {
        *mode = LASER_CONTROLLER_SERVICE_HAPTIC_MODE_INTERNAL_TRIGGER;
    } else if (strcmp(name, "external_edge") == 0) {
        *mode = LASER_CONTROLLER_SERVICE_HAPTIC_MODE_EXTERNAL_EDGE;
    } else if (strcmp(name, "external_level") == 0) {
        *mode = LASER_CONTROLLER_SERVICE_HAPTIC_MODE_EXTERNAL_LEVEL;
    } else if (strcmp(name, "pwm_analog") == 0) {
        *mode = LASER_CONTROLLER_SERVICE_HAPTIC_MODE_PWM_ANALOG;
    } else if (strcmp(name, "audio_to_vibe") == 0) {
        *mode = LASER_CONTROLLER_SERVICE_HAPTIC_MODE_AUDIO_TO_VIBE;
    } else if (strcmp(name, "rtp") == 0) {
        *mode = LASER_CONTROLLER_SERVICE_HAPTIC_MODE_RTP;
    } else if (strcmp(name, "diagnostics") == 0) {
        *mode = LASER_CONTROLLER_SERVICE_HAPTIC_MODE_DIAGNOSTICS;
    } else if (strcmp(name, "auto_cal") == 0) {
        *mode = LASER_CONTROLLER_SERVICE_HAPTIC_MODE_AUTO_CAL;
    } else {
        return false;
    }

    return true;
}

const char *laser_controller_service_haptic_mode_name(
    laser_controller_service_haptic_mode_t mode)
{
    switch (mode) {
        case LASER_CONTROLLER_SERVICE_HAPTIC_MODE_INTERNAL_TRIGGER:
            return "internal_trigger";
        case LASER_CONTROLLER_SERVICE_HAPTIC_MODE_EXTERNAL_EDGE:
            return "external_edge";
        case LASER_CONTROLLER_SERVICE_HAPTIC_MODE_EXTERNAL_LEVEL:
            return "external_level";
        case LASER_CONTROLLER_SERVICE_HAPTIC_MODE_PWM_ANALOG:
            return "pwm_analog";
        case LASER_CONTROLLER_SERVICE_HAPTIC_MODE_AUDIO_TO_VIBE:
            return "audio_to_vibe";
        case LASER_CONTROLLER_SERVICE_HAPTIC_MODE_RTP:
            return "rtp";
        case LASER_CONTROLLER_SERVICE_HAPTIC_MODE_DIAGNOSTICS:
            return "diagnostics";
        case LASER_CONTROLLER_SERVICE_HAPTIC_MODE_AUTO_CAL:
            return "auto_cal";
        default:
            return "unknown";
    }
}

bool laser_controller_service_parse_haptic_actuator(
    const char *name,
    laser_controller_service_haptic_actuator_t *actuator)
{
    if (name == NULL || actuator == NULL) {
        return false;
    }

    if (strcmp(name, "erm") == 0) {
        *actuator = LASER_CONTROLLER_SERVICE_HAPTIC_ACTUATOR_ERM;
        return true;
    }
    if (strcmp(name, "lra") == 0) {
        *actuator = LASER_CONTROLLER_SERVICE_HAPTIC_ACTUATOR_LRA;
        return true;
    }

    return false;
}

const char *laser_controller_service_haptic_actuator_name(
    laser_controller_service_haptic_actuator_t actuator)
{
    switch (actuator) {
        case LASER_CONTROLLER_SERVICE_HAPTIC_ACTUATOR_ERM:
            return "erm";
        case LASER_CONTROLLER_SERVICE_HAPTIC_ACTUATOR_LRA:
            return "lra";
        default:
            return "unknown";
    }
}

bool laser_controller_service_parse_gpio_mode(
    const char *name,
    laser_controller_service_gpio_mode_t *mode)
{
    if (name == NULL || mode == NULL) {
        return false;
    }

    if (strcmp(name, "firmware") == 0) {
        *mode = LASER_CONTROLLER_SERVICE_GPIO_MODE_FIRMWARE;
        return true;
    }
    if (strcmp(name, "input") == 0) {
        *mode = LASER_CONTROLLER_SERVICE_GPIO_MODE_INPUT;
        return true;
    }
    if (strcmp(name, "output") == 0) {
        *mode = LASER_CONTROLLER_SERVICE_GPIO_MODE_OUTPUT;
        return true;
    }

    return false;
}

const char *laser_controller_service_gpio_mode_name(
    laser_controller_service_gpio_mode_t mode)
{
    switch (mode) {
        case LASER_CONTROLLER_SERVICE_GPIO_MODE_FIRMWARE:
            return "firmware";
        case LASER_CONTROLLER_SERVICE_GPIO_MODE_INPUT:
            return "input";
        case LASER_CONTROLLER_SERVICE_GPIO_MODE_OUTPUT:
            return "output";
        default:
            return "unknown";
    }
}
