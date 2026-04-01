#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "laser_controller_types.h"

#define LASER_CONTROLLER_CONFIG_VERSION 1U
#define LASER_CONTROLLER_SERIAL_NUMBER_LEN 24U
#define LASER_CONTROLLER_WAVELENGTH_LUT_POINTS 16U

typedef struct {
    float programming_only_max_w;
    float reduced_mode_min_w;
    float reduced_mode_max_w;
    float full_mode_min_w;
} laser_controller_power_policy_t;

typedef struct {
    laser_controller_radians_t horizon_threshold_rad;
    laser_controller_radians_t horizon_hysteresis_rad;
    laser_controller_distance_m_t tof_min_range_m;
    laser_controller_distance_m_t tof_max_range_m;
    laser_controller_distance_m_t tof_hysteresis_m;
    laser_controller_nm_t lambda_drift_limit_nm;
    laser_controller_nm_t lambda_drift_hysteresis_nm;
    laser_controller_celsius_t ld_overtemp_limit_c;
    laser_controller_volts_t tec_temp_adc_trip_v;
    laser_controller_volts_t tec_temp_adc_hysteresis_v;
    laser_controller_celsius_t tec_min_command_c;
    laser_controller_celsius_t tec_max_command_c;
    laser_controller_celsius_t tec_ready_tolerance_c;
    laser_controller_amps_t max_laser_current_a;
    laser_controller_amps_t off_current_threshold_a;
    laser_controller_amps_t current_match_tolerance_a;
} laser_controller_safety_thresholds_t;

typedef struct {
    uint32_t imu_stale_ms;
    uint32_t tof_stale_ms;
    uint32_t pd_recheck_ms;
    uint32_t rail_good_timeout_ms;
    uint32_t lambda_drift_hold_ms;
    uint32_t tec_temp_adc_hold_ms;
    uint32_t tec_settle_timeout_ms;
} laser_controller_timeout_policy_t;

typedef struct {
    float beam_from_imu[3][3];
} laser_controller_axis_transform_t;

typedef struct {
    uint8_t point_count;
    laser_controller_nm_t wavelength_nm[LASER_CONTROLLER_WAVELENGTH_LUT_POINTS];
    laser_controller_celsius_t target_temp_c[LASER_CONTROLLER_WAVELENGTH_LUT_POINTS];
} laser_controller_wavelength_lut_t;

typedef struct {
    float ld_command_volts_per_amp;
    float tec_command_volts_per_c;
    float lio_amps_per_volt;
    float ld_tmo_c_per_volt;
} laser_controller_analog_scaling_t;

typedef struct {
    uint32_t version;
    uint32_t length_bytes;
    uint32_t hardware_revision;
    char serial_number[LASER_CONTROLLER_SERIAL_NUMBER_LEN];
    laser_controller_power_policy_t power;
    laser_controller_safety_thresholds_t thresholds;
    laser_controller_timeout_policy_t timeouts;
    laser_controller_axis_transform_t axis_transform;
    laser_controller_wavelength_lut_t wavelength_lut;
    laser_controller_analog_scaling_t analog;
    bool alignment_obeys_interlocks;
    bool require_tec_for_nir;
    uint32_t service_flags;
    uint32_t crc32;
} laser_controller_config_t;

void laser_controller_config_load_defaults(laser_controller_config_t *config);
bool laser_controller_config_validate(const laser_controller_config_t *config);
bool laser_controller_config_validate_runtime_safety(
    const laser_controller_config_t *config);
