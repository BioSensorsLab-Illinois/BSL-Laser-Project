#include "laser_controller_config.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static const laser_controller_celsius_t kDefaultWavelengthTempsC[LASER_CONTROLLER_WAVELENGTH_LUT_POINTS] = {
    5.0f,
    7.5f,
    10.7f,
    15.5f,
    20.0f,
    24.6f,
    28.2f,
    29.0f,
    34.5f,
    37.4f,
    44.7f,
    46.1f,
    49.0f,
    54.5f,
    58.7f,
    65.0f,
};

static const laser_controller_nm_t kDefaultWavelengthsNm[LASER_CONTROLLER_WAVELENGTH_LUT_POINTS] = {
    771.2f,
    771.8f,
    772.8f,
    774.8f,
    776.8f,
    778.4f,
    779.8f,
    780.1f,
    780.5f,
    781.6f,
    783.0f,
    783.1f,
    783.2f,
    784.6f,
    786.1f,
    790.0f,
};

void laser_controller_config_load_defaults(laser_controller_config_t *config)
{
    if (config == NULL) {
        return;
    }

    memset(config, 0, sizeof(*config));

    config->version = LASER_CONTROLLER_CONFIG_VERSION;
    config->length_bytes = sizeof(*config);
    config->hardware_revision = 0U;
    (void)snprintf(config->serial_number, sizeof(config->serial_number), "UNPROVISIONED");

    config->power.programming_only_max_w = 30.0f;
    config->power.reduced_mode_min_w = 30.0f;
    config->power.reduced_mode_max_w = 35.0f;
    config->power.full_mode_min_w = 35.1f;

    config->thresholds.horizon_threshold_rad = 0.0f;
    config->thresholds.horizon_hysteresis_rad = 0.05236f;
    config->thresholds.tof_min_range_m = 0.20f;
    config->thresholds.tof_max_range_m = 1.00f;
    config->thresholds.tof_hysteresis_m = 0.02f;
    config->thresholds.lambda_drift_limit_nm = 5.0f;
    config->thresholds.lambda_drift_hysteresis_nm = 0.5f;
    config->thresholds.ld_overtemp_limit_c = 55.0f;
    config->thresholds.tec_temp_adc_trip_v = 2.45f;
    config->thresholds.tec_temp_adc_hysteresis_v = 0.05f;
    config->thresholds.tec_min_command_c = 15.0f;
    config->thresholds.tec_max_command_c = 35.0f;
    config->thresholds.tec_ready_tolerance_c = 0.25f;
    config->thresholds.max_laser_current_a = 5.2f;
    /*
     * Default threshold for the UNEXPECTED_CURRENT supervision.
     * Bench measurement (2026-04-16) showed ~0.3 A of residual reading
     * on the LIO current-sense when the driver is idle-biased (SBDN
     * STANDBY or ON with PCN LOW). The threshold must be comfortably
     * above that idle-bias plateau to avoid false-positive SYSTEM_MAJOR
     * latches during deployment checklist steps that drive SBDN active
     * before the ready-idle bias allowance opens. Real NIR emit current
     * is 2+ A, so 0.800 A is still orders of magnitude below an actual
     * runaway.
     */
    config->thresholds.off_current_threshold_a = 0.800f;
    config->thresholds.current_match_tolerance_a = 0.020f;
    /*
     * GPIO6 ToF-board LED hard brightness cap — 50% duty. Enforced in the
     * board layer at every LED entry point. User directive 2026-04-15 to
     * prevent thermal damage from extended full-duty operation.
     */
    config->thresholds.max_tof_led_duty_cycle_pct = 50U;
    /*
     * LD LIO ADC voltage calibration offset. User-observed bias on this
     * bench was +70 mV (2026-04-17 directive). Per-unit divider tolerance
     * may shift this; adjustable via the safety form. Validation clamps
     * to ±0.5 V — beyond that is a wiring fault, not calibration.
     */
    config->thresholds.lio_voltage_offset_v = 0.07f;

    /*
     * Per-interlock enable mask — every flag defaults to true so this
     * matches the legacy always-on behavior. See `laser_controller_config.h`
     * for the safety rationale; `interlocks_disabled` (bringup/service)
     * remains the master override and short-circuits all of these.
     */
    config->thresholds.interlocks.horizon_enabled     = true;
    config->thresholds.interlocks.distance_enabled    = true;
    config->thresholds.interlocks.lambda_drift_enabled = true;
    config->thresholds.interlocks.tec_temp_adc_enabled = true;
    config->thresholds.interlocks.imu_invalid_enabled  = true;
    config->thresholds.interlocks.imu_stale_enabled    = true;
    config->thresholds.interlocks.tof_invalid_enabled  = true;
    config->thresholds.interlocks.tof_stale_enabled    = true;
    config->thresholds.interlocks.ld_overtemp_enabled  = true;
    config->thresholds.interlocks.ld_loop_bad_enabled  = true;
    config->thresholds.interlocks.tof_low_bound_only   = false;

    config->timeouts.imu_stale_ms = 50U;
    config->timeouts.tof_stale_ms = 100U;
    config->timeouts.pd_recheck_ms = 250U;
    config->timeouts.rail_good_timeout_ms = 250U;
    config->timeouts.lambda_drift_hold_ms = 2000U;
    config->timeouts.tec_temp_adc_hold_ms = 2000U;
    config->timeouts.tec_settle_timeout_ms = 30000U;

    config->axis_transform.beam_from_imu[0][0] = 1.0f;
    config->axis_transform.beam_from_imu[1][1] = 1.0f;
    config->axis_transform.beam_from_imu[2][2] = 1.0f;

    config->wavelength_lut.point_count = LASER_CONTROLLER_WAVELENGTH_LUT_POINTS;
    memcpy(
        config->wavelength_lut.target_temp_c,
        kDefaultWavelengthTempsC,
        sizeof(kDefaultWavelengthTempsC));
    memcpy(
        config->wavelength_lut.wavelength_nm,
        kDefaultWavelengthsNm,
        sizeof(kDefaultWavelengthsNm));

    config->analog.ld_command_volts_per_amp = 0.41667f;
    config->analog.tec_command_volts_per_c = 0.03846f;
    config->analog.lio_amps_per_volt = 1.0f;
    config->analog.ld_tmo_c_per_volt = 1.0f;

    config->alignment_obeys_interlocks = true;
    config->require_tec_for_nir = true;
    config->service_flags = 0U;
    config->crc32 = 0U;
}

