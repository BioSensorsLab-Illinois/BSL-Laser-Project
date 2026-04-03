#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "laser_controller_config.h"
#include "laser_controller_types.h"

#define LASER_CONTROLLER_SERVICE_PROFILE_NAME_LEN 24U
#define LASER_CONTROLLER_SERVICE_TEXT_LEN 96U
#define LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT 3U
#define LASER_CONTROLLER_SERVICE_GPIO_COUNT 49U

typedef enum {
    LASER_CONTROLLER_MODULE_IMU = 0,
    LASER_CONTROLLER_MODULE_DAC,
    LASER_CONTROLLER_MODULE_HAPTIC,
    LASER_CONTROLLER_MODULE_TOF,
    LASER_CONTROLLER_MODULE_BUTTONS,
    LASER_CONTROLLER_MODULE_PD,
    LASER_CONTROLLER_MODULE_LASER_DRIVER,
    LASER_CONTROLLER_MODULE_TEC,
    LASER_CONTROLLER_MODULE_COUNT,
} laser_controller_module_t;

typedef struct {
    bool expected_present;
    bool debug_enabled;
    bool detected;
    bool healthy;
} laser_controller_module_status_t;

typedef struct {
    bool enabled;
    float voltage_v;
    float current_a;
} laser_controller_service_pd_profile_t;

typedef struct {
    bool service_mode_requested;
    float dac_ld_channel_v;
    float dac_tec_channel_v;
} laser_controller_service_dac_runtime_t;

typedef enum {
    LASER_CONTROLLER_SERVICE_SUPPLY_LD = 0,
    LASER_CONTROLLER_SERVICE_SUPPLY_TEC,
} laser_controller_service_supply_t;

typedef enum {
    LASER_CONTROLLER_SERVICE_DAC_REFERENCE_INTERNAL = 0,
    LASER_CONTROLLER_SERVICE_DAC_REFERENCE_EXTERNAL,
} laser_controller_service_dac_reference_t;

typedef enum {
    LASER_CONTROLLER_SERVICE_DAC_SYNC_ASYNC = 0,
    LASER_CONTROLLER_SERVICE_DAC_SYNC_SYNC,
} laser_controller_service_dac_sync_t;

typedef enum {
    LASER_CONTROLLER_SERVICE_HAPTIC_MODE_INTERNAL_TRIGGER = 0,
    LASER_CONTROLLER_SERVICE_HAPTIC_MODE_EXTERNAL_EDGE,
    LASER_CONTROLLER_SERVICE_HAPTIC_MODE_EXTERNAL_LEVEL,
    LASER_CONTROLLER_SERVICE_HAPTIC_MODE_PWM_ANALOG,
    LASER_CONTROLLER_SERVICE_HAPTIC_MODE_AUDIO_TO_VIBE,
    LASER_CONTROLLER_SERVICE_HAPTIC_MODE_RTP,
    LASER_CONTROLLER_SERVICE_HAPTIC_MODE_DIAGNOSTICS,
    LASER_CONTROLLER_SERVICE_HAPTIC_MODE_AUTO_CAL,
} laser_controller_service_haptic_mode_t;

typedef enum {
    LASER_CONTROLLER_SERVICE_HAPTIC_ACTUATOR_ERM = 0,
    LASER_CONTROLLER_SERVICE_HAPTIC_ACTUATOR_LRA,
} laser_controller_service_haptic_actuator_t;

typedef enum {
    LASER_CONTROLLER_SERVICE_GPIO_MODE_FIRMWARE = 0,
    LASER_CONTROLLER_SERVICE_GPIO_MODE_INPUT,
    LASER_CONTROLLER_SERVICE_GPIO_MODE_OUTPUT,
} laser_controller_service_gpio_mode_t;

typedef struct {
    bool active;
    laser_controller_service_gpio_mode_t mode;
    bool level_high;
    bool pullup_enabled;
    bool pulldown_enabled;
} laser_controller_service_gpio_override_t;

typedef struct {
    laser_controller_service_dac_reference_t reference;
    bool gain_2x;
    bool ref_div;
    laser_controller_service_dac_sync_t sync_mode;
} laser_controller_service_dac_config_t;

typedef struct {
    uint32_t odr_hz;
    uint32_t accel_range_g;
    uint32_t gyro_range_dps;
    bool gyro_enabled;
    bool lpf2_enabled;
    bool timestamp_enabled;
    bool bdu_enabled;
    bool if_inc_enabled;
    bool i2c_disabled;
} laser_controller_service_imu_config_t;

