#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "laser_controller_types.h"

#define LASER_CONTROLLER_CONFIG_VERSION 2U
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
    /*
     * Hard maximum PWM duty-cycle for the GPIO6 ToF-board front LED, as
     * an integer percent 0..100. Enforced at every LED entry point
     * (`laser_controller_board_set_tof_illumination` service path AND
     * `laser_controller_board_set_runtime_tof_illumination` runtime
     * path) — no operator request can exceed this cap, and no button-
     * policy decision can either. Prevents thermal damage to the TPS61169
     * driver + LED when held at full duty for extended periods. Default
     * 50 (user directive 2026-04-15).
     */
    uint32_t max_tof_led_duty_cycle_pct;
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

/*
 * VL53L1X ToF sensor calibration + ROI. Persisted in its own NVS blob
 * ("laser_ctrl" / "tof_cal") so it survives reboot and is re-applied on
 * every board-layer ToF init. Added 2026-04-15 per user directive.
 *
 * Distance mode — per VL53L1X datasheet Table 13 + ST ULD API:
 *   short:  <= 1.3 m, best ambient immunity. VCSEL period A=0x07, B=0x05.
 *   medium: <= 3.0 m. VCSEL period A=0x0B, B=0x09.
 *   long:   <= 4.0 m default. VCSEL period A=0x0F, B=0x0D.
 *
 * Timing budget — 20/33/50/100/200/500 ms. Lower = faster refresh; higher
 * = lower noise. Must be <= inter-measurement period.
 *
 * ROI (cone size) — user-configurable SPAD-array footprint. Full 16x16 is
 * the default ~27° FoV cone; 8x8 ~= 15°; 4x4 ~= 7.5° minimum. See VL53L1X
 * datasheet §6.8 and ST UM2356 §2.8 (setROI). Center SPAD is a packed
 * (row<<4)|col index into the 16x16 grid; 199 = grid centre.
 *
 * Offset — signed mm added to the raw distance reading. Calibrated against
 * an 18% grey target at a known distance (typ. 100 mm); measured_offset =
 * target_mm - raw_mm. VL53L1X register 0x001E, signed 13-bit << 2.
 *
 * Crosstalk — cover-glass-induced signal leakage. Calibrated with no
 * target in front; xtalk_cps = measured signal rate at max distance.
 * VL53L1X register 0x0016. Only applied when xtalk_enabled=true.
 */
typedef enum {
    LASER_CONTROLLER_TOF_DISTANCE_MODE_SHORT = 0,
    LASER_CONTROLLER_TOF_DISTANCE_MODE_MEDIUM = 1,
    LASER_CONTROLLER_TOF_DISTANCE_MODE_LONG = 2,
} laser_controller_tof_distance_mode_t;

typedef struct {
    laser_controller_tof_distance_mode_t distance_mode;
    uint32_t timing_budget_ms;        /* 20/33/50/100/200/500 */
    uint8_t  roi_width_spads;         /* 4..16 */
    uint8_t  roi_height_spads;        /* 4..16 */
    uint8_t  roi_center_spad;         /* 0..255; default 199 = grid centre */
    int32_t  offset_mm;               /* signed distance offset correction */
    uint32_t xtalk_cps;               /* cross-talk compensation, counts/sec */
    bool     xtalk_enabled;           /* only applied when true */
} laser_controller_tof_calibration_t;

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