bool laser_controller_config_validate(const laser_controller_config_t *config)
{
    if (config == NULL) {
        return false;
    }

    if (config->version != LASER_CONTROLLER_CONFIG_VERSION) {
        return false;
    }

    if (config->length_bytes != sizeof(*config)) {
        return false;
    }

    if (config->power.programming_only_max_w <= 0.0f) {
        return false;
    }

    if (config->power.reduced_mode_min_w < config->power.programming_only_max_w) {
        return false;
    }

    if (config->power.reduced_mode_max_w < config->power.reduced_mode_min_w) {
        return false;
    }

    if (config->power.full_mode_min_w <= config->power.reduced_mode_max_w) {
        return false;
    }

    if (!laser_controller_config_validate_runtime_safety(config)) {
        return false;
    }

    if (config->wavelength_lut.point_count < 2U) {
        return false;
    }

    return true;
}

bool laser_controller_config_validate_runtime_safety(
    const laser_controller_config_t *config)
{
    if (config == NULL) {
        return false;
    }

    if (config->thresholds.tof_min_range_m <= 0.0f ||
        config->thresholds.tof_max_range_m <= config->thresholds.tof_min_range_m) {
        return false;
    }

    if (config->thresholds.horizon_hysteresis_rad <= 0.0f ||
        config->thresholds.tof_hysteresis_m <= 0.0f) {
        return false;
    }

    if (config->thresholds.lambda_drift_limit_nm <= 0.0f ||
        config->thresholds.lambda_drift_hysteresis_nm < 0.0f ||
        config->thresholds.lambda_drift_hysteresis_nm >=
            config->thresholds.lambda_drift_limit_nm) {
        return false;
    }

    if (config->thresholds.tec_temp_adc_trip_v <= 0.0f ||
        config->thresholds.tec_temp_adc_hysteresis_v < 0.0f ||
        config->thresholds.tec_temp_adc_hysteresis_v >=
            config->thresholds.tec_temp_adc_trip_v) {
        return false;
    }

    if (config->thresholds.tec_min_command_c >= config->thresholds.tec_max_command_c) {
        return false;
    }

    if (config->thresholds.tec_ready_tolerance_c <= 0.0f ||
        config->thresholds.max_laser_current_a <= 0.0f ||
        config->thresholds.off_current_threshold_a < 0.0f ||
        config->thresholds.current_match_tolerance_a < 0.0f) {
        return false;
    }

    /*
     * 2026-04-17 (audit round 3, S1 safety): clamp the runtime safety
     * policy to the hardware ceiling. Before this check, the firmware
     * accepted `max_laser_current_a = 99.99` silently — `bench.c:115
     * laser_controller_bench_max_current_allowed` then fell back to
     * `LASER_CONTROLLER_BENCH_MAX_CURRENT_A` at the emission site, so
     * the effective operational cap stayed correct, BUT the STORED
     * policy read back by `get_status` showed 99.99. An operator
     * auditing settings would see 99.99 and believe the safety cap
     * was effectively disabled. Reject here so the stored value
     * always matches what is actually enforced.
     *
     * Hard upper bound is defined in `bench.c` as
     * `LASER_CONTROLLER_BENCH_MAX_CURRENT_A` (5.2 A). Mirror it here
     * (can't include bench.c privates from config.c without a new
     * public header; the number is the same physical hardware limit).
     */
    if (config->thresholds.max_laser_current_a > 5.2f) {
        return false;
    }

    /*
     * Sanity bounds on the other thresholds that were previously
     * unbounded. These are set generously so every reasonable
     * calibration passes, but a fat-fingered entry like "155" for
     * ld_overtemp_limit_c no longer silently succeeds.
     */
    if (config->thresholds.ld_overtemp_limit_c < 20.0f ||
        config->thresholds.ld_overtemp_limit_c > 120.0f) {
        return false;
    }
    if (config->thresholds.tec_temp_adc_trip_v > 3.3f) {
        return false;
    }
    if (config->thresholds.tof_max_range_m > 10.0f ||
        config->thresholds.tof_min_range_m > 5.0f) {
        return false;
    }

    if (config->thresholds.max_tof_led_duty_cycle_pct > 100U) {
        return false;
    }

    if (config->thresholds.lio_voltage_offset_v < -0.5f ||
        config->thresholds.lio_voltage_offset_v >  0.5f) {
        return false;
    }

    if (config->timeouts.imu_stale_ms == 0U ||
        config->timeouts.tof_stale_ms == 0U ||
        config->timeouts.rail_good_timeout_ms == 0U ||
        config->timeouts.lambda_drift_hold_ms == 0U ||
        config->timeouts.tec_temp_adc_hold_ms == 0U ||
        config->timeouts.tec_settle_timeout_ms == 0U) {
        return false;
    }

    return true;
}