typedef struct {
    uint32_t effect_id;
    laser_controller_service_haptic_mode_t mode;
    uint32_t library;
    laser_controller_service_haptic_actuator_t actuator;
    uint32_t rtp_level;
} laser_controller_service_haptic_config_t;

typedef struct {
    bool enabled;
    uint32_t duty_cycle_pct;
    uint32_t frequency_hz;
} laser_controller_service_tof_illumination_t;

typedef struct {
    bool service_mode_requested;
    bool interlocks_disabled;
    bool persistence_dirty;
    bool persistence_available;
    bool last_save_ok;
    uint32_t profile_revision;
    char profile_name[LASER_CONTROLLER_SERVICE_PROFILE_NAME_LEN];
    bool ld_rail_debug_enabled;
    bool tec_rail_debug_enabled;
    bool haptic_driver_enable_requested;
    uint32_t gpio_override_count;
    laser_controller_service_gpio_override_t
        gpio_overrides[LASER_CONTROLLER_SERVICE_GPIO_COUNT];
    laser_controller_module_status_t modules[LASER_CONTROLLER_MODULE_COUNT];
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
    bool tof_illumination_enabled;
    uint32_t tof_illumination_duty_cycle_pct;
    uint32_t tof_illumination_frequency_hz;
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
    char last_i2c_scan[LASER_CONTROLLER_SERVICE_TEXT_LEN];
    char last_i2c_op[LASER_CONTROLLER_SERVICE_TEXT_LEN];
    char last_spi_op[LASER_CONTROLLER_SERVICE_TEXT_LEN];
    char last_action[LASER_CONTROLLER_SERVICE_TEXT_LEN];
} laser_controller_service_status_t;

void laser_controller_service_init_defaults(void);
void laser_controller_service_copy_status(laser_controller_service_status_t *status);
bool laser_controller_service_mode_requested(void);
bool laser_controller_service_interlocks_disabled(void);
bool laser_controller_service_module_expected(laser_controller_module_t module);
bool laser_controller_service_module_write_enabled(laser_controller_module_t module);
void laser_controller_service_get_dac_runtime(
    laser_controller_service_dac_runtime_t *runtime);
void laser_controller_service_get_dac_config(
    laser_controller_service_dac_config_t *config);
void laser_controller_service_get_imu_config(
    laser_controller_service_imu_config_t *config);
void laser_controller_service_get_haptic_config(
    laser_controller_service_haptic_config_t *config);
void laser_controller_service_get_tof_illumination_config(
    laser_controller_service_tof_illumination_t *config);
void laser_controller_service_report_module_probe(
    laser_controller_module_t module,
    bool detected,
    bool healthy);
void laser_controller_service_set_mode_requested(
    bool enable,
    laser_controller_time_ms_t now_ms);
void laser_controller_service_set_interlocks_disabled(
    bool enabled,
    laser_controller_time_ms_t now_ms);
bool laser_controller_service_apply_preset(
    const char *preset_name,
    laser_controller_time_ms_t now_ms);
bool laser_controller_service_set_profile_name(
    const char *profile_name,
    laser_controller_time_ms_t now_ms);
bool laser_controller_service_set_module_state(
    laser_controller_module_t module,
    bool expected_present,
    bool debug_enabled,
    laser_controller_time_ms_t now_ms);
bool laser_controller_service_set_supply_enable(
    laser_controller_service_supply_t supply,
    bool enabled,
    laser_controller_time_ms_t now_ms);
void laser_controller_service_set_haptic_driver_enable(
    bool enabled,
    laser_controller_time_ms_t now_ms);
void laser_controller_service_get_service_output_requests(
    bool *ld_rail_enabled,
    bool *tec_rail_enabled,
    bool *haptic_driver_enabled);
void laser_controller_service_mark_runtime_status(
    const char *message,
    laser_controller_time_ms_t now_ms);
esp_err_t laser_controller_service_set_dac_shadow(
    bool tec_channel,
    float voltage_v,
    laser_controller_time_ms_t now_ms);
esp_err_t laser_controller_service_set_dac_config(
    laser_controller_service_dac_reference_t reference,
    bool gain_2x,
    bool ref_div,
    laser_controller_service_dac_sync_t sync_mode,
    laser_controller_time_ms_t now_ms);
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
    laser_controller_time_ms_t now_ms);
void laser_controller_service_set_tof_config(
    float min_range_m,
    float max_range_m,
    uint32_t stale_timeout_ms,
    laser_controller_time_ms_t now_ms);
