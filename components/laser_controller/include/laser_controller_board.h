#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "laser_controller_buttons.h"
#include "laser_controller_config.h"
#include "laser_controller_pinmap.h"
#include "laser_controller_rgb_led.h"
#include "laser_controller_service.h"
#include "laser_controller_types.h"

/*
 * ATLS6A214 SBDN pin (GPIO13) is a THREE-STATE input per datasheet Table 1
 * (ATLS6A214D-3.pdf p.2-5):
 *   - 0 V..0.4 V  → SHUTDOWN (laser driver fully off, fast <20us)
 *   - 2.1 V..2.4 V → STANDBY  (driver idle, ~8mA, 20ms re-entry to operate)
 *   - 2.6 V..14 V → OPERATE  (driver active)
 *
 * On the MainPCB, R27=13k7 (to DVDD_3V3) and R28=29k4 (to GND) form a
 * divider that settles SBDN at ~2.25 V — in the STANDBY band — whenever the
 * MCU leaves GPIO13 in input / Hi-Z mode. This is a hardware-sanctioned
 * standby posture; see docs/hardware-recon.md and ATLS6A214D-3.pdf p.7
 * Figure 6.
 *
 * FIRMWARE INVARIANTS (from hardware-safety audit 2026-04-14):
 *  - Fault / fast beam-off MUST use SBDN_STATE_OFF (drive low). Standby is
 *    NOT the safe posture on fault — it still draws 8 mA and keeps the
 *    control loop armed.
 *  - GPIO13 pulls MUST stay disabled in every state. Enabling an internal
 *    pull would fight R27/R28 and shift the Hi-Z voltage out of the standby
 *    band.
 *  - In STANDBY, GPIO13 is reconfigured to GPIO_MODE_INPUT; in OFF and ON
 *    it is GPIO_MODE_INPUT_OUTPUT with the level driven accordingly.
 */
typedef enum {
    LASER_CONTROLLER_SBDN_STATE_OFF = 0,      /* drive LOW — fast shutdown */
    LASER_CONTROLLER_SBDN_STATE_ON = 1,       /* drive HIGH — driver operate */
    LASER_CONTROLLER_SBDN_STATE_STANDBY = 2,  /* INPUT/Hi-Z — ~2.25 V standby */
} laser_controller_sbdn_state_t;

typedef struct {
    bool enable_ld_vin;
    bool enable_tec_vin;
    bool enable_haptic_driver;
    bool enable_alignment_laser;
    laser_controller_sbdn_state_t sbdn_state;
    bool select_driver_low_current;
    /*
     * RGB status LED target state (TLC59116). Written by the control task
     * after safety evaluation, applied by the board layer on the output
     * apply path. Dirty-compare is handled inside the driver so repeated
     * identical applies incur zero I2C traffic. The GPIO6 front-LED
     * brightness continues to flow through
     * `laser_controller_board_set_runtime_tof_illumination` — the RGB
     * status LED here is a SEPARATE physical LED on the button board.
     */
    laser_controller_rgb_led_state_t rgb_led;
} laser_controller_board_outputs_t;

typedef struct {
    bool reachable;
    bool configured;
    bool ref_alarm;
    uint16_t sync_reg;
    uint16_t config_reg;
    uint16_t gain_reg;
    uint16_t status_reg;
    uint16_t data_a_reg;
    uint16_t data_b_reg;
    int32_t last_error;
} laser_controller_board_dac_readback_t;

typedef struct {
    bool reachable;
    bool attached;
    uint8_t cc_status_reg;
    uint8_t pdo_count_reg;
    uint32_t rdo_status_raw;
} laser_controller_board_pd_readback_t;

typedef struct {
    bool reachable;
    bool configured;
    uint8_t who_am_i;
    uint8_t status_reg;
    uint8_t ctrl1_xl_reg;
    uint8_t ctrl2_g_reg;
    uint8_t ctrl3_c_reg;
    uint8_t ctrl4_c_reg;
    uint8_t ctrl10_c_reg;
    int32_t last_error;
} laser_controller_board_imu_readback_t;

typedef struct {
    bool reachable;
    bool enable_pin_high;
    bool trigger_pin_high;
    uint8_t mode_reg;
    uint8_t library_reg;
    uint8_t go_reg;
    uint8_t feedback_reg;
    int32_t last_error;
} laser_controller_board_haptic_readback_t;

typedef struct {
    bool reachable;
    bool configured;
    bool interrupt_line_high;
    bool led_ctrl_asserted;
    bool data_ready;
    uint8_t boot_state;
    uint8_t range_status;
    uint16_t sensor_id;
    uint16_t distance_mm;
    int32_t last_error;
} laser_controller_board_tof_readback_t;

#define LASER_CONTROLLER_BOARD_GPIO_INSPECTOR_PIN_COUNT 36U