esp_err_t laser_controller_service_set_tof_illumination(
    bool enabled,
    uint32_t duty_cycle_pct,
    uint32_t frequency_hz,
    laser_controller_time_ms_t now_ms);
esp_err_t laser_controller_service_set_pd_config(
    const laser_controller_power_policy_t *power_policy,
    const laser_controller_service_pd_profile_t *profiles,
    size_t profile_count,
    laser_controller_time_ms_t now_ms);
void laser_controller_service_get_pd_config(
    laser_controller_power_policy_t *power_policy,
    laser_controller_service_pd_profile_t *profiles,
    size_t profile_count,
    bool *firmware_plan_enabled);
void laser_controller_service_set_pd_firmware_plan_enabled(
    bool enabled,
    laser_controller_time_ms_t now_ms);
esp_err_t laser_controller_service_apply_saved_pd_runtime(
    laser_controller_time_ms_t now_ms);
esp_err_t laser_controller_service_burn_pd_nvm(
    const laser_controller_service_pd_profile_t *profiles,
    size_t profile_count,
    laser_controller_time_ms_t now_ms);
void laser_controller_service_set_haptic_config(
    uint32_t effect_id,
    laser_controller_service_haptic_mode_t mode,
    uint32_t library,
    laser_controller_service_haptic_actuator_t actuator,
    uint32_t rtp_level,
    laser_controller_time_ms_t now_ms);
void laser_controller_service_fire_haptic_test(laser_controller_time_ms_t now_ms);
esp_err_t laser_controller_service_fire_haptic_trigger_pattern(
    uint32_t pulse_count,
    uint32_t high_ms,
    uint32_t low_ms,
    bool release_after,
    laser_controller_time_ms_t now_ms);
bool laser_controller_service_set_gpio_override(
    uint32_t gpio_num,
    laser_controller_service_gpio_mode_t mode,
    bool level_high,
    bool pullup_enabled,
    bool pulldown_enabled,
    laser_controller_time_ms_t now_ms);
void laser_controller_service_clear_gpio_overrides(laser_controller_time_ms_t now_ms);
bool laser_controller_service_get_gpio_override(
    uint32_t gpio_num,
    laser_controller_service_gpio_override_t *override_config);
void laser_controller_service_save_profile(laser_controller_time_ms_t now_ms);
void laser_controller_service_i2c_scan(laser_controller_time_ms_t now_ms);
void laser_controller_service_i2c_read(
    uint32_t address,
    uint32_t reg,
    laser_controller_time_ms_t now_ms);
void laser_controller_service_i2c_write(
    uint32_t address,
    uint32_t reg,
    uint32_t value,
    laser_controller_time_ms_t now_ms);
void laser_controller_service_spi_read(
    const char *device,
    uint32_t reg,
    laser_controller_time_ms_t now_ms);
void laser_controller_service_spi_write(
    const char *device,
    uint32_t reg,
    uint32_t value,
    laser_controller_time_ms_t now_ms);
bool laser_controller_service_parse_module(
    const char *name,
    laser_controller_module_t *module);
const char *laser_controller_service_module_name(laser_controller_module_t module);
bool laser_controller_service_parse_supply(
    const char *name,
    laser_controller_service_supply_t *supply);
const char *laser_controller_service_supply_name(
    laser_controller_service_supply_t supply);
bool laser_controller_service_parse_dac_reference(
    const char *name,
    laser_controller_service_dac_reference_t *reference);
const char *laser_controller_service_dac_reference_name(
    laser_controller_service_dac_reference_t reference);
bool laser_controller_service_parse_dac_sync_mode(
    const char *name,
    laser_controller_service_dac_sync_t *sync_mode);
const char *laser_controller_service_dac_sync_mode_name(
    laser_controller_service_dac_sync_t sync_mode);
bool laser_controller_service_parse_haptic_mode(
    const char *name,
    laser_controller_service_haptic_mode_t *mode);
const char *laser_controller_service_haptic_mode_name(
    laser_controller_service_haptic_mode_t mode);
bool laser_controller_service_parse_haptic_actuator(
    const char *name,
    laser_controller_service_haptic_actuator_t *actuator);
const char *laser_controller_service_haptic_actuator_name(
    laser_controller_service_haptic_actuator_t actuator);
bool laser_controller_service_parse_gpio_mode(
    const char *name,
    laser_controller_service_gpio_mode_t *mode);
const char *laser_controller_service_gpio_mode_name(
    laser_controller_service_gpio_mode_t mode);