typedef struct {
    uint8_t gpio_num;
    uint8_t module_pin;
    bool output_capable;
    bool input_enabled;
    bool output_enabled;
    bool open_drain_enabled;
    bool pullup_enabled;
    bool pulldown_enabled;
    bool level_high;
    bool override_active;
    laser_controller_service_gpio_mode_t override_mode;
    bool override_level_high;
    bool override_pullup_enabled;
    bool override_pulldown_enabled;
} laser_controller_board_gpio_pin_readback_t;

typedef struct {
    bool any_override_active;
    uint32_t active_override_count;
    laser_controller_board_gpio_pin_readback_t
        pins[LASER_CONTROLLER_BOARD_GPIO_INSPECTOR_PIN_COUNT];
} laser_controller_board_gpio_inspector_t;

typedef enum {
    LASER_CONTROLLER_PD_SNAPSHOT_SOURCE_NONE = 0,
    LASER_CONTROLLER_PD_SNAPSHOT_SOURCE_BOOT_RECONCILE,
    LASER_CONTROLLER_PD_SNAPSHOT_SOURCE_INTEGRATE_REFRESH,
    LASER_CONTROLLER_PD_SNAPSHOT_SOURCE_CACHED,
} laser_controller_pd_snapshot_source_t;

typedef struct {
    bool ld_rail_pgood;
    bool tec_rail_pgood;
    bool ld_telemetry_valid;
    bool tec_telemetry_valid;
    bool pd_contract_valid;
    bool pd_snapshot_fresh;
    bool pd_source_is_host_only;
    float pd_negotiated_power_w;
    float pd_source_voltage_v;
    float pd_source_current_a;
    float pd_operating_current_a;
    uint8_t pd_contract_object_position;
    uint8_t pd_sink_profile_count;
    laser_controller_time_ms_t pd_last_updated_ms;
    laser_controller_pd_snapshot_source_t pd_source;
    laser_controller_service_pd_profile_t
        pd_sink_profiles[LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT];
    bool imu_data_valid;
    bool imu_data_fresh;
    laser_controller_radians_t beam_pitch_rad;
    laser_controller_radians_t beam_roll_rad;
    laser_controller_radians_t beam_yaw_rad;
    bool tof_data_valid;
    bool tof_data_fresh;
    laser_controller_distance_m_t tof_distance_m;
    bool pcn_pwm_active;
    bool tof_illumination_pwm_active;
    bool driver_loop_good;
    laser_controller_volts_t laser_driver_temp_voltage_v;
    laser_controller_celsius_t laser_driver_temp_c;
    laser_controller_volts_t laser_current_monitor_voltage_v;
    laser_controller_amps_t measured_laser_current_a;
    bool tec_temp_good;
    laser_controller_celsius_t tec_temp_c;
    laser_controller_volts_t tec_command_voltage_v;
    laser_controller_volts_t tec_temp_adc_voltage_v;
    laser_controller_amps_t tec_current_a;
    laser_controller_volts_t tec_voltage_v;
    bool comms_alive;
    bool watchdog_ok;
    bool brownout_seen;
    laser_controller_button_state_t button;
    laser_controller_board_dac_readback_t dac;
    laser_controller_board_pd_readback_t pd_readback;
    laser_controller_board_imu_readback_t imu_readback;
    laser_controller_board_haptic_readback_t haptic_readback;
    laser_controller_board_tof_readback_t tof_readback;
    /*
     * MCP23017 button-expander diagnostic readback. Published for the
     * Integrate panel to surface reachability / last I2C error.
     */
    laser_controller_button_board_readback_t buttons_readback;
    /*
     * TLC59116 RGB-LED diagnostic readback. Mirrors the last-applied
     * state so host UIs can render the current color/blink.
     */
    laser_controller_rgb_led_readback_t rgb_readback;
    laser_controller_board_gpio_inspector_t gpio_inspector;
} laser_controller_board_inputs_t;

void laser_controller_board_init_safe_defaults(void);
void laser_controller_board_read_inputs(
    laser_controller_board_inputs_t *inputs,
    const laser_controller_config_t *config);
void laser_controller_board_apply_outputs(const laser_controller_board_outputs_t *outputs);
void laser_controller_board_get_last_outputs(laser_controller_board_outputs_t *outputs);
laser_controller_time_ms_t laser_controller_board_uptime_ms(void);
esp_err_t laser_controller_board_i2c_probe(uint32_t address);
esp_err_t laser_controller_board_i2c_write(
    uint32_t address,
    const uint8_t *tx_data,
    size_t tx_len);
esp_err_t laser_controller_board_i2c_write_read(
    uint32_t address,
    const uint8_t *tx_data,
    size_t tx_len,
    uint8_t *rx_data,
    size_t rx_len);
esp_err_t laser_controller_board_configure_pd_debug(
    const laser_controller_service_pd_profile_t *profiles,
    size_t profile_count);
esp_err_t laser_controller_board_burn_pd_nvm(
    const laser_controller_service_pd_profile_t *profiles,
    size_t profile_count);
/*
 * Hard rule (2026-04-16 user directive): NO PD chip writes are allowed
 * unless an operator-initiated authorization window is currently open.
 * Calling either of the two write functions above without an active
 * authorization returns ESP_ERR_INVALID_STATE — guarantees no firmware
 * code path can ever silently renegotiate USB-PD. The comms-layer
 * handlers for `pd_debug_config`, `pd_save_firmware_plan`, and
 * `pd_burn_nvm` open this window with the appropriate hold time
 * immediately before invoking the write. Window auto-expires; nothing
 * in the auto-loop can re-open it.
 */
void laser_controller_board_authorize_pd_write_window(uint32_t hold_ms);
/*
 * Stop any active LEDC PWM on PCN and drive the GPIO LOW (LISL idle bias).
 * Required at the start of `run_deployment_sequence` so the checklist
 * starts from a known PCN=LOW state regardless of what the runtime was
 * doing before. Without this, a previous modulation cycle leaves LEDC
 * still owning the GPIO and `gpio_set_level` writes from the safe-output
 * path don't actually drive the pin. (2026-04-16 user directive.)
 */
void laser_controller_board_force_pcn_low(void);
void laser_controller_board_force_pd_refresh(void);
void laser_controller_board_force_pd_refresh_with_source(
    laser_controller_pd_snapshot_source_t source);
bool laser_controller_board_gpio_inspector_has_pin(uint32_t gpio_num);
void laser_controller_board_reset_gpio_debug_state(void);
void laser_controller_board_apply_debug_gpio_state_now(void);
void laser_controller_board_get_shared_i2c_line_levels(
    bool *sda_high,
    bool *scl_high);
esp_err_t laser_controller_board_imu_spi_read(uint8_t reg, uint8_t *value);
esp_err_t laser_controller_board_imu_spi_write(uint8_t reg, uint8_t value);
esp_err_t laser_controller_board_configure_dac_debug(
    laser_controller_service_dac_reference_t reference,
    bool gain_2x,
    bool ref_div,
    laser_controller_service_dac_sync_t sync_mode);
esp_err_t laser_controller_board_set_dac_debug_output(
    bool tec_channel,
    float voltage_v);
esp_err_t laser_controller_board_configure_imu_runtime(
    uint32_t odr_hz,
    uint32_t accel_range_g,
    uint32_t gyro_range_dps,
    bool gyro_enabled,
    bool lpf2_enabled,
    bool timestamp_enabled,
    bool bdu_enabled,
    bool if_inc_enabled,
    bool i2c_disabled);
esp_err_t laser_controller_board_configure_haptic_debug(
    uint32_t effect_id,
    laser_controller_service_haptic_mode_t mode,
    uint32_t library,
    laser_controller_service_haptic_actuator_t actuator,
    uint32_t rtp_level);
esp_err_t laser_controller_board_set_tof_illumination(
    bool enabled,
    uint32_t duty_cycle_pct,
    uint32_t frequency_hz);
esp_err_t laser_controller_board_set_runtime_tof_illumination(
    bool enabled,
    uint32_t duty_cycle_pct,
    uint32_t frequency_hz);
/*
 * Set the hard maximum duty-cycle (percent, 0..100) for the GPIO6 ToF-board
 * front LED. Applied at EVERY illumination entry point — both the service
 * path (`laser_controller_board_set_tof_illumination`) and the runtime path
 * (`laser_controller_board_set_runtime_tof_illumination`). If the caller
 * requests a duty above this cap, the board layer silently clamps it down.
 * Default is 100 (no cap). app.c pushes the value from
 * `config.thresholds.max_tof_led_duty_cycle_pct` each control tick.
 */
void laser_controller_board_set_tof_led_max_duty_pct(uint32_t cap);
uint32_t laser_controller_board_get_tof_led_max_duty_pct(void);
/*
 * Apply VL53L1X calibration at runtime. Stops ranging, writes distance
 * mode / timing budget / ROI / offset / xtalk, then restarts ranging.
 * Intended for operator-driven calibration updates from the Integrate
 * panel. The full ToF init path also reads the current service-status
 * calibration and performs the same writes at boot / after service
 * exit. Added 2026-04-15.
 */
esp_err_t laser_controller_board_tof_apply_calibration(
    const laser_controller_tof_calibration_t *cal);
esp_err_t laser_controller_board_fire_haptic_test(void);
void laser_controller_board_apply_actuator_targets(
    const laser_controller_config_t *config,
    laser_controller_amps_t high_state_current_a,
    laser_controller_celsius_t target_temp_c,
    bool modulation_enabled,
    uint32_t modulation_frequency_hz,
    uint32_t modulation_duty_cycle_pct,
    bool nir_output_enable,
    bool select_driver_low_current);

/*
 * Temporary bring-up hook for bench simulation and host-side tests.
 * This remains intentionally test-only and must never be used as the normal
 * runtime path on hardware.
 */
void laser_controller_board_inject_mock_inputs(const laser_controller_board_inputs_t *inputs);
