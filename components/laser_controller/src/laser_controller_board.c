#include "laser_controller_board.h"

#include <math.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_private/gpio.h"
#include "esp_rom_gpio.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "soc/gpio_sig_map.h"
#include "soc/io_mux_reg.h"

#include "laser_controller_service.h"

#define LASER_CONTROLLER_STUSB4500_ADDR        0x28U
#define LASER_CONTROLLER_DAC80502_ADDR         0x48U
#define LASER_CONTROLLER_DRV2605_ADDR          0x5AU
#define LASER_CONTROLLER_VL53L1X_ADDR          0x29U
#define LASER_CONTROLLER_VL53L1X_BOOT_STATE_REG 0x00E5U
#define LASER_CONTROLLER_VL53L1X_SENSOR_ID_REG  0x010FU
#define LASER_CONTROLLER_VL53L1X_SENSOR_ID      0xEACCU
#define LASER_CONTROLLER_VL53L1X_GPIO_HV_MUX_CTRL_REG 0x0030U
#define LASER_CONTROLLER_VL53L1X_GPIO_STATUS_REG      0x0031U
#define LASER_CONTROLLER_VL53L1X_VHV_TIMEOUT_REG      0x0008U
#define LASER_CONTROLLER_VL53L1X_START_VHV_FROM_PREV_REG 0x000BU
#define LASER_CONTROLLER_VL53L1X_PHASECAL_TIMEOUT_REG 0x004BU
#define LASER_CONTROLLER_VL53L1X_RANGE_TIMEOUT_A_REG  0x005EU
#define LASER_CONTROLLER_VL53L1X_RANGE_TIMEOUT_B_REG  0x0061U
#define LASER_CONTROLLER_VL53L1X_VCSEL_PERIOD_A_REG   0x0060U
#define LASER_CONTROLLER_VL53L1X_VCSEL_PERIOD_B_REG   0x0063U
#define LASER_CONTROLLER_VL53L1X_VALID_PHASE_HIGH_REG 0x0069U
#define LASER_CONTROLLER_VL53L1X_INTERMEASUREMENT_REG 0x006CU
#define LASER_CONTROLLER_VL53L1X_WOI_SD0_REG          0x0078U
#define LASER_CONTROLLER_VL53L1X_INITIAL_PHASE_SD0_REG 0x007AU
#define LASER_CONTROLLER_VL53L1X_INTERRUPT_CLEAR_REG  0x0086U
#define LASER_CONTROLLER_VL53L1X_MODE_START_REG       0x0087U
#define LASER_CONTROLLER_VL53L1X_RANGE_STATUS_REG     0x0089U
#define LASER_CONTROLLER_VL53L1X_SPADS_REG            0x008CU
#define LASER_CONTROLLER_VL53L1X_AMBIENT_RATE_REG     0x0090U
#define LASER_CONTROLLER_VL53L1X_DISTANCE_MM_REG      0x0096U
#define LASER_CONTROLLER_VL53L1X_SIGNAL_RATE_REG      0x0098U
#define LASER_CONTROLLER_VL53L1X_OSC_CALIBRATE_REG    0x00DEU
#define LASER_CONTROLLER_I2C_SPEED_HZ          100000U
#define LASER_CONTROLLER_I2C_TIMEOUT_MS        50
#define LASER_CONTROLLER_PD_POLL_MS            100U
#define LASER_CONTROLLER_TOF_POLL_MS           100U
#define LASER_CONTROLLER_TOF_BOOT_RETRY_MS     2U
#define LASER_CONTROLLER_TOF_BOOT_TIMEOUT_MS   20U
#define LASER_CONTROLLER_TOF_INIT_READY_TIMEOUT_MS 100U
#define LASER_CONTROLLER_TOF_INTERMEASUREMENT_MS 75U
#define LASER_CONTROLLER_TOF_TIMING_BUDGET_MS  50U
#define LASER_CONTROLLER_SPI_HOST              SPI2_HOST
#define LASER_CONTROLLER_IMU_SPI_HZ            1000000
#define LASER_CONTROLLER_DAC_SYNC_REG          0x02U
#define LASER_CONTROLLER_DAC_CONFIG_REG        0x03U
#define LASER_CONTROLLER_DAC_GAIN_REG          0x04U
#define LASER_CONTROLLER_DAC_TRIGGER_REG       0x05U
#define LASER_CONTROLLER_DAC_STATUS_REG        0x07U
#define LASER_CONTROLLER_DAC_DATA_A_REG        0x08U
#define LASER_CONTROLLER_DAC_DATA_B_REG        0x09U
#define LASER_CONTROLLER_DAC_SOFT_RESET_CODE   0x000AU
#define LASER_CONTROLLER_DAC_ZERO_SCALE_CODE   0x0000U
#define LASER_CONTROLLER_DAC_GAIN_DEFAULT      0x0103U
#define LASER_CONTROLLER_DAC_SYNC_DEFAULT      0x0300U
#define LASER_CONTROLLER_DAC_CONFIG_DEFAULT    0x0000U
#define LASER_CONTROLLER_DAC_REF_V             2.5f
#define LASER_CONTROLLER_DAC_FULL_SCALE_V      2.5f
#define LASER_CONTROLLER_DAC_CODE_MAX          65535U
#define LASER_CONTROLLER_DAC_STATUS_REFALARM_BIT 0x0001U
#define LASER_CONTROLLER_LSM6DSO_WHOAMI_REG    0x0FU
#define LASER_CONTROLLER_LSM6DSO_WHOAMI_VALUE  0x6CU
#define LASER_CONTROLLER_LSM6DSO_CTRL1_XL_REG   0x10U
#define LASER_CONTROLLER_LSM6DSO_CTRL2_G_REG    0x11U
#define LASER_CONTROLLER_LSM6DSO_CTRL3_C_REG    0x12U
#define LASER_CONTROLLER_LSM6DSO_CTRL4_C_REG    0x13U
#define LASER_CONTROLLER_LSM6DSO_CTRL10_C_REG   0x19U
#define LASER_CONTROLLER_LSM6DSO_STATUS_REG     0x1EU
#define LASER_CONTROLLER_LSM6DSO_OUTX_L_G_REG   0x22U
#define LASER_CONTROLLER_LSM6DSO_OUTX_L_A_REG   0x28U
#define LASER_CONTROLLER_LSM6DSO_RESET_BIT      0x01U
#define LASER_CONTROLLER_LSM6DSO_IF_INC_BIT     0x04U
#define LASER_CONTROLLER_LSM6DSO_BDU_BIT        0x40U
#define LASER_CONTROLLER_LSM6DSO_I2C_DISABLE_BIT 0x04U
#define LASER_CONTROLLER_LSM6DSO_TIMESTAMP_BIT  0x20U
#define LASER_CONTROLLER_LSM6DSO_GDA_BIT        0x02U
#define LASER_CONTROLLER_LSM6DSO_XLDA_BIT       0x01U
#define LASER_CONTROLLER_STUSB_CC_STATUS_REG   0x11U
#define LASER_CONTROLLER_STUSB_PD_COMMAND_CTRL_REG 0x1AU
#define LASER_CONTROLLER_STUSB_DPM_PDO_NUMB    0x70U
#define LASER_CONTROLLER_STUSB_DPM_PDO1_0      0x85U
#define LASER_CONTROLLER_STUSB_RDO_STATUS_0    0x91U
#define LASER_CONTROLLER_STUSB_FTP_KEY_REG     0x95U
#define LASER_CONTROLLER_STUSB_FTP_CTRL_0_REG  0x96U
#define LASER_CONTROLLER_STUSB_FTP_CTRL_1_REG  0x97U
#define LASER_CONTROLLER_STUSB_FTP_RW_BUFFER_REG 0x53U
#define LASER_CONTROLLER_STUSB_FTP_PASSWORD    0x47U
#define LASER_CONTROLLER_STUSB_FTP_RST_N_BIT   0x40U
#define LASER_CONTROLLER_STUSB_FTP_REQ_BIT     0x10U
#define LASER_CONTROLLER_STUSB_FTP_SECTOR_MASK 0x07U
#define LASER_CONTROLLER_STUSB_FTP_SER_MASK    0xF8U
#define LASER_CONTROLLER_STUSB_FTP_OPCODE_MASK 0x07U
#define LASER_CONTROLLER_STUSB_FTP_OPCODE_READ 0x00U
#define LASER_CONTROLLER_STUSB_FTP_OPCODE_WRITE_PL 0x01U
#define LASER_CONTROLLER_STUSB_FTP_OPCODE_WRITE_SER 0x02U
#define LASER_CONTROLLER_STUSB_FTP_OPCODE_ERASE 0x05U
#define LASER_CONTROLLER_STUSB_FTP_OPCODE_PROGRAM 0x06U
#define LASER_CONTROLLER_STUSB_FTP_OPCODE_SOFT_PROGRAM 0x07U
#define LASER_CONTROLLER_STUSB_TX_HEADER_LOW_REG 0x51U
#define LASER_CONTROLLER_STUSB_SOFT_RESET_PAYLOAD 0x0DU
#define LASER_CONTROLLER_STUSB_SEND_COMMAND     0x26U
#define LASER_CONTROLLER_DRV2605_MODE_REG      0x01U
#define LASER_CONTROLLER_DRV2605_RTP_REG       0x02U
#define LASER_CONTROLLER_DRV2605_LIBRARY_REG   0x03U
#define LASER_CONTROLLER_DRV2605_WAVESEQ1_REG  0x04U
#define LASER_CONTROLLER_DRV2605_GO_REG        0x0CU
#define LASER_CONTROLLER_DRV2605_FEEDBACK_REG  0x1AU

static void laser_controller_board_normalize_pd_profiles(
    const laser_controller_service_pd_profile_t *profiles,
    laser_controller_service_pd_profile_t *normalized_profiles,
    uint8_t *profile_count_encoded);
static void laser_controller_board_release_pcn_pwm_if_needed(uint32_t gpio_num);
static void laser_controller_board_release_tof_sideband_if_needed(uint32_t gpio_num);
#define LASER_CONTROLLER_DRV2605_STANDBY_BIT   0x40U
#define LASER_CONTROLLER_DRV2605_RESET_BIT     0x80U
#define LASER_CONTROLLER_PCN_PWM_SPEED_MODE    LEDC_LOW_SPEED_MODE
#define LASER_CONTROLLER_PCN_PWM_TIMER         LEDC_TIMER_0
#define LASER_CONTROLLER_PCN_PWM_CHANNEL       LEDC_CHANNEL_0
#define LASER_CONTROLLER_PCN_PWM_RESOLUTION    LEDC_TIMER_8_BIT
#define LASER_CONTROLLER_PCN_PWM_DUTY_MAX      ((1U << 8U) - 1U)
#define LASER_CONTROLLER_TOF_LED_PWM_SPEED_MODE LEDC_LOW_SPEED_MODE
#define LASER_CONTROLLER_TOF_LED_PWM_TIMER      LEDC_TIMER_1
#define LASER_CONTROLLER_TOF_LED_PWM_CHANNEL    LEDC_CHANNEL_1
#define LASER_CONTROLLER_TOF_LED_PWM_RESOLUTION LEDC_TIMER_8_BIT
#define LASER_CONTROLLER_TOF_LED_PWM_DUTY_MAX   ((1U << 8U) - 1U)
#define LASER_CONTROLLER_TOF_LED_PWM_DEFAULT_FREQ_HZ 20000U
#define LASER_CONTROLLER_TOF_LED_PWM_MIN_FREQ_HZ 5000U
#define LASER_CONTROLLER_TOF_LED_PWM_MAX_FREQ_HZ 100000U
#define LASER_CONTROLLER_DAC_RETRY_MS          500U
#define LASER_CONTROLLER_DAC_RESET_SETTLE_US   2000U
#define LASER_CONTROLLER_IMU_RETRY_MS          500U
#define LASER_CONTROLLER_STUSB_NVM_BANK_COUNT  5U
#define LASER_CONTROLLER_STUSB_NVM_BANK_SIZE   8U
#define LASER_CONTROLLER_STUSB_NVM_ALL_BANKS_MASK 0x1FU
#define LASER_CONTROLLER_STUSB_NVM_COMMAND_TIMEOUT_MS 20U
#define LASER_CONTROLLER_STUSB_NVM_RESET_DELAY_US 10U
#define LASER_CONTROLLER_PD_SNAPSHOT_FRESH_MS   5000U
#define LASER_CONTROLLER_TWO_PI_RAD            6.28318530717958647692f
#define LASER_CONTROLLER_PI_RAD                3.14159265358979323846f
#define LASER_CONTROLLER_RAD_PER_DEG           0.01745329251994329577f

typedef struct {
    bool attached;
    bool host_only;
    bool contract_valid;
    float negotiated_power_w;
    float source_voltage_v;
    float source_current_a;
    float operating_current_a;
    uint8_t contract_object_position;
    uint8_t sink_profile_count;
    laser_controller_service_pd_profile_t
        sink_profiles[LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT];
    laser_controller_time_ms_t updated_ms;
    laser_controller_pd_snapshot_source_t source;
} laser_controller_pd_snapshot_t;

typedef struct {
    float voltage_v;
    float current_a;
} laser_controller_pd_pdo_t;

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
} laser_controller_board_imu_config_t;

typedef struct {
    bool identified;
    bool configured;
    esp_err_t last_error;
    laser_controller_time_ms_t last_attempt_ms;
    laser_controller_time_ms_t last_sample_ms;
    laser_controller_time_ms_t last_orientation_ms;
    float accel_g[3];
    float gyro_dps[3];
    laser_controller_radians_t beam_pitch_rad;
    laser_controller_radians_t beam_roll_rad;
    laser_controller_radians_t beam_yaw_rad;
    laser_controller_board_imu_config_t config;
} laser_controller_board_imu_runtime_t;

typedef struct {
    bool reachable;
    bool configured;
    bool ranging;
    bool data_ready;
    uint8_t boot_state;
    uint8_t range_status;
    uint8_t interrupt_polarity;
    uint8_t accepted_sample_count;
    uint8_t accepted_sample_index;
    uint16_t sensor_id;
    uint16_t raw_distance_mm;
    uint16_t distance_mm;
    uint16_t accepted_samples_mm[3];
    esp_err_t last_error;
    laser_controller_time_ms_t last_attempt_ms;
    laser_controller_time_ms_t last_sample_ms;
} laser_controller_board_tof_runtime_t;

typedef struct {
    bool enabled;
    uint32_t duty_cycle_pct;
    uint32_t frequency_hz;
} laser_controller_board_tof_illumination_t;

typedef enum {
    LASER_CONTROLLER_TOF_SIDEBAND_MODE_FLOAT = 0,
    LASER_CONTROLLER_TOF_SIDEBAND_MODE_LOW,
    LASER_CONTROLLER_TOF_SIDEBAND_MODE_HIGH,
    LASER_CONTROLLER_TOF_SIDEBAND_MODE_PWM,
    LASER_CONTROLLER_TOF_SIDEBAND_MODE_OVERRIDE,
} laser_controller_board_tof_sideband_mode_t;

static const uint8_t kVl53l1xDefaultConfiguration[] = {
    0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x02, 0x08, 0x00, 0x08, 0x10,
    0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x0f, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x20, 0x0b, 0x00, 0x00, 0x02, 0x0a, 0x21, 0x00,
    0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0xc8, 0x00, 0x00, 0x38, 0xff,
    0x01, 0x00, 0x08, 0x00, 0x00, 0x01, 0xcc, 0x0f, 0x01, 0xf1, 0x0d,
    0x01, 0x68, 0x00, 0x80, 0x08, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x0f,
    0x89, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x0f, 0x0d,
    0x0e, 0x0e, 0x00, 0x00, 0x02, 0xc7, 0xff, 0x9b, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00
};

static const laser_controller_board_outputs_t kSafeOutputs = {
    .enable_ld_vin = false,
    .enable_tec_vin = false,
    .enable_haptic_driver = false,
    .enable_alignment_laser = false,
    .sbdn_state = LASER_CONTROLLER_SBDN_STATE_OFF,
    .select_driver_low_current = true,
    .rgb_led = {
        .r = 0U,
        .g = 0U,
        .b = 0U,
        .blink = false,
        .enabled = false,
    },
};

static const struct {
    float voltage_v;
    float temp_c;
} kTecTempCalibration[] = {
    { 0.180f, 6.0f },
    { 0.221f, 7.5f },
    { 0.316f, 10.7f },
    { 0.473f, 15.5f },
    { 0.606f, 20.0f },
    { 0.630f, 20.5f },
    { 0.957f, 28.2f },
    { 0.985f, 29.0f },
    { 1.345f, 37.4f },
    { 1.640f, 44.7f },
    { 1.700f, 46.1f },
    { 2.030f, 54.5f },
    { 2.182f, 58.7f },
    { 2.429f, 65.0f },
};

typedef struct {
    uint8_t gpio_num;
    uint8_t module_pin;
} laser_controller_board_gpio_descriptor_t;

static const laser_controller_board_gpio_descriptor_t kGpioInspectorPins[
    LASER_CONTROLLER_BOARD_GPIO_INSPECTOR_PIN_COUNT] = {
    { 4U, 4U },   { 5U, 5U },   { 6U, 6U },   { 7U, 7U },
    { 15U, 8U },  { 16U, 9U },  { 17U, 10U }, { 18U, 11U },
    { 8U, 12U },  { 19U, 13U }, { 20U, 14U }, { 3U, 15U },
    { 46U, 16U }, { 9U, 17U },  { 10U, 18U }, { 11U, 19U },
    { 12U, 20U }, { 13U, 21U }, { 14U, 22U }, { 21U, 23U },
    { 47U, 24U }, { 48U, 25U }, { 45U, 26U }, { 0U, 27U },
    { 35U, 28U }, { 36U, 29U }, { 37U, 30U }, { 38U, 31U },
    { 39U, 32U }, { 40U, 33U }, { 41U, 34U }, { 42U, 35U },
    { 44U, 36U }, { 43U, 37U }, { 2U, 38U },  { 1U, 39U },
};

static bool s_mock_override_enabled;
static laser_controller_board_inputs_t s_mock_inputs;
static laser_controller_board_outputs_t s_last_outputs = {
    .enable_ld_vin = false,
    .enable_tec_vin = false,
    .enable_haptic_driver = false,
    .enable_alignment_laser = false,
    .sbdn_state = LASER_CONTROLLER_SBDN_STATE_OFF,
    .select_driver_low_current = true,
};
static adc_oneshot_unit_handle_t s_adc_handle;
static bool s_adc_ready;
static i2c_master_bus_handle_t s_i2c_bus;
static bool s_i2c_ready;
static spi_device_handle_t s_imu_spi;
static bool s_spi_ready;
static bool s_dac_ready;
static esp_err_t s_dac_last_error;
static laser_controller_time_ms_t s_dac_last_attempt_ms;
static bool s_gpio_ready;
static bool s_pwm_active;
static bool s_tof_illumination_pwm_active;
static bool s_tof_runtime_owns_sideband;
static laser_controller_board_tof_sideband_mode_t s_tof_sideband_mode;
static uint32_t s_tof_sideband_applied_duty_cycle_pct;
static uint32_t s_tof_sideband_applied_frequency_hz;
static laser_controller_pd_snapshot_t s_pd_snapshot;
static bool s_pd_refresh_allowed;
static laser_controller_pd_snapshot_source_t s_pd_refresh_requested_source;
/*
 * 2026-04-16 HARD RULE: NO PD chip writes without operator approval.
 * `s_pd_write_authorization_until_ms` is set non-zero by an operator-
 * initiated comms handler immediately before calling a PD-write function
 * (`configure_pd_debug` or `burn_pd_nvm`). The write check rejects with
 * ESP_ERR_INVALID_STATE if the window isn't open OR has expired. The
 * window auto-expires; nothing in the control loop can re-open it.
 * This means no firmware path — including future regressions — can
 * silently renegotiate USB-PD or burn NVM.
 *
 * Volatile because the comms task writes and the control task reads
 * without locks (single-word atomic on ESP32-S3).
 */
static volatile laser_controller_time_ms_t s_pd_write_authorization_until_ms;
static float s_last_ld_voltage_v = -1.0f;
static float s_last_tec_voltage_v = -1.0f;
static laser_controller_board_imu_runtime_t s_imu_runtime;
static laser_controller_board_tof_runtime_t s_tof_runtime;
static laser_controller_board_tof_illumination_t s_tof_illumination;
static laser_controller_board_dac_readback_t s_dac_readback;
static laser_controller_board_pd_readback_t s_pd_readback;
static laser_controller_board_imu_readback_t s_imu_readback;
static laser_controller_board_haptic_readback_t s_haptic_readback;
static laser_controller_board_tof_readback_t s_tof_readback;
static laser_controller_board_gpio_inspector_t s_gpio_inspector;
static laser_controller_time_ms_t s_tof_last_poll_ms;
static esp_err_t s_tof_last_error;
static SemaphoreHandle_t s_bus_mutex;
static StaticSemaphore_t s_bus_mutex_buffer;
/*
 * Button board + RGB LED state. All reads/writes are on the control task
 * (same ownership as the other peripheral readbacks) so no additional
 * lock is required. The ISR increments an atomic counter inside
 * laser_controller_buttons.c; the control task drains it via
 * laser_controller_buttons_get_isr_fire_count().
 */
static laser_controller_button_board_readback_t s_button_readback;
static laser_controller_rgb_led_readback_t s_rgb_readback;
static laser_controller_button_state_t s_last_button_state;
/*
 * Hard safety cap on GPIO6 ToF LED duty-cycle. Default 100 (no cap) until
 * app.c pushes the configured value from `safety.max_tof_led_duty_cycle_pct`.
 * Written by control task; read by set_tof_illumination + set_runtime_*.
 * A cap of 0 disables the LED entirely.
 */
static uint32_t s_tof_led_max_duty_pct = 100U;

static void laser_controller_board_lock_bus(void)
{
    if (s_bus_mutex != NULL) {
        (void)xSemaphoreTake(s_bus_mutex, portMAX_DELAY);
    }
}

static void laser_controller_board_unlock_bus(void)
{
    if (s_bus_mutex != NULL) {
        (void)xSemaphoreGive(s_bus_mutex);
    }
}

static float laser_controller_board_clamp_float(float value, float minimum, float maximum)
{
    if (value < minimum) {
        return minimum;
    }

    if (value > maximum) {
        return maximum;
    }

    return value;
}

static uint32_t laser_controller_board_clamp_u32(
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

static bool laser_controller_board_is_shared_i2c_gpio(uint32_t gpio_num)
{
    return gpio_num == LASER_CONTROLLER_GPIO_SHARED_I2C_SDA ||
           gpio_num == LASER_CONTROLLER_GPIO_SHARED_I2C_SCL;
}

static bool laser_controller_board_is_imu_spi_gpio(uint32_t gpio_num)
{
    return gpio_num == LASER_CONTROLLER_GPIO_IMU_SDI ||
           gpio_num == LASER_CONTROLLER_GPIO_IMU_CS ||
           gpio_num == LASER_CONTROLLER_GPIO_IMU_SCLK ||
           gpio_num == LASER_CONTROLLER_GPIO_IMU_SDO;
}

static bool laser_controller_board_is_transport_or_strap_gpio(uint32_t gpio_num)
{
    return gpio_num == LASER_CONTROLLER_GPIO_USB_D_N ||
           gpio_num == LASER_CONTROLLER_GPIO_USB_D_P ||
           gpio_num == LASER_CONTROLLER_GPIO_UART0_RX ||
           gpio_num == LASER_CONTROLLER_GPIO_UART0_TX ||
           gpio_num == LASER_CONTROLLER_GPIO_BOOT_BUTTON ||
           gpio_num == LASER_CONTROLLER_GPIO_BOOT_OPTION ||
           gpio_num == LASER_CONTROLLER_GPIO_JTAG_STRAP_OPEN ||
           gpio_num == LASER_CONTROLLER_GPIO_VDD_SPI_STRAP_OPEN;
}

/*
 * ADC1-bound analog input pins used for telemetry (LD TMO, LD LIO,
 * TEC TMO, TEC ITEC, TEC VTEC). These are configured ONCE at boot via
 * `adc_oneshot_config_channel` inside `ensure_adc_ready`, and the
 * configuration is never re-applied. Any path that calls
 * `gpio_reset_pin` on these pins clobbers the ADC analog configuration:
 * IOMUX goes back to `PIN_FUNC_GPIO`, the digital input buffer state
 * flips, and the default pull state is reapplied — all of which can
 * shift the analog readback or add noise. The GPIO-override sweep in
 * `laser_controller_board_reset_gpio_debug_state` previously hit these
 * pins on every service-mode exit / deployment entry / overrides-clear
 * transition. Excluding them here preserves the ADC pin state across
 * those transitions. User directive 2026-04-15 (late).
 */
static bool laser_controller_board_is_adc_input_gpio(uint32_t gpio_num)
{
    return gpio_num == LASER_CONTROLLER_GPIO_LD_DRIVER_TEMP_MONITOR ||
           gpio_num == LASER_CONTROLLER_GPIO_LD_CURRENT_MONITOR ||
           gpio_num == LASER_CONTROLLER_GPIO_TEC_TMO ||
           gpio_num == LASER_CONTROLLER_GPIO_TEC_ITEC ||
           gpio_num == LASER_CONTROLLER_GPIO_TEC_VTEC;
}

static bool laser_controller_board_gpio_override_active(uint32_t gpio_num)
{
    laser_controller_service_gpio_override_t override_config;

    return laser_controller_service_get_gpio_override(gpio_num, &override_config) &&
           override_config.active;
}

static const laser_controller_board_gpio_descriptor_t *
laser_controller_board_find_gpio_descriptor(uint32_t gpio_num)
{
    for (size_t index = 0U;
         index < LASER_CONTROLLER_BOARD_GPIO_INSPECTOR_PIN_COUNT;
         ++index) {
        if (kGpioInspectorPins[index].gpio_num == gpio_num) {
            return &kGpioInspectorPins[index];
        }
    }

    return NULL;
}

bool laser_controller_board_gpio_inspector_has_pin(uint32_t gpio_num)
{
    return laser_controller_board_find_gpio_descriptor(gpio_num) != NULL;
}

static void laser_controller_board_apply_gpio_overrides(void)
{
    gpio_config_t config = { 0 };
    laser_controller_service_gpio_override_t override_config;

    for (size_t index = 0U;
         index < LASER_CONTROLLER_BOARD_GPIO_INSPECTOR_PIN_COUNT;
         ++index) {
        const uint32_t gpio_num = kGpioInspectorPins[index].gpio_num;

        if (!laser_controller_service_get_gpio_override(gpio_num, &override_config) ||
            !override_config.active) {
            continue;
        }

        laser_controller_board_release_pcn_pwm_if_needed(gpio_num);
        laser_controller_board_release_tof_sideband_if_needed(gpio_num);
        memset(&config, 0, sizeof(config));
        config.pin_bit_mask = (1ULL << gpio_num);
        config.mode = override_config.mode == LASER_CONTROLLER_SERVICE_GPIO_MODE_INPUT ?
                          GPIO_MODE_INPUT :
                          GPIO_MODE_INPUT_OUTPUT;
        config.pull_up_en =
            override_config.pullup_enabled ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
        config.pull_down_en =
            override_config.pulldown_enabled ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
        config.intr_type = GPIO_INTR_DISABLE;
        (void)gpio_config(&config);

        if (override_config.mode == LASER_CONTROLLER_SERVICE_GPIO_MODE_OUTPUT) {
            (void)gpio_set_level(gpio_num, override_config.level_high ? 1 : 0);
        }
    }
}

static void laser_controller_board_release_pcn_pwm_if_needed(uint32_t gpio_num)
{
    if (gpio_num != LASER_CONTROLLER_GPIO_LD_PCN) {
        return;
    }

    if (s_pwm_active) {
        (void)ledc_stop(
            LASER_CONTROLLER_PCN_PWM_SPEED_MODE,
            LASER_CONTROLLER_PCN_PWM_CHANNEL,
            0U);
        s_pwm_active = false;
    }

    (void)gpio_reset_pin((gpio_num_t)gpio_num);
    gpio_func_sel(gpio_num, PIN_FUNC_GPIO);
    esp_rom_gpio_connect_out_signal(gpio_num, SIG_GPIO_OUT_IDX, false, false);
}

static void laser_controller_board_capture_gpio_inspector(void)
{
    gpio_io_config_t io_config = { 0 };
    laser_controller_service_gpio_override_t override_config;

    memset(&s_gpio_inspector, 0, sizeof(s_gpio_inspector));

    for (size_t index = 0U;
         index < LASER_CONTROLLER_BOARD_GPIO_INSPECTOR_PIN_COUNT;
         ++index) {
        laser_controller_board_gpio_pin_readback_t *pin = &s_gpio_inspector.pins[index];
        const uint32_t gpio_num = kGpioInspectorPins[index].gpio_num;

        pin->gpio_num = (uint8_t)gpio_num;
        pin->module_pin = kGpioInspectorPins[index].module_pin;
        pin->output_capable = GPIO_IS_VALID_OUTPUT_GPIO((int)gpio_num);
        pin->level_high = gpio_get_level(gpio_num) != 0;

        if (gpio_get_io_config((gpio_num_t)gpio_num, &io_config) == ESP_OK) {
            pin->input_enabled = io_config.ie;
            pin->output_enabled = io_config.oe;
            pin->open_drain_enabled = io_config.od;
            pin->pullup_enabled = io_config.pu;
            pin->pulldown_enabled = io_config.pd;
        }

        if (laser_controller_service_get_gpio_override(gpio_num, &override_config) &&
            override_config.active) {
            pin->override_active = true;
            pin->override_mode = override_config.mode;
            pin->override_level_high = override_config.level_high;
            pin->override_pullup_enabled = override_config.pullup_enabled;
            pin->override_pulldown_enabled = override_config.pulldown_enabled;
            s_gpio_inspector.any_override_active = true;
            s_gpio_inspector.active_override_count += 1U;
        } else {
            pin->override_mode = LASER_CONTROLLER_SERVICE_GPIO_MODE_FIRMWARE;
        }
    }
}

static void laser_controller_board_load_default_imu_config(
    laser_controller_board_imu_config_t *config)
{
    if (config == NULL) {
        return;
    }

    config->odr_hz = 208U;
    config->accel_range_g = 4U;
    config->gyro_range_dps = 500U;
    config->gyro_enabled = true;
    config->lpf2_enabled = false;
    config->timestamp_enabled = true;
    config->bdu_enabled = true;
    config->if_inc_enabled = true;
    config->i2c_disabled = true;
}

static uint8_t laser_controller_board_imu_odr_bits(uint32_t odr_hz)
{
    switch (odr_hz) {
        case 12U:
        case 13U:
            return 0x01U;
        case 26U:
            return 0x02U;
        case 52U:
            return 0x03U;
        case 104U:
            return 0x04U;
        case 208U:
            return 0x05U;
        case 416U:
            return 0x06U;
        case 833U:
            return 0x07U;
        case 1660U:
            return 0x08U;
        case 3330U:
            return 0x09U;
        case 6660U:
            return 0x0AU;
        default:
            return 0x05U;
    }
}

static uint8_t laser_controller_board_imu_accel_fs_bits(uint32_t accel_range_g)
{
    switch (accel_range_g) {
        case 2U:
            return 0x00U;
        case 16U:
            return 0x01U;
        case 4U:
            return 0x02U;
        case 8U:
            return 0x03U;
        default:
            return 0x02U;
    }
}

static float laser_controller_board_imu_accel_g_per_lsb(uint32_t accel_range_g)
{
    switch (accel_range_g) {
        case 2U:
            return 0.000061f;
        case 4U:
            return 0.000122f;
        case 8U:
            return 0.000244f;
        case 16U:
            return 0.000488f;
        default:
            return 0.000122f;
    }
}

static float laser_controller_board_imu_gyro_dps_per_lsb(uint32_t gyro_range_dps)
{
    switch (gyro_range_dps) {
        case 125U:
            return 0.004375f;
        case 250U:
            return 0.008750f;
        case 500U:
            return 0.017500f;
        case 1000U:
            return 0.035000f;
        case 2000U:
            return 0.070000f;
        default:
            return 0.017500f;
    }
}

static uint8_t laser_controller_board_imu_gyro_ctrl2_value(
    uint32_t odr_hz,
    uint32_t gyro_range_dps,
    bool gyro_enabled)
{
    uint8_t value = 0U;

    if (!gyro_enabled) {
        return value;
    }

    value = (uint8_t)(laser_controller_board_imu_odr_bits(odr_hz) << 4U);
    switch (gyro_range_dps) {
        case 125U:
            value |= 0x02U;
            break;
        case 250U:
            value |= (0x00U << 2U);
            break;
        case 500U:
            value |= (0x01U << 2U);
            break;
        case 1000U:
            value |= (0x02U << 2U);
            break;
        case 2000U:
            value |= (0x03U << 2U);
            break;
        default:
            value |= (0x01U << 2U);
            break;
    }

    return value;
}

static void laser_controller_board_service_report_probe(
    laser_controller_module_t module,
    bool detected,
    bool healthy)
{
    laser_controller_service_report_module_probe(module, detected, healthy);
}

static void laser_controller_board_stop_tof_illumination_pwm(void)
{
    if (s_tof_illumination_pwm_active) {
        (void)ledc_stop(
            LASER_CONTROLLER_TOF_LED_PWM_SPEED_MODE,
            LASER_CONTROLLER_TOF_LED_PWM_CHANNEL,
            0U);
        s_tof_illumination_pwm_active = false;
    }
}

static bool laser_controller_board_service_owns_tof_sideband(void)
{
    return laser_controller_service_mode_requested() &&
           laser_controller_service_module_write_enabled(
               LASER_CONTROLLER_MODULE_TOF);
}

static bool laser_controller_board_any_owns_tof_sideband(void)
{
    return laser_controller_board_service_owns_tof_sideband() ||
           s_tof_runtime_owns_sideband;
}

static void laser_controller_board_release_tof_sideband_if_needed(
    uint32_t gpio_num)
{
    if (gpio_num != LASER_CONTROLLER_GPIO_TOF_LED_CTRL) {
        return;
    }

    laser_controller_board_stop_tof_illumination_pwm();
    (void)gpio_reset_pin((gpio_num_t)gpio_num);
    gpio_func_sel(gpio_num, PIN_FUNC_GPIO);
    esp_rom_gpio_connect_out_signal(gpio_num, SIG_GPIO_OUT_IDX, false, false);
}

static esp_err_t laser_controller_board_apply_tof_sideband_state_locked(
    bool anyone_owns_sideband)
{
    /*
     * NOTE ON PARAMETER SEMANTICS:
     * The caller must pass `laser_controller_board_any_owns_tof_sideband()`
     * — i.e. TRUE whenever SERVICE OR RUNTIME owns GPIO6. A FALSE parameter
     * forces the sideband LOW. A previous bug at capture_tof_readback passed
     * `service_owns_tof_sideband` only, which overwrote runtime ownership
     * every TOF poll and caused GPIO6 to flicker on Operate and flash-then-off
     * on Integrate. The parameter was renamed from `service_owns_sideband` to
     * `anyone_owns_sideband` to make this contract obvious at every call site.
     */
    gpio_config_t config = { 0 };
    esp_err_t err = ESP_OK;
    laser_controller_board_tof_sideband_mode_t desired_mode =
        LASER_CONTROLLER_TOF_SIDEBAND_MODE_FLOAT;
    const uint32_t desired_frequency_hz = laser_controller_board_clamp_u32(
        s_tof_illumination.frequency_hz,
        LASER_CONTROLLER_TOF_LED_PWM_MIN_FREQ_HZ,
        LASER_CONTROLLER_TOF_LED_PWM_MAX_FREQ_HZ);
    uint32_t desired_duty_cycle_pct = laser_controller_board_clamp_u32(
        s_tof_illumination.duty_cycle_pct,
        0U,
        100U);

    if (laser_controller_board_gpio_override_active(
            LASER_CONTROLLER_GPIO_TOF_LED_CTRL)) {
        laser_controller_board_release_tof_sideband_if_needed(
            LASER_CONTROLLER_GPIO_TOF_LED_CTRL);
        s_tof_sideband_mode = LASER_CONTROLLER_TOF_SIDEBAND_MODE_OVERRIDE;
        s_tof_sideband_applied_duty_cycle_pct = 0U;
        s_tof_sideband_applied_frequency_hz = 0U;
        return ESP_OK;
    }

    config.pin_bit_mask = (1ULL << LASER_CONTROLLER_GPIO_TOF_LED_CTRL);
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;

    if (!anyone_owns_sideband) {
        desired_mode = LASER_CONTROLLER_TOF_SIDEBAND_MODE_LOW;
    } else if (!s_tof_illumination.enabled || desired_duty_cycle_pct == 0U) {
        desired_mode = LASER_CONTROLLER_TOF_SIDEBAND_MODE_LOW;
    } else if (desired_duty_cycle_pct >= 100U) {
        desired_mode = LASER_CONTROLLER_TOF_SIDEBAND_MODE_HIGH;
    } else {
        desired_mode = LASER_CONTROLLER_TOF_SIDEBAND_MODE_PWM;
    }

    if (s_tof_sideband_mode == desired_mode &&
        (desired_mode != LASER_CONTROLLER_TOF_SIDEBAND_MODE_PWM ||
         (s_tof_sideband_applied_duty_cycle_pct == desired_duty_cycle_pct &&
          s_tof_sideband_applied_frequency_hz == desired_frequency_hz))) {
        return ESP_OK;
    }

    laser_controller_board_release_tof_sideband_if_needed(
        LASER_CONTROLLER_GPIO_TOF_LED_CTRL);

    if (desired_mode == LASER_CONTROLLER_TOF_SIDEBAND_MODE_FLOAT) {
        config.mode = GPIO_MODE_INPUT;
        (void)gpio_config(&config);
        s_tof_sideband_mode = desired_mode;
        s_tof_sideband_applied_duty_cycle_pct = 0U;
        s_tof_sideband_applied_frequency_hz = 0U;
        return ESP_OK;
    }

    config.mode = GPIO_MODE_INPUT_OUTPUT;
    err = gpio_config(&config);
    if (err != ESP_OK) {
        return err;
    }

    if (desired_mode == LASER_CONTROLLER_TOF_SIDEBAND_MODE_PWM) {
        ledc_timer_config_t timer_config = {
            .speed_mode = LASER_CONTROLLER_TOF_LED_PWM_SPEED_MODE,
            .duty_resolution = LASER_CONTROLLER_TOF_LED_PWM_RESOLUTION,
            .timer_num = LASER_CONTROLLER_TOF_LED_PWM_TIMER,
            .freq_hz = desired_frequency_hz,
            .clk_cfg = LEDC_AUTO_CLK,
            .deconfigure = false,
        };
        ledc_channel_config_t channel_config = {
            .gpio_num = LASER_CONTROLLER_GPIO_TOF_LED_CTRL,
            .speed_mode = LASER_CONTROLLER_TOF_LED_PWM_SPEED_MODE,
            .channel = LASER_CONTROLLER_TOF_LED_PWM_CHANNEL,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = LASER_CONTROLLER_TOF_LED_PWM_TIMER,
            .duty = (laser_controller_board_clamp_u32(
                        desired_duty_cycle_pct,
                        1U,
                        99U) *
                     LASER_CONTROLLER_TOF_LED_PWM_DUTY_MAX) /
                    100U,
            .hpoint = 0,
            .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
            .flags = { .output_invert = 0 },
            .deconfigure = false,
        };

        err = ledc_timer_config(&timer_config);
        if (err == ESP_OK) {
            err = ledc_channel_config(&channel_config);
        }

        if (err == ESP_OK) {
            s_tof_illumination_pwm_active = true;
            s_tof_sideband_mode = desired_mode;
            s_tof_sideband_applied_duty_cycle_pct = desired_duty_cycle_pct;
            s_tof_sideband_applied_frequency_hz = desired_frequency_hz;
            return ESP_OK;
        }
    }

    (void)gpio_set_level(
        LASER_CONTROLLER_GPIO_TOF_LED_CTRL,
        desired_mode == LASER_CONTROLLER_TOF_SIDEBAND_MODE_HIGH ? 1 : 0);
    s_tof_sideband_mode = desired_mode;
    s_tof_sideband_applied_duty_cycle_pct =
        desired_mode == LASER_CONTROLLER_TOF_SIDEBAND_MODE_HIGH ? 100U : 0U;
    s_tof_sideband_applied_frequency_hz = 0U;
    return err;
}

static void laser_controller_board_apply_tof_sideband_state(bool anyone_owns_sideband)
{
    (void)laser_controller_board_apply_tof_sideband_state_locked(anyone_owns_sideband);
}

static void laser_controller_board_drive_safe_gpio_levels(
    const laser_controller_board_outputs_t *outputs)
{
    const laser_controller_board_outputs_t effective =
        outputs != NULL ? *outputs : kSafeOutputs;

    if (!laser_controller_board_gpio_override_active(
            LASER_CONTROLLER_GPIO_PWR_TEC_EN)) {
        (void)gpio_set_level(
            LASER_CONTROLLER_GPIO_PWR_TEC_EN,
            effective.enable_tec_vin ? 1 : 0);
    }
    if (!laser_controller_board_gpio_override_active(
            LASER_CONTROLLER_GPIO_PWR_LD_EN)) {
        (void)gpio_set_level(
            LASER_CONTROLLER_GPIO_PWR_LD_EN,
            effective.enable_ld_vin ? 1 : 0);
    }
    if (!laser_controller_board_gpio_override_active(
            LASER_CONTROLLER_GPIO_LD_SBDN)) {
        /*
         * Three-state SBDN drive (ATLS6A214 datasheet Table 1; MainPCB
         * R27=13k7 to DVDD_3V3, R28=29k4 to GND → Hi-Z settles at 2.25 V
         * in the documented 2.1..2.4 V standby band).
         *
         * Pulls stay disabled at all times — internal pull-up/down would
         * fight the R27/R28 divider and shift the Hi-Z voltage out of the
         * standby band. This was enforced at ensure_gpio_ready init-time
         * and is NOT altered per state transition here.
         */
        switch (effective.sbdn_state) {
            case LASER_CONTROLLER_SBDN_STATE_ON:
                (void)gpio_set_direction(
                    LASER_CONTROLLER_GPIO_LD_SBDN,
                    GPIO_MODE_INPUT_OUTPUT);
                (void)gpio_set_level(
                    LASER_CONTROLLER_GPIO_LD_SBDN,
                    1);
                break;
            case LASER_CONTROLLER_SBDN_STATE_STANDBY:
                /*
                 * Reconfigure to input-only so the external R27/R28
                 * divider, not the MCU driver, owns the pin. Level reads
                 * become meaningful only as the observed idle voltage.
                 */
                (void)gpio_set_direction(
                    LASER_CONTROLLER_GPIO_LD_SBDN,
                    GPIO_MODE_INPUT);
                break;
            case LASER_CONTROLLER_SBDN_STATE_OFF:
            default:
                (void)gpio_set_direction(
                    LASER_CONTROLLER_GPIO_LD_SBDN,
                    GPIO_MODE_INPUT_OUTPUT);
                (void)gpio_set_level(
                    LASER_CONTROLLER_GPIO_LD_SBDN,
                    0);
                break;
        }
    }
    if (!laser_controller_board_gpio_override_active(
            LASER_CONTROLLER_GPIO_LD_PCN)) {
        (void)gpio_set_level(
            LASER_CONTROLLER_GPIO_LD_PCN,
            effective.select_driver_low_current ? 0 : 1);
    }
    if (!laser_controller_board_gpio_override_active(
            LASER_CONTROLLER_GPIO_ERM_TRIG_GN_LD_EN)) {
        (void)gpio_set_level(
            LASER_CONTROLLER_GPIO_ERM_TRIG_GN_LD_EN,
            effective.enable_alignment_laser ? 1 : 0);
    }
    if (!laser_controller_board_gpio_override_active(
            LASER_CONTROLLER_GPIO_ERM_EN)) {
        (void)gpio_set_level(
            LASER_CONTROLLER_GPIO_ERM_EN,
            effective.enable_haptic_driver ? 1 : 0);
    }
}

/*
 * GPIO7 / BUTTON_INTA is wired to the MCP23017 INTA pin, which is
 * configured open-drain active-low (IOCON.ODR=1). ESP side therefore
 * requires an internal pull-up to hold the line high when the expander
 * is quiescent. The ISR fires on falling edge. The service routine is
 * installed in `laser_controller_board_ensure_gpio_ready` below.
 *
 * NOTE: This pin was previously allocated as LASER_CONTROLLER_GPIO_TOF_GPIO1_INT
 * (VL53L1X data-ready). The ToF daughterboard now runs in polling-only
 * mode because the button board shares the same physical J2 connector —
 * see docs/firmware-pinmap.md and the 2026-04-15 hardware-recon update.
 */
static void IRAM_ATTR laser_controller_board_button_inta_isr(void *arg)
{
    (void)arg;
    laser_controller_buttons_on_isr_fired();
}

static esp_err_t laser_controller_board_ensure_gpio_ready(void)
{
    const uint64_t output_mask =
        (1ULL << LASER_CONTROLLER_GPIO_PWR_TEC_EN) |
        (1ULL << LASER_CONTROLLER_GPIO_PWR_LD_EN) |
        (1ULL << LASER_CONTROLLER_GPIO_LD_SBDN) |
        (1ULL << LASER_CONTROLLER_GPIO_LD_PCN) |
        (1ULL << LASER_CONTROLLER_GPIO_ERM_EN) |
        (1ULL << LASER_CONTROLLER_GPIO_ERM_TRIG_GN_LD_EN);
    /*
     * Input pins that must stay pull-up / pull-down DISABLED — these are
     * analog-adjacent or comparator-driven safety inputs where an internal
     * pull would fight the board network. GPIO7 (BUTTON_INTA) is NOT in
     * this mask because it needs the internal pull-up; it is configured
     * separately below.
     */
    const uint64_t input_mask =
        (1ULL << LASER_CONTROLLER_GPIO_PWR_TEC_PGOOD) |
        (1ULL << LASER_CONTROLLER_GPIO_PWR_LD_PGOOD) |
        (1ULL << LASER_CONTROLLER_GPIO_LD_LPGD) |
        (1ULL << LASER_CONTROLLER_GPIO_TEC_TEMPGD) |
        (1ULL << LASER_CONTROLLER_GPIO_IMU_INT2);
    gpio_config_t config = { 0 };
    esp_err_t err;

    if (s_gpio_ready) {
        return ESP_OK;
    }

    config.pin_bit_mask = output_mask;
    config.mode = GPIO_MODE_INPUT_OUTPUT;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    err = gpio_config(&config);
    if (err != ESP_OK) {
        return err;
    }

    config.pin_bit_mask = input_mask;
    config.mode = GPIO_MODE_INPUT;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    err = gpio_config(&config);
    if (err != ESP_OK) {
        return err;
    }

    /*
     * Button board INTA — separate config because (a) pull-up is required
     * for the open-drain input, (b) it is the only GPIO that gets an ISR
     * hooked in this project. Installing the ISR service is idempotent
     * per ESP-IDF.
     */
    config.pin_bit_mask = (1ULL << LASER_CONTROLLER_GPIO_BUTTON_INTA);
    config.mode = GPIO_MODE_INPUT;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.pull_up_en = GPIO_PULLUP_ENABLE;
    config.intr_type = GPIO_INTR_NEGEDGE;
    err = gpio_config(&config);
    if (err != ESP_OK) {
        return err;
    }

    /*
     * ESP_ERR_INVALID_STATE here just means the ISR service is already
     * installed (another module got there first). That is acceptable.
     */
    err = gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    err = gpio_isr_handler_add(
        LASER_CONTROLLER_GPIO_BUTTON_INTA,
        laser_controller_board_button_inta_isr,
        NULL);
    if (err != ESP_OK) {
        return err;
    }

    s_gpio_ready = true;
    laser_controller_board_drive_safe_gpio_levels(&kSafeOutputs);
    return ESP_OK;
}

static esp_err_t laser_controller_board_ensure_adc_ready(void)
{
    adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = ADC_UNIT_1,
        .clk_src = 0,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    static const adc_channel_t kChannels[] = {
        ADC_CHANNEL_0,
        ADC_CHANNEL_1,
        ADC_CHANNEL_7,
        ADC_CHANNEL_8,
        ADC_CHANNEL_9,
    };
    esp_err_t err;

    if (s_adc_ready) {
        return ESP_OK;
    }

    err = adc_oneshot_new_unit(&unit_config, &s_adc_handle);
    if (err != ESP_OK) {
        return err;
    }

    for (size_t index = 0U; index < sizeof(kChannels) / sizeof(kChannels[0]); ++index) {
        err = adc_oneshot_config_channel(s_adc_handle, kChannels[index], &channel_config);
        if (err != ESP_OK) {
            return err;
        }
    }

    s_adc_ready = true;
    return ESP_OK;
}

static esp_err_t laser_controller_board_ensure_i2c_ready(void)
{
    i2c_master_bus_config_t config = {
        .i2c_port = -1,
        .sda_io_num = LASER_CONTROLLER_GPIO_SHARED_I2C_SDA,
        .scl_io_num = LASER_CONTROLLER_GPIO_SHARED_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = 1,
            .allow_pd = 0,
        },
    };
    esp_err_t err;

    if (s_i2c_ready) {
        return ESP_OK;
    }

    err = i2c_new_master_bus(&config, &s_i2c_bus);
    if (err == ESP_OK) {
        s_i2c_ready = true;
    }

    return err;
}

static void laser_controller_board_get_shared_i2c_levels_internal(
    bool *sda_high,
    bool *scl_high)
{
    if (sda_high != NULL) {
        *sda_high = gpio_get_level(LASER_CONTROLLER_GPIO_SHARED_I2C_SDA) != 0;
    }

    if (scl_high != NULL) {
        *scl_high = gpio_get_level(LASER_CONTROLLER_GPIO_SHARED_I2C_SCL) != 0;
    }
}

static void laser_controller_board_recover_shared_i2c_bus_locked(void)
{
    gpio_config_t config = { 0 };
    bool bus_detached = true;

    if (s_i2c_ready && s_i2c_bus != NULL) {
        esp_err_t detach_err = ESP_OK;

        (void)i2c_master_bus_wait_all_done(
            s_i2c_bus,
            LASER_CONTROLLER_I2C_TIMEOUT_MS);
        (void)i2c_master_bus_reset(s_i2c_bus);
        detach_err = i2c_del_master_bus(s_i2c_bus);
        if (detach_err == ESP_OK) {
            s_i2c_bus = NULL;
            s_i2c_ready = false;
        } else {
            bus_detached = false;
        }
    }

    if (!bus_detached) {
        return;
    }

    config.pin_bit_mask =
        (1ULL << LASER_CONTROLLER_GPIO_SHARED_I2C_SDA) |
        (1ULL << LASER_CONTROLLER_GPIO_SHARED_I2C_SCL);
    config.mode = GPIO_MODE_INPUT_OUTPUT_OD;
    config.pull_up_en = GPIO_PULLUP_ENABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    (void)gpio_config(&config);

    (void)gpio_set_level(LASER_CONTROLLER_GPIO_SHARED_I2C_SDA, 1);
    (void)gpio_set_level(LASER_CONTROLLER_GPIO_SHARED_I2C_SCL, 1);
    esp_rom_delay_us(5U);

    for (uint32_t pulse = 0U; pulse < 9U; ++pulse) {
        (void)gpio_set_level(LASER_CONTROLLER_GPIO_SHARED_I2C_SCL, 0);
        esp_rom_delay_us(5U);
        (void)gpio_set_level(LASER_CONTROLLER_GPIO_SHARED_I2C_SCL, 1);
        esp_rom_delay_us(5U);
    }

    /* Generate a best-effort STOP condition before handing the bus back. */
    (void)gpio_set_level(LASER_CONTROLLER_GPIO_SHARED_I2C_SDA, 0);
    esp_rom_delay_us(5U);
    (void)gpio_set_level(LASER_CONTROLLER_GPIO_SHARED_I2C_SCL, 1);
    esp_rom_delay_us(5U);
    (void)gpio_set_level(LASER_CONTROLLER_GPIO_SHARED_I2C_SDA, 1);
    esp_rom_delay_us(5U);
}

static void laser_controller_board_recover_shared_i2c_bus(void)
{
    laser_controller_board_lock_bus();
    laser_controller_board_recover_shared_i2c_bus_locked();
    laser_controller_board_unlock_bus();
}

static bool laser_controller_board_i2c_error_needs_recovery(esp_err_t err)
{
    return err == ESP_ERR_TIMEOUT ||
           err == ESP_FAIL ||
           err == ESP_ERR_INVALID_STATE;
}

static esp_err_t laser_controller_board_ensure_spi_ready(void)
{
    spi_bus_config_t bus_config = {
        .mosi_io_num = LASER_CONTROLLER_GPIO_IMU_SDI,
        .miso_io_num = LASER_CONTROLLER_GPIO_IMU_SDO,
        .sclk_io_num = LASER_CONTROLLER_GPIO_IMU_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .data4_io_num = -1,
        .data5_io_num = -1,
        .data6_io_num = -1,
        .data7_io_num = -1,
        .max_transfer_sz = 16,
        .flags = SPICOMMON_BUSFLAG_MASTER |
                 SPICOMMON_BUSFLAG_MOSI |
                 SPICOMMON_BUSFLAG_MISO |
                 SPICOMMON_BUSFLAG_SCLK,
        .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
        .intr_flags = 0,
    };
    spi_device_interface_config_t device_config = {
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
        .mode = 3,
        .clock_source = SPI_CLK_SRC_DEFAULT,
        .duty_cycle_pos = 128,
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
        .clock_speed_hz = LASER_CONTROLLER_IMU_SPI_HZ,
        .input_delay_ns = 0,
        .sample_point = SPI_SAMPLING_POINT_PHASE_0,
        .spics_io_num = LASER_CONTROLLER_GPIO_IMU_CS,
        .flags = 0,
        .queue_size = 1,
        .pre_cb = NULL,
        .post_cb = NULL,
    };
    esp_err_t err;

    if (s_spi_ready) {
        return ESP_OK;
    }

    err = spi_bus_initialize(
        LASER_CONTROLLER_SPI_HOST,
        &bus_config,
        SPI_DMA_DISABLED);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = spi_bus_add_device(
        LASER_CONTROLLER_SPI_HOST,
        &device_config,
        &s_imu_spi);
    if (err == ESP_OK) {
        s_spi_ready = true;
    }

    return err;
}

static esp_err_t laser_controller_board_i2c_tx(
    uint32_t address,
    const uint8_t *tx_data,
    size_t tx_len)
{
    esp_err_t err = ESP_OK;

    if (tx_data == NULL || tx_len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    for (uint32_t attempt = 0U; attempt < 2U; ++attempt) {
        i2c_master_dev_handle_t device = NULL;
        i2c_device_config_t config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = (uint16_t)(address & 0x7FU),
            .scl_speed_hz = LASER_CONTROLLER_I2C_SPEED_HZ,
            .scl_wait_us = 0U,
            .flags = {
                .disable_ack_check = 0,
            },
        };

        err = laser_controller_board_ensure_i2c_ready();
        if (err != ESP_OK) {
            if (attempt == 0U && laser_controller_board_i2c_error_needs_recovery(err)) {
                laser_controller_board_recover_shared_i2c_bus();
                continue;
            }
            return err;
        }

        laser_controller_board_lock_bus();
        err = i2c_master_bus_add_device(s_i2c_bus, &config, &device);
        if (err == ESP_OK) {
            err = i2c_master_transmit(
                device,
                tx_data,
                tx_len,
                LASER_CONTROLLER_I2C_TIMEOUT_MS);
            (void)i2c_master_bus_rm_device(device);
        }
        laser_controller_board_unlock_bus();

        if (err == ESP_OK || !laser_controller_board_i2c_error_needs_recovery(err) ||
            attempt > 0U) {
            return err;
        }

        laser_controller_board_recover_shared_i2c_bus();
    }

    return err;
}

static esp_err_t laser_controller_board_i2c_txrx(
    uint32_t address,
    const uint8_t *tx_data,
    size_t tx_len,
    uint8_t *rx_data,
    size_t rx_len)
{
    esp_err_t err = ESP_OK;

    if (tx_data == NULL || tx_len == 0U || rx_data == NULL || rx_len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    for (uint32_t attempt = 0U; attempt < 2U; ++attempt) {
        i2c_master_dev_handle_t device = NULL;
        i2c_device_config_t config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = (uint16_t)(address & 0x7FU),
            .scl_speed_hz = LASER_CONTROLLER_I2C_SPEED_HZ,
            .scl_wait_us = 0U,
            .flags = {
                .disable_ack_check = 0,
            },
        };

        err = laser_controller_board_ensure_i2c_ready();
        if (err != ESP_OK) {
            if (attempt == 0U && laser_controller_board_i2c_error_needs_recovery(err)) {
                laser_controller_board_recover_shared_i2c_bus();
                continue;
            }
            return err;
        }

        laser_controller_board_lock_bus();
        err = i2c_master_bus_add_device(s_i2c_bus, &config, &device);
        if (err == ESP_OK) {
            err = i2c_master_transmit_receive(
                device,
                tx_data,
                tx_len,
                rx_data,
                rx_len,
                LASER_CONTROLLER_I2C_TIMEOUT_MS);
            (void)i2c_master_bus_rm_device(device);
        }
        laser_controller_board_unlock_bus();

        if (err == ESP_OK || !laser_controller_board_i2c_error_needs_recovery(err) ||
            attempt > 0U) {
            return err;
        }

        laser_controller_board_recover_shared_i2c_bus();
    }

    return err;
}

static esp_err_t laser_controller_board_i2c_read_reg_u8(
    uint32_t address,
    uint8_t reg,
    uint8_t *value)
{
    return laser_controller_board_i2c_txrx(address, &reg, 1U, value, 1U);
}

static esp_err_t laser_controller_board_i2c_write_reg16_u8(
    uint32_t address,
    uint16_t reg,
    uint8_t value)
{
    const uint8_t tx[3] = {
        (uint8_t)((reg >> 8U) & 0xFFU),
        (uint8_t)(reg & 0xFFU),
        value,
    };

    return laser_controller_board_i2c_tx(address, tx, sizeof(tx));
}

static esp_err_t laser_controller_board_i2c_write_reg16_u16(
    uint32_t address,
    uint16_t reg,
    uint16_t value)
{
    const uint8_t tx[4] = {
        (uint8_t)((reg >> 8U) & 0xFFU),
        (uint8_t)(reg & 0xFFU),
        (uint8_t)((value >> 8U) & 0xFFU),
        (uint8_t)(value & 0xFFU),
    };

    return laser_controller_board_i2c_tx(address, tx, sizeof(tx));
}

static esp_err_t laser_controller_board_i2c_write_reg16_u32(
    uint32_t address,
    uint16_t reg,
    uint32_t value)
{
    const uint8_t tx[6] = {
        (uint8_t)((reg >> 8U) & 0xFFU),
        (uint8_t)(reg & 0xFFU),
        (uint8_t)((value >> 24U) & 0xFFU),
        (uint8_t)((value >> 16U) & 0xFFU),
        (uint8_t)((value >> 8U) & 0xFFU),
        (uint8_t)(value & 0xFFU),
    };

    return laser_controller_board_i2c_tx(address, tx, sizeof(tx));
}

static esp_err_t laser_controller_board_i2c_write_reg16_block(
    uint32_t address,
    uint16_t reg,
    const uint8_t *values,
    size_t len)
{
    uint8_t buffer[2U + sizeof(kVl53l1xDefaultConfiguration)];

    if (values == NULL || len == 0U || len > sizeof(kVl53l1xDefaultConfiguration)) {
        return ESP_ERR_INVALID_ARG;
    }

    buffer[0] = (uint8_t)((reg >> 8U) & 0xFFU);
    buffer[1] = (uint8_t)(reg & 0xFFU);
    memcpy(&buffer[2], values, len);
    return laser_controller_board_i2c_tx(address, buffer, len + 2U);
}

static esp_err_t laser_controller_board_i2c_read_reg16_u8(
    uint32_t address,
    uint16_t reg,
    uint8_t *value)
{
    const uint8_t tx[2] = {
        (uint8_t)((reg >> 8U) & 0xFFU),
        (uint8_t)(reg & 0xFFU),
    };

    return laser_controller_board_i2c_txrx(address, tx, sizeof(tx), value, 1U);
}

static esp_err_t laser_controller_board_i2c_read_reg16_u16(
    uint32_t address,
    uint16_t reg,
    uint16_t *value)
{
    uint8_t rx[2] = { 0 };
    const uint8_t tx[2] = {
        (uint8_t)((reg >> 8U) & 0xFFU),
        (uint8_t)(reg & 0xFFU),
    };
    esp_err_t err;

    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    err = laser_controller_board_i2c_txrx(address, tx, sizeof(tx), rx, sizeof(rx));
    if (err != ESP_OK) {
        return err;
    }

    *value = (uint16_t)(((uint16_t)rx[0] << 8U) | rx[1]);
    return ESP_OK;
}

static esp_err_t laser_controller_board_i2c_read_stusb_block(
    uint8_t start_reg,
    uint8_t *buffer,
    size_t len)
{
    return laser_controller_board_i2c_txrx(
        LASER_CONTROLLER_STUSB4500_ADDR,
        &start_reg,
        1U,
        buffer,
        len);
}

static esp_err_t laser_controller_board_i2c_write_stusb_block(
    uint8_t start_reg,
    const uint8_t *buffer,
    size_t len)
{
    uint8_t tx[1U + LASER_CONTROLLER_STUSB_NVM_BANK_SIZE];

    if (buffer == NULL || len == 0U || len > LASER_CONTROLLER_STUSB_NVM_BANK_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }

    tx[0] = start_reg;
    memcpy(&tx[1], buffer, len);
    return laser_controller_board_i2c_write(
        LASER_CONTROLLER_STUSB4500_ADDR,
        tx,
        len + 1U);
}

static esp_err_t laser_controller_board_stusb_nvm_wait_ready(
    laser_controller_time_ms_t timeout_ms)
{
    const laser_controller_time_ms_t start_ms =
        laser_controller_board_uptime_ms();
    uint8_t ctrl0 = 0U;
    esp_err_t err;

    do {
        err = laser_controller_board_i2c_read_reg_u8(
            LASER_CONTROLLER_STUSB4500_ADDR,
            LASER_CONTROLLER_STUSB_FTP_CTRL_0_REG,
            &ctrl0);
        if (err != ESP_OK) {
            return err;
        }
        if ((ctrl0 & LASER_CONTROLLER_STUSB_FTP_REQ_BIT) == 0U) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    } while ((laser_controller_board_uptime_ms() - start_ms) < timeout_ms);

    return ESP_ERR_TIMEOUT;
}

static esp_err_t laser_controller_board_stusb_nvm_exit_test_mode(void)
{
    static const uint8_t kExitSequence[2] = {
        LASER_CONTROLLER_STUSB_FTP_RST_N_BIT,
        0x00U,
    };
    esp_err_t err;

    err = laser_controller_board_i2c_write_stusb_block(
        LASER_CONTROLLER_STUSB_FTP_CTRL_0_REG,
        kExitSequence,
        sizeof(kExitSequence));
    if (err != ESP_OK) {
        return err;
    }

    return laser_controller_board_i2c_write(
        LASER_CONTROLLER_STUSB4500_ADDR,
        (const uint8_t[]){
            LASER_CONTROLLER_STUSB_FTP_KEY_REG,
            0x00U,
        },
        2U);
}

static esp_err_t laser_controller_board_stusb_nvm_enter_read_mode(void)
{
    esp_err_t err;

    err = laser_controller_board_i2c_write(
        LASER_CONTROLLER_STUSB4500_ADDR,
        (const uint8_t[]){
            LASER_CONTROLLER_STUSB_FTP_KEY_REG,
            LASER_CONTROLLER_STUSB_FTP_PASSWORD,
        },
        2U);
    if (err != ESP_OK) {
        return err;
    }

    err = laser_controller_board_i2c_write(
        LASER_CONTROLLER_STUSB4500_ADDR,
        (const uint8_t[]){
            LASER_CONTROLLER_STUSB_FTP_CTRL_0_REG,
            LASER_CONTROLLER_STUSB_FTP_RST_N_BIT,
        },
        2U);
    if (err != ESP_OK) {
        return err;
    }

    err = laser_controller_board_i2c_write(
        LASER_CONTROLLER_STUSB4500_ADDR,
        (const uint8_t[]){
            LASER_CONTROLLER_STUSB_FTP_CTRL_0_REG,
            0x00U,
        },
        2U);
    if (err != ESP_OK) {
        return err;
    }

    esp_rom_delay_us(LASER_CONTROLLER_STUSB_NVM_RESET_DELAY_US);

    return laser_controller_board_i2c_write(
        LASER_CONTROLLER_STUSB4500_ADDR,
        (const uint8_t[]){
            LASER_CONTROLLER_STUSB_FTP_CTRL_0_REG,
            LASER_CONTROLLER_STUSB_FTP_RST_N_BIT,
        },
        2U);
}

static esp_err_t laser_controller_board_stusb_nvm_read_bank(
    uint8_t bank_index,
    uint8_t *bank_data)
{
    esp_err_t err;

    if (bank_data == NULL || bank_index >= LASER_CONTROLLER_STUSB_NVM_BANK_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    err = laser_controller_board_i2c_write(
        LASER_CONTROLLER_STUSB4500_ADDR,
        (const uint8_t[]){
            LASER_CONTROLLER_STUSB_FTP_CTRL_1_REG,
            LASER_CONTROLLER_STUSB_FTP_OPCODE_READ,
        },
        2U);
    if (err != ESP_OK) {
        return err;
    }

    err = laser_controller_board_i2c_write(
        LASER_CONTROLLER_STUSB4500_ADDR,
        (const uint8_t[]){
            LASER_CONTROLLER_STUSB_FTP_CTRL_0_REG,
            (uint8_t)(LASER_CONTROLLER_STUSB_FTP_RST_N_BIT |
                       LASER_CONTROLLER_STUSB_FTP_REQ_BIT |
                       (bank_index & LASER_CONTROLLER_STUSB_FTP_SECTOR_MASK)),
        },
        2U);
    if (err != ESP_OK) {
        return err;
    }

    err = laser_controller_board_stusb_nvm_wait_ready(
        LASER_CONTROLLER_STUSB_NVM_COMMAND_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    return laser_controller_board_i2c_read_stusb_block(
        LASER_CONTROLLER_STUSB_FTP_RW_BUFFER_REG,
        bank_data,
        LASER_CONTROLLER_STUSB_NVM_BANK_SIZE);
}

static esp_err_t laser_controller_board_stusb_nvm_read_all(
    uint8_t bank_data[LASER_CONTROLLER_STUSB_NVM_BANK_COUNT]
                     [LASER_CONTROLLER_STUSB_NVM_BANK_SIZE])
{
    esp_err_t err;

    if (bank_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    err = laser_controller_board_stusb_nvm_enter_read_mode();
    if (err != ESP_OK) {
        return err;
    }

    for (uint8_t bank_index = 0U;
         bank_index < LASER_CONTROLLER_STUSB_NVM_BANK_COUNT;
         ++bank_index) {
        err = laser_controller_board_stusb_nvm_read_bank(
            bank_index,
            bank_data[bank_index]);
        if (err != ESP_OK) {
            (void)laser_controller_board_stusb_nvm_exit_test_mode();
            return err;
        }
    }

    return laser_controller_board_stusb_nvm_exit_test_mode();
}

static esp_err_t laser_controller_board_stusb_nvm_enter_write_mode(
    uint8_t erased_banks_mask)
{
    esp_err_t err;

    if ((erased_banks_mask & LASER_CONTROLLER_STUSB_NVM_ALL_BANKS_MASK) == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    err = laser_controller_board_i2c_write(
        LASER_CONTROLLER_STUSB4500_ADDR,
        (const uint8_t[]){
            LASER_CONTROLLER_STUSB_FTP_KEY_REG,
            LASER_CONTROLLER_STUSB_FTP_PASSWORD,
        },
        2U);
    if (err != ESP_OK) {
        return err;
    }

    err = laser_controller_board_i2c_write(
        LASER_CONTROLLER_STUSB4500_ADDR,
        (const uint8_t[]){
            LASER_CONTROLLER_STUSB_FTP_RW_BUFFER_REG,
            0x00U,
        },
        2U);
    if (err != ESP_OK) {
        return err;
    }

    err = laser_controller_board_stusb_nvm_enter_read_mode();
    if (err != ESP_OK) {
        return err;
    }

    err = laser_controller_board_i2c_write(
        LASER_CONTROLLER_STUSB4500_ADDR,
        (const uint8_t[]){
            LASER_CONTROLLER_STUSB_FTP_CTRL_1_REG,
            (uint8_t)(((erased_banks_mask << 3U) &
                       LASER_CONTROLLER_STUSB_FTP_SER_MASK) |
                      LASER_CONTROLLER_STUSB_FTP_OPCODE_WRITE_SER),
        },
        2U);
    if (err != ESP_OK) {
        return err;
    }

    err = laser_controller_board_i2c_write(
        LASER_CONTROLLER_STUSB4500_ADDR,
        (const uint8_t[]){
            LASER_CONTROLLER_STUSB_FTP_CTRL_0_REG,
            (uint8_t)(LASER_CONTROLLER_STUSB_FTP_RST_N_BIT |
                       LASER_CONTROLLER_STUSB_FTP_REQ_BIT),
        },
        2U);
    if (err != ESP_OK) {
        return err;
    }

    err = laser_controller_board_stusb_nvm_wait_ready(
        LASER_CONTROLLER_STUSB_NVM_COMMAND_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    err = laser_controller_board_i2c_write(
        LASER_CONTROLLER_STUSB4500_ADDR,
        (const uint8_t[]){
            LASER_CONTROLLER_STUSB_FTP_CTRL_1_REG,
            LASER_CONTROLLER_STUSB_FTP_OPCODE_SOFT_PROGRAM,
        },
        2U);
    if (err != ESP_OK) {
        return err;
    }

    err = laser_controller_board_i2c_write(
        LASER_CONTROLLER_STUSB4500_ADDR,
        (const uint8_t[]){
            LASER_CONTROLLER_STUSB_FTP_CTRL_0_REG,
            (uint8_t)(LASER_CONTROLLER_STUSB_FTP_RST_N_BIT |
                       LASER_CONTROLLER_STUSB_FTP_REQ_BIT),
        },
        2U);
    if (err != ESP_OK) {
        return err;
    }

    err = laser_controller_board_stusb_nvm_wait_ready(
        LASER_CONTROLLER_STUSB_NVM_COMMAND_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    err = laser_controller_board_i2c_write(
        LASER_CONTROLLER_STUSB4500_ADDR,
        (const uint8_t[]){
            LASER_CONTROLLER_STUSB_FTP_CTRL_1_REG,
            LASER_CONTROLLER_STUSB_FTP_OPCODE_ERASE,
        },
        2U);
    if (err != ESP_OK) {
        return err;
    }

    err = laser_controller_board_i2c_write(
        LASER_CONTROLLER_STUSB4500_ADDR,
        (const uint8_t[]){
            LASER_CONTROLLER_STUSB_FTP_CTRL_0_REG,
            (uint8_t)(LASER_CONTROLLER_STUSB_FTP_RST_N_BIT |
                       LASER_CONTROLLER_STUSB_FTP_REQ_BIT),
        },
        2U);
    if (err != ESP_OK) {
        return err;
    }

    return laser_controller_board_stusb_nvm_wait_ready(
        LASER_CONTROLLER_STUSB_NVM_COMMAND_TIMEOUT_MS);
}

static esp_err_t laser_controller_board_stusb_nvm_write_bank(
    uint8_t bank_index,
    const uint8_t *bank_data)
{
    esp_err_t err;

    if (bank_data == NULL || bank_index >= LASER_CONTROLLER_STUSB_NVM_BANK_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    err = laser_controller_board_i2c_write_stusb_block(
        LASER_CONTROLLER_STUSB_FTP_RW_BUFFER_REG,
        bank_data,
        LASER_CONTROLLER_STUSB_NVM_BANK_SIZE);
    if (err != ESP_OK) {
        return err;
    }

    err = laser_controller_board_i2c_write(
        LASER_CONTROLLER_STUSB4500_ADDR,
        (const uint8_t[]){
            LASER_CONTROLLER_STUSB_FTP_CTRL_1_REG,
            LASER_CONTROLLER_STUSB_FTP_OPCODE_WRITE_PL,
        },
        2U);
    if (err != ESP_OK) {
        return err;
    }

    err = laser_controller_board_i2c_write(
        LASER_CONTROLLER_STUSB4500_ADDR,
        (const uint8_t[]){
            LASER_CONTROLLER_STUSB_FTP_CTRL_0_REG,
            (uint8_t)(LASER_CONTROLLER_STUSB_FTP_RST_N_BIT |
                       LASER_CONTROLLER_STUSB_FTP_REQ_BIT),
        },
        2U);
    if (err != ESP_OK) {
        return err;
    }

    err = laser_controller_board_stusb_nvm_wait_ready(
        LASER_CONTROLLER_STUSB_NVM_COMMAND_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    err = laser_controller_board_i2c_write(
        LASER_CONTROLLER_STUSB4500_ADDR,
        (const uint8_t[]){
            LASER_CONTROLLER_STUSB_FTP_CTRL_1_REG,
            LASER_CONTROLLER_STUSB_FTP_OPCODE_PROGRAM,
        },
        2U);
    if (err != ESP_OK) {
        return err;
    }

    err = laser_controller_board_i2c_write(
        LASER_CONTROLLER_STUSB4500_ADDR,
        (const uint8_t[]){
            LASER_CONTROLLER_STUSB_FTP_CTRL_0_REG,
            (uint8_t)(LASER_CONTROLLER_STUSB_FTP_RST_N_BIT |
                       LASER_CONTROLLER_STUSB_FTP_REQ_BIT |
                       (bank_index & LASER_CONTROLLER_STUSB_FTP_SECTOR_MASK)),
        },
        2U);
    if (err != ESP_OK) {
        return err;
    }

    return laser_controller_board_stusb_nvm_wait_ready(
        LASER_CONTROLLER_STUSB_NVM_COMMAND_TIMEOUT_MS);
}

static bool laser_controller_board_pd_current_to_nvm_code(
    float current_a,
    uint8_t *lut_code,
    bool *uses_flex)
{
    static const struct {
        float current_a;
        uint8_t code;
    } kCurrentLut[] = {
        { 0.50f, 0x01U },
        { 0.75f, 0x02U },
        { 1.00f, 0x03U },
        { 1.25f, 0x04U },
        { 1.50f, 0x05U },
        { 1.75f, 0x06U },
        { 2.00f, 0x07U },
        { 2.25f, 0x08U },
        { 2.50f, 0x09U },
        { 2.75f, 0x0AU },
        { 3.00f, 0x0BU },
        { 3.50f, 0x0CU },
        { 4.00f, 0x0DU },
        { 4.50f, 0x0EU },
        { 5.00f, 0x0FU },
    };

    if (lut_code == NULL || uses_flex == NULL) {
        return false;
    }

    for (size_t index = 0U; index < sizeof(kCurrentLut) / sizeof(kCurrentLut[0]); ++index) {
        if (fabsf(current_a - kCurrentLut[index].current_a) <= 0.01f) {
            *lut_code = kCurrentLut[index].code;
            *uses_flex = false;
            return true;
        }
    }

    if (current_a < 0.01f || current_a > 5.0f) {
        return false;
    }

    *lut_code = 0x00U;
    *uses_flex = true;
    return true;
}

static bool laser_controller_board_voltage_to_nvm_steps(
    float voltage_v,
    uint16_t *steps)
{
    const float clamped_v =
        laser_controller_board_clamp_float(voltage_v, 5.0f, 20.0f);
    const uint16_t encoded_steps =
        (uint16_t)lroundf(clamped_v / 0.05f);

    if (steps == NULL) {
        return false;
    }

    if (fabsf(clamped_v - ((float)encoded_steps * 0.05f)) > 0.01f) {
        return false;
    }

    *steps = encoded_steps;
    return true;
}

static bool laser_controller_board_current_to_nvm_steps(
    float current_a,
    uint16_t *steps)
{
    const float clamped_a =
        laser_controller_board_clamp_float(current_a, 0.01f, 5.0f);
    const uint16_t encoded_steps =
        (uint16_t)lroundf(clamped_a / 0.01f);

    if (steps == NULL) {
        return false;
    }

    if (fabsf(clamped_a - ((float)encoded_steps * 0.01f)) > 0.005f) {
        return false;
    }

    *steps = encoded_steps;
    return true;
}

static bool laser_controller_board_prepare_pd_nvm_map(
    const laser_controller_service_pd_profile_t *profiles,
    size_t profile_count,
    const uint8_t current_map[LASER_CONTROLLER_STUSB_NVM_BANK_COUNT]
                             [LASER_CONTROLLER_STUSB_NVM_BANK_SIZE],
    uint8_t target_map[LASER_CONTROLLER_STUSB_NVM_BANK_COUNT]
                      [LASER_CONTROLLER_STUSB_NVM_BANK_SIZE])
{
    laser_controller_service_pd_profile_t
        normalized_profiles[LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT];
    uint8_t profile_count_encoded = 1U;
    uint8_t lut_codes[LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT] = { 0 };
    bool uses_flex[LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT] = { 0 };
    bool flex_current_set = false;
    uint16_t flex_current_steps = 0U;

    if (profiles == NULL ||
        current_map == NULL ||
        target_map == NULL ||
        profile_count != LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT) {
        return false;
    }

    memcpy(
        target_map,
        current_map,
        LASER_CONTROLLER_STUSB_NVM_BANK_COUNT *
            LASER_CONTROLLER_STUSB_NVM_BANK_SIZE);

    laser_controller_board_normalize_pd_profiles(
        profiles,
        normalized_profiles,
        &profile_count_encoded);

    for (size_t index = 0U;
         index < LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT;
         ++index) {
        if (index > 0U && !normalized_profiles[index].enabled) {
            lut_codes[index] =
                index == 1U ?
                    (uint8_t)(target_map[3][4] & 0x0FU) :
                    (uint8_t)((target_map[3][5] >> 4U) & 0x0FU);
            uses_flex[index] = false;
            continue;
        }

        if (!laser_controller_board_pd_current_to_nvm_code(
                normalized_profiles[index].current_a,
                &lut_codes[index],
                &uses_flex[index])) {
            return false;
        }

        if (uses_flex[index]) {
            uint16_t current_steps = 0U;

            if (!laser_controller_board_current_to_nvm_steps(
                    normalized_profiles[index].current_a,
                    &current_steps)) {
                return false;
            }

            if (!flex_current_set) {
                flex_current_steps = current_steps;
                flex_current_set = true;
            } else if (flex_current_steps != current_steps) {
                return false;
            }
        }
    }

    target_map[3][2] =
        (uint8_t)((target_map[3][2] & 0x0FU) | (lut_codes[0] << 4U));
    target_map[3][2] =
        (uint8_t)((target_map[3][2] & (uint8_t)~0x06U) |
                  (profile_count_encoded <= 1U ? 0x00U :
                   profile_count_encoded == 2U ? 0x04U : 0x06U));
    target_map[3][4] =
        (uint8_t)((target_map[3][4] & 0xF0U) | lut_codes[1]);
    target_map[3][5] =
        (uint8_t)((target_map[3][5] & 0x0FU) | (lut_codes[2] << 4U));

    if (profile_count_encoded >= 2U) {
        uint16_t voltage_steps = 0U;

        if (!laser_controller_board_voltage_to_nvm_steps(
                normalized_profiles[1].voltage_v,
                &voltage_steps)) {
            return false;
        }

        target_map[4][0] =
            (uint8_t)((target_map[4][0] & 0x3FU) |
                      ((uint8_t)(voltage_steps & 0x03U) << 6U));
        target_map[4][1] = (uint8_t)((voltage_steps >> 2U) & 0xFFU);
    }

    if (profile_count_encoded >= 3U) {
        uint16_t voltage_steps = 0U;

        if (!laser_controller_board_voltage_to_nvm_steps(
                normalized_profiles[2].voltage_v,
                &voltage_steps)) {
            return false;
        }

        target_map[4][2] = (uint8_t)(voltage_steps & 0xFFU);
        target_map[4][3] =
            (uint8_t)((target_map[4][3] & 0xFCU) |
                      ((voltage_steps >> 8U) & 0x03U));
    }

    if (flex_current_set) {
        target_map[4][3] =
            (uint8_t)((target_map[4][3] & 0x03U) |
                      ((uint8_t)(flex_current_steps & 0x3FU) << 2U));
        target_map[4][4] =
            (uint8_t)((target_map[4][4] & 0xF0U) |
                      ((flex_current_steps >> 6U) & 0x0FU));
    }

    return true;
}

static esp_err_t laser_controller_board_dac_write_command(uint8_t command, uint16_t value)
{
    uint8_t payload[3];

    payload[0] = command;
    payload[1] = (uint8_t)((value >> 8U) & 0xFFU);
    payload[2] = (uint8_t)(value & 0xFFU);

    return laser_controller_board_i2c_tx(
        LASER_CONTROLLER_DAC80502_ADDR,
        payload,
        sizeof(payload));
}

static esp_err_t laser_controller_board_dac_read_command(uint8_t command, uint16_t *value)
{
    uint8_t rx[2] = { 0 };
    esp_err_t err;

    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    err = laser_controller_board_i2c_txrx(
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

static void laser_controller_board_clear_dac_readback(void)
{
    memset(&s_dac_readback, 0, sizeof(s_dac_readback));
    s_dac_readback.last_error = ESP_OK;
}

static void laser_controller_board_clear_pd_readback(void)
{
    memset(&s_pd_readback, 0, sizeof(s_pd_readback));
}

static void laser_controller_board_clear_imu_readback(void)
{
    memset(&s_imu_readback, 0, sizeof(s_imu_readback));
    s_imu_readback.last_error = ESP_OK;
}

static void laser_controller_board_clear_haptic_readback(void)
{
    memset(&s_haptic_readback, 0, sizeof(s_haptic_readback));
    s_haptic_readback.last_error = ESP_OK;
}

static void laser_controller_board_refresh_haptic_gpio_levels(void)
{
    s_haptic_readback.enable_pin_high =
        gpio_get_level(LASER_CONTROLLER_GPIO_ERM_EN) != 0;
    s_haptic_readback.trigger_pin_high =
        gpio_get_level(LASER_CONTROLLER_GPIO_ERM_TRIG_GN_LD_EN) != 0;
}

static void laser_controller_board_clear_tof_readback(void)
{
    memset(&s_tof_readback, 0, sizeof(s_tof_readback));
    s_tof_readback.last_error = ESP_OK;
}

static void laser_controller_board_clear_tof_runtime(void)
{
    memset(&s_tof_runtime, 0, sizeof(s_tof_runtime));
    s_tof_runtime.last_error = ESP_OK;
    s_tof_runtime.range_status = 0xFFU;
}

static uint8_t laser_controller_board_tof_map_range_status(uint8_t raw_status)
{
    switch (raw_status & 0x1FU) {
        case 9:
            return 0;
        case 6:
            return 1;
        case 4:
            return 2;
        case 8:
            return 3;
        case 5:
            return 4;
        case 3:
            return 5;
        case 19:
            return 6;
        case 7:
            return 7;
        case 12:
            return 9;
        case 18:
            return 10;
        case 22:
            return 11;
        case 23:
            return 12;
        case 13:
            return 13;
        default:
            return 0xFFU;
    }
}

static uint16_t laser_controller_board_tof_median3_u16(
    uint16_t a,
    uint16_t b,
    uint16_t c)
{
    if (a > b) {
        const uint16_t temp = a;
        a = b;
        b = temp;
    }
    if (b > c) {
        const uint16_t temp = b;
        b = c;
        c = temp;
    }
    if (a > b) {
        const uint16_t temp = a;
        a = b;
        b = temp;
    }

    return b;
}

static uint16_t laser_controller_board_tof_promote_distance_sample(uint16_t raw_distance_mm)
{
    const uint8_t slot = s_tof_runtime.accepted_sample_index;

    s_tof_runtime.accepted_samples_mm[slot] = raw_distance_mm;
    s_tof_runtime.accepted_sample_index = (uint8_t)((slot + 1U) % 3U);
    if (s_tof_runtime.accepted_sample_count < 3U) {
        s_tof_runtime.accepted_sample_count++;
    }

    if (s_tof_runtime.accepted_sample_count == 1U) {
        return s_tof_runtime.accepted_samples_mm[0];
    }

    if (s_tof_runtime.accepted_sample_count == 2U) {
        const uint32_t sum =
            (uint32_t)s_tof_runtime.accepted_samples_mm[0] +
            (uint32_t)s_tof_runtime.accepted_samples_mm[1];
        return (uint16_t)((sum + 1U) / 2U);
    }

    return laser_controller_board_tof_median3_u16(
        s_tof_runtime.accepted_samples_mm[0],
        s_tof_runtime.accepted_samples_mm[1],
        s_tof_runtime.accepted_samples_mm[2]);
}

static void laser_controller_board_publish_tof_inputs(
    laser_controller_time_ms_t now_ms,
    const laser_controller_config_t *config,
    laser_controller_board_inputs_t *inputs)
{
    const uint32_t stale_ms = config->timeouts.tof_stale_ms;
    const bool fresh =
        s_tof_runtime.last_sample_ms != 0U &&
        (now_ms - s_tof_runtime.last_sample_ms) <= stale_ms;
    const bool valid =
        fresh &&
        s_tof_runtime.configured &&
        s_tof_runtime.range_status == 0U &&
        s_tof_runtime.distance_mm > 0U;

    if (inputs == NULL || config == NULL) {
        return;
    }

    inputs->tof_data_fresh = fresh;
    inputs->tof_data_valid = valid;
    inputs->tof_distance_m = (float)s_tof_runtime.distance_mm / 1000.0f;
}

static esp_err_t laser_controller_board_tof_get_interrupt_polarity(uint8_t *polarity)
{
    uint8_t value = 0U;
    esp_err_t err;

    if (polarity == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    err = laser_controller_board_i2c_read_reg16_u8(
        LASER_CONTROLLER_VL53L1X_ADDR,
        LASER_CONTROLLER_VL53L1X_GPIO_HV_MUX_CTRL_REG,
        &value);
    if (err != ESP_OK) {
        return err;
    }

    *polarity = (uint8_t)(!(value >> 4U));
    return ESP_OK;
}

static esp_err_t laser_controller_board_tof_check_data_ready(bool *ready)
{
    uint8_t status_reg = 0U;
    uint8_t polarity = 0U;
    esp_err_t err;

    if (ready == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    err = laser_controller_board_tof_get_interrupt_polarity(&polarity);
    if (err != ESP_OK) {
        return err;
    }

    err = laser_controller_board_i2c_read_reg16_u8(
        LASER_CONTROLLER_VL53L1X_ADDR,
        LASER_CONTROLLER_VL53L1X_GPIO_STATUS_REG,
        &status_reg);
    if (err != ESP_OK) {
        return err;
    }

    s_tof_runtime.interrupt_polarity = polarity;
    *ready = (status_reg & 0x01U) == polarity;
    return ESP_OK;
}

static esp_err_t laser_controller_board_tof_clear_interrupt(void)
{
    return laser_controller_board_i2c_write_reg16_u8(
        LASER_CONTROLLER_VL53L1X_ADDR,
        LASER_CONTROLLER_VL53L1X_INTERRUPT_CLEAR_REG,
        0x01U);
}

static esp_err_t laser_controller_board_tof_start_ranging(void)
{
    return laser_controller_board_i2c_write_reg16_u8(
        LASER_CONTROLLER_VL53L1X_ADDR,
        LASER_CONTROLLER_VL53L1X_MODE_START_REG,
        0x40U);
}

static esp_err_t laser_controller_board_tof_stop_ranging(void)
{
    return laser_controller_board_i2c_write_reg16_u8(
        LASER_CONTROLLER_VL53L1X_ADDR,
        LASER_CONTROLLER_VL53L1X_MODE_START_REG,
        0x00U);
}

static esp_err_t laser_controller_board_tof_set_timing_budget_ms(uint16_t timing_budget_ms)
{
    switch (timing_budget_ms) {
        case 20:
            if (laser_controller_board_i2c_write_reg16_u16(
                    LASER_CONTROLLER_VL53L1X_ADDR,
                    LASER_CONTROLLER_VL53L1X_RANGE_TIMEOUT_A_REG,
                    0x001EU) != ESP_OK ||
                laser_controller_board_i2c_write_reg16_u16(
                    LASER_CONTROLLER_VL53L1X_ADDR,
                    LASER_CONTROLLER_VL53L1X_RANGE_TIMEOUT_B_REG,
                    0x0022U) != ESP_OK) {
                return ESP_FAIL;
            }
            return ESP_OK;
        case 33:
            if (laser_controller_board_i2c_write_reg16_u16(
                    LASER_CONTROLLER_VL53L1X_ADDR,
                    LASER_CONTROLLER_VL53L1X_RANGE_TIMEOUT_A_REG,
                    0x0060U) != ESP_OK ||
                laser_controller_board_i2c_write_reg16_u16(
                    LASER_CONTROLLER_VL53L1X_ADDR,
                    LASER_CONTROLLER_VL53L1X_RANGE_TIMEOUT_B_REG,
                    0x006EU) != ESP_OK) {
                return ESP_FAIL;
            }
            return ESP_OK;
        case 50:
            if (laser_controller_board_i2c_write_reg16_u16(
                    LASER_CONTROLLER_VL53L1X_ADDR,
                    LASER_CONTROLLER_VL53L1X_RANGE_TIMEOUT_A_REG,
                    0x00ADU) != ESP_OK ||
                laser_controller_board_i2c_write_reg16_u16(
                    LASER_CONTROLLER_VL53L1X_ADDR,
                    LASER_CONTROLLER_VL53L1X_RANGE_TIMEOUT_B_REG,
                    0x00C6U) != ESP_OK) {
                return ESP_FAIL;
            }
            return ESP_OK;
        case 100:
            if (laser_controller_board_i2c_write_reg16_u16(
                    LASER_CONTROLLER_VL53L1X_ADDR,
                    LASER_CONTROLLER_VL53L1X_RANGE_TIMEOUT_A_REG,
                    0x01CCU) != ESP_OK ||
                laser_controller_board_i2c_write_reg16_u16(
                    LASER_CONTROLLER_VL53L1X_ADDR,
                    LASER_CONTROLLER_VL53L1X_RANGE_TIMEOUT_B_REG,
                    0x01EAU) != ESP_OK) {
                return ESP_FAIL;
            }
            return ESP_OK;
        case 200:
            if (laser_controller_board_i2c_write_reg16_u16(
                    LASER_CONTROLLER_VL53L1X_ADDR,
                    LASER_CONTROLLER_VL53L1X_RANGE_TIMEOUT_A_REG,
                    0x02D9U) != ESP_OK ||
                laser_controller_board_i2c_write_reg16_u16(
                    LASER_CONTROLLER_VL53L1X_ADDR,
                    LASER_CONTROLLER_VL53L1X_RANGE_TIMEOUT_B_REG,
                    0x02F8U) != ESP_OK) {
                return ESP_FAIL;
            }
            return ESP_OK;
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

/*
 * VL53L1X distance-mode helpers — VCSEL periods + valid-phase + WOI per
 * ST ULD driver reference (VL53L1X_API/STSW-IMG009). Distance mode changes
 * the signal-rate-over-ambient-rate filter and the pulse timing; it must
 * be written BEFORE setting timing budget because timing-budget registers
 * depend on the pulse period.
 *   short  : ≤ 1.3 m, best ambient immunity
 *   medium : ≤ 3.0 m
 *   long   : ≤ 4.0 m (factory default)
 * Shared register list comes from VL53L1X datasheet §5.11 and AN5191.
 */
static esp_err_t laser_controller_board_tof_set_distance_mode_short(void)
{
    if (laser_controller_board_i2c_write_reg16_u8(
            LASER_CONTROLLER_VL53L1X_ADDR,
            LASER_CONTROLLER_VL53L1X_PHASECAL_TIMEOUT_REG,
            0x14U) != ESP_OK ||
        laser_controller_board_i2c_write_reg16_u8(
            LASER_CONTROLLER_VL53L1X_ADDR,
            LASER_CONTROLLER_VL53L1X_VCSEL_PERIOD_A_REG,
            0x07U) != ESP_OK ||
        laser_controller_board_i2c_write_reg16_u8(
            LASER_CONTROLLER_VL53L1X_ADDR,
            LASER_CONTROLLER_VL53L1X_VCSEL_PERIOD_B_REG,
            0x05U) != ESP_OK ||
        laser_controller_board_i2c_write_reg16_u8(
            LASER_CONTROLLER_VL53L1X_ADDR,
            LASER_CONTROLLER_VL53L1X_VALID_PHASE_HIGH_REG,
            0x38U) != ESP_OK ||
        laser_controller_board_i2c_write_reg16_u16(
            LASER_CONTROLLER_VL53L1X_ADDR,
            LASER_CONTROLLER_VL53L1X_WOI_SD0_REG,
            0x0705U) != ESP_OK ||
        laser_controller_board_i2c_write_reg16_u16(
            LASER_CONTROLLER_VL53L1X_ADDR,
            LASER_CONTROLLER_VL53L1X_INITIAL_PHASE_SD0_REG,
            0x0606U) != ESP_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t laser_controller_board_tof_set_distance_mode_medium(void)
{
    if (laser_controller_board_i2c_write_reg16_u8(
            LASER_CONTROLLER_VL53L1X_ADDR,
            LASER_CONTROLLER_VL53L1X_PHASECAL_TIMEOUT_REG,
            0x0AU) != ESP_OK ||
        laser_controller_board_i2c_write_reg16_u8(
            LASER_CONTROLLER_VL53L1X_ADDR,
            LASER_CONTROLLER_VL53L1X_VCSEL_PERIOD_A_REG,
            0x0BU) != ESP_OK ||
        laser_controller_board_i2c_write_reg16_u8(
            LASER_CONTROLLER_VL53L1X_ADDR,
            LASER_CONTROLLER_VL53L1X_VCSEL_PERIOD_B_REG,
            0x09U) != ESP_OK ||
        laser_controller_board_i2c_write_reg16_u8(
            LASER_CONTROLLER_VL53L1X_ADDR,
            LASER_CONTROLLER_VL53L1X_VALID_PHASE_HIGH_REG,
            0x78U) != ESP_OK ||
        laser_controller_board_i2c_write_reg16_u16(
            LASER_CONTROLLER_VL53L1X_ADDR,
            LASER_CONTROLLER_VL53L1X_WOI_SD0_REG,
            0x0B09U) != ESP_OK ||
        laser_controller_board_i2c_write_reg16_u16(
            LASER_CONTROLLER_VL53L1X_ADDR,
            LASER_CONTROLLER_VL53L1X_INITIAL_PHASE_SD0_REG,
            0x0A0AU) != ESP_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t laser_controller_board_tof_set_distance_mode_long(void);

static esp_err_t laser_controller_board_tof_set_distance_mode(
    laser_controller_tof_distance_mode_t mode)
{
    switch (mode) {
        case LASER_CONTROLLER_TOF_DISTANCE_MODE_SHORT:
            return laser_controller_board_tof_set_distance_mode_short();
        case LASER_CONTROLLER_TOF_DISTANCE_MODE_MEDIUM:
            return laser_controller_board_tof_set_distance_mode_medium();
        case LASER_CONTROLLER_TOF_DISTANCE_MODE_LONG:
        default:
            return laser_controller_board_tof_set_distance_mode_long();
    }
}

/*
 * VL53L1X ROI (cone size). w/h in SPADs, clamped 4..16. Center spad is a
 * packed byte index on the 16x16 SPAD grid per VL53L1X datasheet §6.8
 * (default 199 = centre). Setting a smaller ROI narrows the effective
 * field-of-view — 4x4 ≈ 7.5° cone vs 16x16 ≈ 27° at full grid.
 *
 * ROI_CONFIG__USER_ROI_REQUESTED_GLOBAL_XY_SIZE = ((h-1)<<4) | (w-1)
 * ROI_CONFIG__USER_ROI_CENTRE_SPAD = center_spad (0..255)
 */
static esp_err_t laser_controller_board_tof_set_roi(
    uint8_t width_spads,
    uint8_t height_spads,
    uint8_t center_spad)
{
    if (width_spads < 4U) width_spads = 4U;
    if (width_spads > 16U) width_spads = 16U;
    if (height_spads < 4U) height_spads = 4U;
    if (height_spads > 16U) height_spads = 16U;

    const uint8_t xy = (uint8_t)(((height_spads - 1U) << 4) | (width_spads - 1U));

    if (laser_controller_board_i2c_write_reg16_u8(
            LASER_CONTROLLER_VL53L1X_ADDR,
            0x007FU /* ROI_CONFIG__USER_ROI_CENTRE_SPAD */,
            center_spad) != ESP_OK) {
        return ESP_FAIL;
    }
    if (laser_controller_board_i2c_write_reg16_u8(
            LASER_CONTROLLER_VL53L1X_ADDR,
            0x0080U /* ROI_CONFIG__USER_ROI_REQUESTED_GLOBAL_XY_SIZE */,
            xy) != ESP_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

/*
 * Offset correction — signed mm added to the raw range reading. VL53L1X
 * register 0x001E (ALGO__PART_TO_PART_RANGE_OFFSET_MM) is a signed 13-bit
 * value expressed as `(offset_mm * 4)` (i.e. stored in 1/4-mm units per
 * the ULD driver). Clamp input to ±511 mm so the ×4 shift fits.
 */
static esp_err_t laser_controller_board_tof_set_offset_mm(int32_t offset_mm)
{
    if (offset_mm > 511) offset_mm = 511;
    if (offset_mm < -512) offset_mm = -512;
    const int16_t raw = (int16_t)(offset_mm * 4);
    return laser_controller_board_i2c_write_reg16_u16(
        LASER_CONTROLLER_VL53L1X_ADDR,
        0x001EU /* ALGO__PART_TO_PART_RANGE_OFFSET_MM */,
        (uint16_t)raw);
}

/*
 * Crosstalk compensation — writes kcps to register 0x0016. When xtalk is
 * disabled, writes 0 to effectively remove compensation. The STM ULD
 * driver applies a `<< 9` scaling when writing 0x0016 but our variant
 * stores raw kcps because the host value is already a measured kcps
 * scalar; for precise cover-glass calibration the `<< 9` ULD form can
 * be restored later with a measured target.
 */
static esp_err_t laser_controller_board_tof_set_xtalk(
    uint32_t xtalk_cps,
    bool enabled)
{
    const uint16_t value = enabled ? (uint16_t)(xtalk_cps & 0xFFFFU) : 0U;
    return laser_controller_board_i2c_write_reg16_u16(
        LASER_CONTROLLER_VL53L1X_ADDR,
        0x0016U /* ALGO__CROSSTALK_COMPENSATION_PLANE_OFFSET_KCPS */,
        value);
}

static esp_err_t laser_controller_board_tof_set_distance_mode_long(void)
{
    if (laser_controller_board_i2c_write_reg16_u8(
            LASER_CONTROLLER_VL53L1X_ADDR,
            LASER_CONTROLLER_VL53L1X_PHASECAL_TIMEOUT_REG,
            0x0AU) != ESP_OK ||
        laser_controller_board_i2c_write_reg16_u8(
            LASER_CONTROLLER_VL53L1X_ADDR,
            LASER_CONTROLLER_VL53L1X_VCSEL_PERIOD_A_REG,
            0x0FU) != ESP_OK ||
        laser_controller_board_i2c_write_reg16_u8(
            LASER_CONTROLLER_VL53L1X_ADDR,
            LASER_CONTROLLER_VL53L1X_VCSEL_PERIOD_B_REG,
            0x0DU) != ESP_OK ||
        laser_controller_board_i2c_write_reg16_u8(
            LASER_CONTROLLER_VL53L1X_ADDR,
            LASER_CONTROLLER_VL53L1X_VALID_PHASE_HIGH_REG,
            0xB8U) != ESP_OK ||
        laser_controller_board_i2c_write_reg16_u16(
            LASER_CONTROLLER_VL53L1X_ADDR,
            LASER_CONTROLLER_VL53L1X_WOI_SD0_REG,
            0x0F0DU) != ESP_OK ||
        laser_controller_board_i2c_write_reg16_u16(
            LASER_CONTROLLER_VL53L1X_ADDR,
            LASER_CONTROLLER_VL53L1X_INITIAL_PHASE_SD0_REG,
            0x0E0EU) != ESP_OK) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t laser_controller_board_tof_set_intermeasurement_ms(uint16_t intermeasurement_ms)
{
    uint16_t osc = 0U;
    uint32_t encoded = 0U;
    esp_err_t err;

    err = laser_controller_board_i2c_read_reg16_u16(
        LASER_CONTROLLER_VL53L1X_ADDR,
        LASER_CONTROLLER_VL53L1X_OSC_CALIBRATE_REG,
        &osc);
    if (err != ESP_OK) {
        return err;
    }

    osc &= 0x03FFU;
    encoded = (uint32_t)((float)osc * (float)intermeasurement_ms * 1.075f);
    return laser_controller_board_i2c_write_reg16_u32(
        LASER_CONTROLLER_VL53L1X_ADDR,
        LASER_CONTROLLER_VL53L1X_INTERMEASUREMENT_REG,
        encoded);
}

static esp_err_t laser_controller_board_tof_configure_sensor(void)
{
    bool ready = false;
    esp_err_t err;

    err = laser_controller_board_i2c_write_reg16_block(
        LASER_CONTROLLER_VL53L1X_ADDR,
        0x002DU,
        kVl53l1xDefaultConfiguration,
        sizeof(kVl53l1xDefaultConfiguration));
    if (err != ESP_OK) {
        return err;
    }

    err = laser_controller_board_tof_start_ranging();
    if (err != ESP_OK) {
        return err;
    }

    for (uint32_t waited_ms = 0U;
         waited_ms < LASER_CONTROLLER_TOF_INIT_READY_TIMEOUT_MS;
         waited_ms += LASER_CONTROLLER_TOF_BOOT_RETRY_MS) {
        err = laser_controller_board_tof_check_data_ready(&ready);
        if (err != ESP_OK) {
            return err;
        }
        if (ready) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(LASER_CONTROLLER_TOF_BOOT_RETRY_MS));
    }

    err = laser_controller_board_tof_clear_interrupt();
    if (err != ESP_OK) {
        return err;
    }

    err = laser_controller_board_tof_stop_ranging();
    if (err != ESP_OK) {
        return err;
    }

    if (laser_controller_board_i2c_write_reg16_u8(
            LASER_CONTROLLER_VL53L1X_ADDR,
            LASER_CONTROLLER_VL53L1X_VHV_TIMEOUT_REG,
            0x09U) != ESP_OK ||
        laser_controller_board_i2c_write_reg16_u8(
            LASER_CONTROLLER_VL53L1X_ADDR,
            LASER_CONTROLLER_VL53L1X_START_VHV_FROM_PREV_REG,
            0x00U) != ESP_OK) {
        return ESP_FAIL;
    }

    /*
     * Apply operator-configured calibration from service NVS. Distance
     * mode must be written BEFORE timing budget (timing budget registers
     * depend on VCSEL period). ROI + offset + xtalk can be written in any
     * order but are done after timing budget for consistency with the ST
     * ULD driver sequence. Falls back to safe defaults (long, 50 ms,
     * 16x16, zero offset, xtalk off) if the service module is missing.
     */
    laser_controller_tof_calibration_t cal;
    laser_controller_service_get_tof_calibration(&cal);
    if (cal.timing_budget_ms == 0U ||
        cal.roi_width_spads == 0U ||
        cal.roi_height_spads == 0U) {
        cal.distance_mode = LASER_CONTROLLER_TOF_DISTANCE_MODE_LONG;
        cal.timing_budget_ms = 50U;
        cal.roi_width_spads = 16U;
        cal.roi_height_spads = 16U;
        cal.roi_center_spad = 199U;
        cal.offset_mm = 0;
        cal.xtalk_cps = 0U;
        cal.xtalk_enabled = false;
    }

    err = laser_controller_board_tof_set_distance_mode(cal.distance_mode);
    if (err != ESP_OK) {
        return err;
    }
    err = laser_controller_board_tof_set_timing_budget_ms(
        (uint16_t)cal.timing_budget_ms);
    if (err != ESP_OK) {
        return err;
    }
    err = laser_controller_board_tof_set_roi(
        cal.roi_width_spads,
        cal.roi_height_spads,
        cal.roi_center_spad);
    if (err != ESP_OK) {
        return err;
    }
    err = laser_controller_board_tof_set_offset_mm(cal.offset_mm);
    if (err != ESP_OK) {
        return err;
    }
    err = laser_controller_board_tof_set_xtalk(
        cal.xtalk_cps,
        cal.xtalk_enabled);
    if (err != ESP_OK) {
        return err;
    }

    err = laser_controller_board_tof_set_intermeasurement_ms(
        LASER_CONTROLLER_TOF_INTERMEASUREMENT_MS);
    if (err != ESP_OK) {
        return err;
    }

    return laser_controller_board_tof_start_ranging();
}

/*
 * Re-apply ToF calibration without tearing down ranging. Writes
 * distance-mode / timing-budget / ROI / offset / xtalk. Intended for
 * runtime calibration updates where a full init cycle would be overkill.
 * Callers should stop ranging first if they intend to change
 * distance-mode or timing-budget (register-level changes the VL53L1X
 * handles in-flight are narrow); the ST ULD notes a stop → change →
 * start sequence as safest.
 */
esp_err_t laser_controller_board_tof_apply_calibration(
    const laser_controller_tof_calibration_t *cal)
{
    if (cal == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err;

    (void)laser_controller_board_tof_stop_ranging();

    err = laser_controller_board_tof_set_distance_mode(cal->distance_mode);
    if (err != ESP_OK) {
        (void)laser_controller_board_tof_start_ranging();
        return err;
    }
    err = laser_controller_board_tof_set_timing_budget_ms(
        (uint16_t)cal->timing_budget_ms);
    if (err != ESP_OK) {
        (void)laser_controller_board_tof_start_ranging();
        return err;
    }
    err = laser_controller_board_tof_set_roi(
        cal->roi_width_spads,
        cal->roi_height_spads,
        cal->roi_center_spad);
    if (err != ESP_OK) {
        (void)laser_controller_board_tof_start_ranging();
        return err;
    }
    err = laser_controller_board_tof_set_offset_mm(cal->offset_mm);
    if (err != ESP_OK) {
        (void)laser_controller_board_tof_start_ranging();
        return err;
    }
    err = laser_controller_board_tof_set_xtalk(
        cal->xtalk_cps,
        cal->xtalk_enabled);
    if (err != ESP_OK) {
        (void)laser_controller_board_tof_start_ranging();
        return err;
    }

    return laser_controller_board_tof_start_ranging();
}

static void laser_controller_board_capture_dac_readback(bool configured)
{
    uint16_t value = 0U;
    esp_err_t err;

    laser_controller_board_clear_dac_readback();
    s_dac_readback.configured = configured;

    err = laser_controller_board_i2c_probe(LASER_CONTROLLER_DAC80502_ADDR);
    if (err != ESP_OK) {
        s_dac_readback.last_error = (int32_t)err;
        return;
    }

    s_dac_readback.reachable = true;

    err = laser_controller_board_dac_read_command(
        LASER_CONTROLLER_DAC_SYNC_REG,
        &value);
    if (err != ESP_OK) {
        s_dac_readback.last_error = (int32_t)err;
        return;
    }
    s_dac_readback.sync_reg = value;

    err = laser_controller_board_dac_read_command(
        LASER_CONTROLLER_DAC_CONFIG_REG,
        &value);
    if (err != ESP_OK) {
        s_dac_readback.last_error = (int32_t)err;
        return;
    }
    s_dac_readback.config_reg = value;

    err = laser_controller_board_dac_read_command(
        LASER_CONTROLLER_DAC_GAIN_REG,
        &value);
    if (err != ESP_OK) {
        s_dac_readback.last_error = (int32_t)err;
        return;
    }
    s_dac_readback.gain_reg = value;

    err = laser_controller_board_dac_read_command(
        LASER_CONTROLLER_DAC_STATUS_REG,
        &value);
    if (err != ESP_OK) {
        s_dac_readback.last_error = (int32_t)err;
        return;
    }
    s_dac_readback.status_reg = value;
    s_dac_readback.ref_alarm =
        (value & LASER_CONTROLLER_DAC_STATUS_REFALARM_BIT) != 0U;

    err = laser_controller_board_dac_read_command(
        LASER_CONTROLLER_DAC_DATA_A_REG,
        &value);
    if (err != ESP_OK) {
        s_dac_readback.last_error = (int32_t)err;
        return;
    }
    s_dac_readback.data_a_reg = value;

    err = laser_controller_board_dac_read_command(
        LASER_CONTROLLER_DAC_DATA_B_REG,
        &value);
    if (err != ESP_OK) {
        s_dac_readback.last_error = (int32_t)err;
        return;
    }
    s_dac_readback.data_b_reg = value;
    s_dac_readback.last_error = ESP_OK;
}

static void laser_controller_board_capture_haptic_readback(void)
{
    uint8_t value = 0U;
    esp_err_t err;

    laser_controller_board_clear_haptic_readback();
    laser_controller_board_refresh_haptic_gpio_levels();

    if (!laser_controller_service_module_expected(LASER_CONTROLLER_MODULE_HAPTIC)) {
        return;
    }

    err = laser_controller_board_i2c_probe(LASER_CONTROLLER_DRV2605_ADDR);
    if (err != ESP_OK) {
        s_haptic_readback.last_error = (int32_t)err;
        return;
    }

    s_haptic_readback.reachable = true;

    err = laser_controller_board_i2c_read_reg_u8(
        LASER_CONTROLLER_DRV2605_ADDR,
        LASER_CONTROLLER_DRV2605_MODE_REG,
        &value);
    if (err != ESP_OK) {
        s_haptic_readback.last_error = (int32_t)err;
        return;
    }
    s_haptic_readback.mode_reg = value;

    err = laser_controller_board_i2c_read_reg_u8(
        LASER_CONTROLLER_DRV2605_ADDR,
        LASER_CONTROLLER_DRV2605_LIBRARY_REG,
        &value);
    if (err != ESP_OK) {
        s_haptic_readback.last_error = (int32_t)err;
        return;
    }
    s_haptic_readback.library_reg = value;

    err = laser_controller_board_i2c_read_reg_u8(
        LASER_CONTROLLER_DRV2605_ADDR,
        LASER_CONTROLLER_DRV2605_GO_REG,
        &value);
    if (err != ESP_OK) {
        s_haptic_readback.last_error = (int32_t)err;
        return;
    }
    s_haptic_readback.go_reg = value;

    err = laser_controller_board_i2c_read_reg_u8(
        LASER_CONTROLLER_DRV2605_ADDR,
        LASER_CONTROLLER_DRV2605_FEEDBACK_REG,
        &value);
    if (err != ESP_OK) {
        s_haptic_readback.last_error = (int32_t)err;
        return;
    }
    s_haptic_readback.feedback_reg = value;
    s_haptic_readback.last_error = ESP_OK;
}

static void laser_controller_board_capture_tof_readback(
    laser_controller_time_ms_t now_ms,
    const laser_controller_config_t *config,
    laser_controller_board_inputs_t *inputs)
{
    const bool tof_expected =
        laser_controller_service_module_write_enabled(
            LASER_CONTROLLER_MODULE_TOF);
    /*
     * BUGFIX: previously passed `service_owns_tof_sideband` alone, which
     * ignored runtime ownership and forced GPIO6 LOW on every TOF poll.
     * Pass `any_owns_...` so the runtime illumination request (Operate LED
     * slider, or the ToF daughterboard illumination during deployment)
     * survives each TOF readback cycle.
     */
    const bool anyone_owns_tof_sideband =
        laser_controller_board_any_owns_tof_sideband();
    bool data_ready = false;
    uint8_t boot_state = 0U;
    uint16_t sensor_id = 0U;
    uint8_t raw_range_status = 0U;
    uint16_t distance_mm = 0U;
    esp_err_t err;

    laser_controller_board_apply_tof_sideband_state(anyone_owns_tof_sideband);

    if (!tof_expected) {
        if (s_tof_runtime.ranging) {
            (void)laser_controller_board_tof_stop_ranging();
        }
        laser_controller_board_clear_tof_runtime();
        laser_controller_board_clear_tof_readback();
        s_tof_last_error = ESP_OK;
        s_tof_last_poll_ms = now_ms;
        laser_controller_board_service_report_probe(
            LASER_CONTROLLER_MODULE_TOF,
            false,
            false);
        return;
    }

    if ((now_ms - s_tof_last_poll_ms) < LASER_CONTROLLER_TOF_POLL_MS &&
        (s_tof_readback.reachable || s_tof_last_error == ESP_OK)) {
        laser_controller_board_publish_tof_inputs(now_ms, config, inputs);
        return;
    }

    s_tof_last_poll_ms = now_ms;
    laser_controller_board_clear_tof_readback();
    /*
     * ToF GPIO1 is no longer wired to an ESP32 GPIO — GPIO7 was reassigned
     * to MCP23017 INTA on 2026-04-15 because the button board and ToF
     * daughterboard share the same J2 connector. The ToF now runs in
     * polling-only mode via the RANGE_STATUS register read further below,
     * so `interrupt_line_high` is simply not observable and we report
     * false. Range freshness flows through `tof_readback.data_ready`
     * (set from the RANGE_STATUS low bit) instead.
     */
    s_tof_readback.interrupt_line_high = false;
    s_tof_readback.led_ctrl_asserted =
        s_tof_illumination.enabled ||
        (gpio_get_level(LASER_CONTROLLER_GPIO_TOF_LED_CTRL) != 0);

    err = laser_controller_board_i2c_probe(LASER_CONTROLLER_VL53L1X_ADDR);
    if (err != ESP_OK) {
        laser_controller_board_clear_tof_runtime();
        s_tof_last_error = err;
        s_tof_readback.last_error = (int32_t)err;
        laser_controller_board_service_report_probe(
            LASER_CONTROLLER_MODULE_TOF,
            false,
            false);
        return;
    }

    s_tof_readback.reachable = true;
    s_tof_runtime.reachable = true;

    err = laser_controller_board_i2c_read_reg16_u8(
        LASER_CONTROLLER_VL53L1X_ADDR,
        LASER_CONTROLLER_VL53L1X_BOOT_STATE_REG,
        &boot_state);
    if (err != ESP_OK) {
        s_tof_runtime.last_error = err;
        s_tof_last_error = err;
        s_tof_readback.last_error = (int32_t)err;
        laser_controller_board_service_report_probe(
            LASER_CONTROLLER_MODULE_TOF,
            true,
            false);
        return;
    }
    s_tof_runtime.boot_state = boot_state;
    s_tof_readback.boot_state = boot_state;

    err = laser_controller_board_i2c_read_reg16_u16(
        LASER_CONTROLLER_VL53L1X_ADDR,
        LASER_CONTROLLER_VL53L1X_SENSOR_ID_REG,
        &sensor_id);
    if (err != ESP_OK) {
        s_tof_runtime.last_error = err;
        s_tof_last_error = err;
        s_tof_readback.last_error = (int32_t)err;
        laser_controller_board_service_report_probe(
            LASER_CONTROLLER_MODULE_TOF,
            true,
            false);
        return;
    }
    s_tof_runtime.sensor_id = sensor_id;
    s_tof_readback.sensor_id = sensor_id;

    if (boot_state == 0U) {
        s_tof_runtime.last_error = ESP_ERR_INVALID_STATE;
        s_tof_last_error = ESP_ERR_INVALID_STATE;
        s_tof_readback.last_error = (int32_t)ESP_ERR_INVALID_STATE;
        laser_controller_board_service_report_probe(
            LASER_CONTROLLER_MODULE_TOF,
            true,
            false);
        return;
    }

    if (sensor_id != LASER_CONTROLLER_VL53L1X_SENSOR_ID) {
        s_tof_runtime.last_error = ESP_ERR_INVALID_RESPONSE;
        s_tof_last_error = ESP_ERR_INVALID_RESPONSE;
        s_tof_readback.last_error = (int32_t)ESP_ERR_INVALID_RESPONSE;
        laser_controller_board_service_report_probe(
            LASER_CONTROLLER_MODULE_TOF,
            true,
            false);
        return;
    }

    if (!s_tof_runtime.configured) {
        err = laser_controller_board_tof_configure_sensor();
        if (err != ESP_OK) {
            s_tof_runtime.last_error = err;
            s_tof_last_error = err;
            s_tof_readback.last_error = (int32_t)err;
            laser_controller_board_service_report_probe(
                LASER_CONTROLLER_MODULE_TOF,
                true,
                false);
            return;
        }

        s_tof_runtime.configured = true;
        s_tof_runtime.ranging = true;
    }

    s_tof_readback.configured = s_tof_runtime.configured;

    err = laser_controller_board_tof_check_data_ready(&data_ready);
    if (err != ESP_OK) {
        s_tof_runtime.last_error = err;
        s_tof_last_error = err;
        s_tof_readback.last_error = (int32_t)err;
        laser_controller_board_service_report_probe(
            LASER_CONTROLLER_MODULE_TOF,
            true,
            false);
        return;
    }

    s_tof_runtime.data_ready = data_ready;
    s_tof_readback.data_ready = data_ready;

    if (data_ready) {
        uint8_t mapped_range_status = 0xFFU;

        err = laser_controller_board_i2c_read_reg16_u8(
            LASER_CONTROLLER_VL53L1X_ADDR,
            LASER_CONTROLLER_VL53L1X_RANGE_STATUS_REG,
            &raw_range_status);
        if (err == ESP_OK) {
            err = laser_controller_board_i2c_read_reg16_u16(
                LASER_CONTROLLER_VL53L1X_ADDR,
                LASER_CONTROLLER_VL53L1X_DISTANCE_MM_REG,
                &distance_mm);
        }
        if (err == ESP_OK) {
            err = laser_controller_board_tof_clear_interrupt();
        }
        if (err != ESP_OK) {
            s_tof_runtime.last_error = err;
            s_tof_last_error = err;
            s_tof_readback.last_error = (int32_t)err;
            laser_controller_board_service_report_probe(
                LASER_CONTROLLER_MODULE_TOF,
                true,
                false);
            return;
        }

        mapped_range_status =
            laser_controller_board_tof_map_range_status(raw_range_status);
        s_tof_runtime.range_status = mapped_range_status;
        s_tof_runtime.raw_distance_mm = distance_mm;
        if (mapped_range_status == 0U && distance_mm > 0U) {
            s_tof_runtime.distance_mm =
                laser_controller_board_tof_promote_distance_sample(distance_mm);
            s_tof_runtime.last_sample_ms = now_ms;
        }
    }

    s_tof_readback.range_status = s_tof_runtime.range_status;
    s_tof_readback.distance_mm = s_tof_runtime.raw_distance_mm;
    s_tof_runtime.last_error = ESP_OK;
    s_tof_last_error = ESP_OK;
    s_tof_readback.last_error = ESP_OK;

    laser_controller_board_publish_tof_inputs(now_ms, config, inputs);

    laser_controller_board_service_report_probe(
        LASER_CONTROLLER_MODULE_TOF,
        true,
        s_tof_runtime.configured);
}

static bool laser_controller_board_dac_config_is_safe(
    laser_controller_service_dac_reference_t reference,
    bool gain_2x,
    bool ref_div)
{
    /*
     * On this board the DAC runs from 3.3 V and the default bring-up path uses
     * the internal 2.5 V reference. That internal reference requires REF-DIV=1
     * on this rail; otherwise the DAC trips REF-ALARM and forces both outputs
     * to 0 V.
     */
    (void)gain_2x;

    if (reference == LASER_CONTROLLER_SERVICE_DAC_REFERENCE_INTERNAL &&
        !ref_div) {
        return false;
    }

    return true;
}

static esp_err_t laser_controller_board_dac_check_status(void)
{
    uint16_t status_reg = 0U;
    esp_err_t err =
        laser_controller_board_dac_read_command(
            LASER_CONTROLLER_DAC_STATUS_REG,
            &status_reg);

    if (err != ESP_OK) {
        s_dac_readback.last_error = (int32_t)err;
        return err;
    }

    s_dac_readback.status_reg = status_reg;
    s_dac_readback.ref_alarm =
        (status_reg & LASER_CONTROLLER_DAC_STATUS_REFALARM_BIT) != 0U;

    if ((status_reg & LASER_CONTROLLER_DAC_STATUS_REFALARM_BIT) != 0U) {
        s_dac_readback.last_error = ESP_ERR_INVALID_STATE;
        return ESP_ERR_INVALID_STATE;
    }

    s_dac_readback.last_error = ESP_OK;
    return ESP_OK;
}

static esp_err_t laser_controller_board_dac_init_safe(void)
{
    esp_err_t err;
    const laser_controller_time_ms_t now_ms = laser_controller_board_uptime_ms();

    if (s_dac_ready) {
        return ESP_OK;
    }

    if (s_dac_last_error != ESP_OK &&
        (now_ms - s_dac_last_attempt_ms) < LASER_CONTROLLER_DAC_RETRY_MS) {
        return s_dac_last_error;
    }

    s_dac_last_attempt_ms = now_ms;

    err = laser_controller_board_i2c_probe(LASER_CONTROLLER_DAC80502_ADDR);
    if (err != ESP_OK) {
        s_dac_last_error = err;
        laser_controller_board_clear_dac_readback();
        s_dac_readback.last_error = (int32_t)err;
        laser_controller_board_service_report_probe(
            LASER_CONTROLLER_MODULE_DAC,
            false,
            false);
        return err;
    }

    err = laser_controller_board_dac_write_command(
        LASER_CONTROLLER_DAC_TRIGGER_REG,
        LASER_CONTROLLER_DAC_SOFT_RESET_CODE);
    if (err != ESP_OK) {
        s_dac_last_error = err;
        return err;
    }

    esp_rom_delay_us(LASER_CONTROLLER_DAC_RESET_SETTLE_US);

    err = laser_controller_board_i2c_probe(LASER_CONTROLLER_DAC80502_ADDR);
    if (err != ESP_OK) {
        s_dac_last_error = err;
        laser_controller_board_clear_dac_readback();
        s_dac_readback.last_error = (int32_t)err;
        laser_controller_board_service_report_probe(
            LASER_CONTROLLER_MODULE_DAC,
            false,
            false);
        return err;
    }

    err = laser_controller_board_dac_write_command(
        LASER_CONTROLLER_DAC_SYNC_REG,
        LASER_CONTROLLER_DAC_SYNC_DEFAULT);
    if (err != ESP_OK) {
        s_dac_last_error = err;
        return err;
    }

    err = laser_controller_board_dac_write_command(
        LASER_CONTROLLER_DAC_CONFIG_REG,
        LASER_CONTROLLER_DAC_CONFIG_DEFAULT);
    if (err != ESP_OK) {
        s_dac_last_error = err;
        return err;
    }

    err = laser_controller_board_dac_write_command(
        LASER_CONTROLLER_DAC_GAIN_REG,
        LASER_CONTROLLER_DAC_GAIN_DEFAULT);
    if (err != ESP_OK) {
        s_dac_last_error = err;
        return err;
    }

    err = laser_controller_board_dac_write_command(
        LASER_CONTROLLER_DAC_DATA_A_REG,
        LASER_CONTROLLER_DAC_ZERO_SCALE_CODE);
    if (err != ESP_OK) {
        s_dac_last_error = err;
        return err;
    }

    err = laser_controller_board_dac_write_command(
        LASER_CONTROLLER_DAC_DATA_B_REG,
        LASER_CONTROLLER_DAC_ZERO_SCALE_CODE);
    if (err != ESP_OK) {
        s_dac_last_error = err;
        return err;
    }

    err = laser_controller_board_dac_check_status();
    if (err != ESP_OK) {
        s_dac_last_error = err;
        return err;
    }

    s_last_ld_voltage_v = 0.0f;
    s_last_tec_voltage_v = 0.0f;
    s_dac_ready = true;
    s_dac_last_error = ESP_OK;
    laser_controller_board_capture_dac_readback(true);
    laser_controller_board_service_report_probe(
        LASER_CONTROLLER_MODULE_DAC,
        true,
        true);
    return ESP_OK;
}

static uint16_t laser_controller_board_voltage_to_dac_code(float voltage_v)
{
    const float clamped =
        laser_controller_board_clamp_float(
            voltage_v,
            0.0f,
            LASER_CONTROLLER_DAC_FULL_SCALE_V);
    const float ratio = clamped / LASER_CONTROLLER_DAC_FULL_SCALE_V;
    const uint32_t code =
        (uint32_t)lroundf(ratio * (float)LASER_CONTROLLER_DAC_CODE_MAX);

    return (uint16_t)(code > LASER_CONTROLLER_DAC_CODE_MAX ?
                          LASER_CONTROLLER_DAC_CODE_MAX :
                          code);
}

/*
 * Oversample count for ADC reads — 8 samples averaged in one call at
 * ~3-5 us per `adc_oneshot_read`, so total cost per channel is ~25-40 us.
 * Combined with the single-pole IIR in `read_inputs` call sites, this
 * gives dead-stable TMO / LIO / TEC telemetry even over the noisy ESP32
 * internal ADC. See user directive 2026-04-15 (late).
 */
#define LASER_CONTROLLER_ADC_OVERSAMPLE_COUNT 8U

static esp_err_t laser_controller_board_read_adc_voltage(
    adc_channel_t channel,
    float *voltage_v)
{
    int raw = 0;
    esp_err_t err;
    uint32_t raw_sum = 0U;
    uint32_t sample_count = 0U;

    if (voltage_v == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    err = laser_controller_board_ensure_adc_ready();
    if (err != ESP_OK) {
        return err;
    }

    for (uint32_t i = 0U; i < LASER_CONTROLLER_ADC_OVERSAMPLE_COUNT; ++i) {
        err = adc_oneshot_read(s_adc_handle, channel, &raw);
        if (err != ESP_OK) {
            /*
             * If any sample fails mid-oversample, return the error so the
             * caller can skip the filter update on this tick. The IIR
             * then holds its previous state — this is the correct
             * behavior for a transient ADC read glitch.
             */
            return err;
        }
        raw_sum += (uint32_t)(raw < 0 ? 0 : raw);
        sample_count++;
    }

    const uint32_t raw_avg = raw_sum / sample_count;
    *voltage_v = ((float)raw_avg / 4095.0f) * 3.3f;
    return ESP_OK;
}

/*
 * Per-channel IIR low-pass filter state. Alpha = 0.25 balances smoothing
 * against response time: with a 5 ms control tick, an IIR step from 0 to
 * V_target reaches 99 % in ~70 ms. Much faster than operator perception
 * but heavy enough to kill visible jitter on the host readout.
 *
 * `primed` flags so the first valid sample after a rail-invalid window
 * initializes the filter to the raw value rather than interpolating
 * from a stale zero.
 */
#define LASER_CONTROLLER_ADC_IIR_ALPHA 0.25f

typedef struct {
    float value;
    bool primed;
} laser_controller_adc_filter_t;

static laser_controller_adc_filter_t s_adc_filter_ld_temp_v;
static laser_controller_adc_filter_t s_adc_filter_ld_current_v;
static laser_controller_adc_filter_t s_adc_filter_tec_temp_v;
static laser_controller_adc_filter_t s_adc_filter_tec_current;
static laser_controller_adc_filter_t s_adc_filter_tec_voltage;

static float laser_controller_adc_filter_update(
    laser_controller_adc_filter_t *filter,
    float sample)
{
    if (!filter->primed) {
        filter->value = sample;
        filter->primed = true;
    } else {
        filter->value +=
            LASER_CONTROLLER_ADC_IIR_ALPHA * (sample - filter->value);
    }
    return filter->value;
}

static void laser_controller_adc_filter_reset(
    laser_controller_adc_filter_t *filter)
{
    filter->value = 0.0f;
    filter->primed = false;
}

static float laser_controller_board_lookup_tec_temp_c(float voltage_v)
{
    if (voltage_v <= kTecTempCalibration[0].voltage_v) {
        return kTecTempCalibration[0].temp_c;
    }

    for (size_t index = 1U;
         index < sizeof(kTecTempCalibration) / sizeof(kTecTempCalibration[0]);
         ++index) {
        const float low_v = kTecTempCalibration[index - 1U].voltage_v;
        const float high_v = kTecTempCalibration[index].voltage_v;

        if (voltage_v <= high_v) {
            const float low_t = kTecTempCalibration[index - 1U].temp_c;
            const float high_t = kTecTempCalibration[index].temp_c;
            const float ratio =
                high_v > low_v ? (voltage_v - low_v) / (high_v - low_v) : 0.0f;
            return low_t + ((high_t - low_t) * ratio);
        }
    }

    return kTecTempCalibration[
               (sizeof(kTecTempCalibration) / sizeof(kTecTempCalibration[0])) - 1U]
        .temp_c;
}

static float laser_controller_board_decode_cc_current_a(uint8_t cc_status)
{
    const uint8_t cc1_state = (uint8_t)(cc_status & 0x03U);
    const uint8_t cc2_state = (uint8_t)((cc_status >> 2U) & 0x03U);
    const uint8_t active_state = cc1_state > cc2_state ? cc1_state : cc2_state;

    switch (active_state) {
        case 0x01U:
            return 0.5f;
        case 0x02U:
            return 1.5f;
        case 0x03U:
            return 3.0f;
        default:
            return 0.0f;
    }
}

static bool laser_controller_board_decode_pdo(
    const uint8_t *bytes,
    laser_controller_pd_pdo_t *pdo)
{
    uint32_t word;

    if (bytes == NULL || pdo == NULL) {
        return false;
    }

    word = (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) |
           ((uint32_t)bytes[3] << 24U);
    pdo->voltage_v = (float)((word >> 10U) & 0x3FFU) * 0.05f;
    pdo->current_a = (float)(word & 0x3FFU) * 0.01f;
    return pdo->voltage_v > 0.0f && pdo->current_a > 0.0f;
}

static uint32_t laser_controller_board_encode_fixed_pdo_word(
    float voltage_v,
    float current_a)
{
    const uint32_t voltage_steps =
        (uint32_t)lroundf(
            laser_controller_board_clamp_float(voltage_v, 0.0f, 20.0f) / 0.05f);
    const uint32_t current_steps =
        (uint32_t)lroundf(
            laser_controller_board_clamp_float(current_a, 0.0f, 5.0f) / 0.01f);

    return ((voltage_steps & 0x3FFU) << 10U) | (current_steps & 0x3FFU);
}

static void laser_controller_board_encode_fixed_pdo_bytes(
    float voltage_v,
    float current_a,
    uint8_t *bytes)
{
    const uint32_t word =
        laser_controller_board_encode_fixed_pdo_word(voltage_v, current_a);

    if (bytes == NULL) {
        return;
    }

    bytes[0] = (uint8_t)(word & 0xFFU);
    bytes[1] = (uint8_t)((word >> 8U) & 0xFFU);
    bytes[2] = (uint8_t)((word >> 16U) & 0xFFU);
    bytes[3] = (uint8_t)((word >> 24U) & 0xFFU);
}

static void laser_controller_board_normalize_pd_profiles(
    const laser_controller_service_pd_profile_t *profiles,
    laser_controller_service_pd_profile_t *normalized_profiles,
    uint8_t *profile_count)
{
    uint8_t normalized_count = 1U;

    if (normalized_profiles == NULL) {
        return;
    }

    memset(
        normalized_profiles,
        0,
        sizeof(*normalized_profiles) * LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT);

    /*
     * PDO1 is the USB-PD fallback / safe profile. The STUSB4500 fixes
     * PDO1 voltage at 5 V at the hardware level — writing a different
     * voltage has no effect because the chip enforces the USB-PD
     * initial-negotiation voltage internally. Only the OPERATING
     * CURRENT is programmable. `enabled` must stay TRUE because the
     * chip refuses to run without a fallback profile.
     */
    normalized_profiles[0].enabled = true;
    normalized_profiles[0].voltage_v = 5.0f;
    normalized_profiles[0].current_a =
        profiles != NULL && profiles[0].current_a > 0.0f ?
            laser_controller_board_clamp_float(profiles[0].current_a, 0.5f, 5.0f) :
            3.0f;

    if (profiles != NULL && profiles[1].enabled) {
        normalized_profiles[1].enabled = true;
        normalized_profiles[1].voltage_v =
            laser_controller_board_clamp_float(profiles[1].voltage_v, 5.0f, 20.0f);
        normalized_profiles[1].current_a =
            laser_controller_board_clamp_float(profiles[1].current_a, 0.5f, 5.0f);
        normalized_count = 2U;
    }

    if (profiles != NULL && normalized_profiles[1].enabled && profiles[2].enabled) {
        normalized_profiles[2].enabled = true;
        normalized_profiles[2].voltage_v =
            laser_controller_board_clamp_float(profiles[2].voltage_v, 5.0f, 20.0f);
        normalized_profiles[2].current_a =
            laser_controller_board_clamp_float(profiles[2].current_a, 0.5f, 5.0f);
        normalized_count = 3U;
    }

    if (profile_count != NULL) {
        *profile_count = normalized_count;
    }
}

static esp_err_t laser_controller_board_stusb_send_soft_reset(void)
{
    const uint8_t tx_header_payload[2] = {
        LASER_CONTROLLER_STUSB_TX_HEADER_LOW_REG,
        LASER_CONTROLLER_STUSB_SOFT_RESET_PAYLOAD,
    };
    const uint8_t command_payload[2] = {
        LASER_CONTROLLER_STUSB_PD_COMMAND_CTRL_REG,
        LASER_CONTROLLER_STUSB_SEND_COMMAND,
    };
    esp_err_t err;

    err = laser_controller_board_i2c_write(
        LASER_CONTROLLER_STUSB4500_ADDR,
        tx_header_payload,
        sizeof(tx_header_payload));
    if (err != ESP_OK) {
        return err;
    }

    return laser_controller_board_i2c_write(
        LASER_CONTROLLER_STUSB4500_ADDR,
        command_payload,
        sizeof(command_payload));
}

static void laser_controller_board_refresh_pd_snapshot(laser_controller_time_ms_t now_ms)
{
    uint8_t cc_status = 0U;
    uint8_t pdo_count = 0U;
    uint8_t rdo_bytes[4] = { 0 };
    uint8_t pdo_bytes[12] = { 0 };
    laser_controller_pd_pdo_t candidates[LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT] = { 0 };
    float cc_current_a = 0.0f;
    esp_err_t err;

    if (!s_pd_refresh_allowed &&
        s_pd_snapshot.updated_ms > 0U &&
        (now_ms - s_pd_snapshot.updated_ms) < LASER_CONTROLLER_PD_POLL_MS) {
        return;
    }

    const bool forced_refresh = s_pd_refresh_allowed;
    const laser_controller_pd_snapshot_source_t source =
        forced_refresh ?
            s_pd_refresh_requested_source :
            LASER_CONTROLLER_PD_SNAPSHOT_SOURCE_CACHED;

    s_pd_refresh_allowed = false;
    s_pd_refresh_requested_source = LASER_CONTROLLER_PD_SNAPSHOT_SOURCE_NONE;
    memset(&s_pd_snapshot, 0, sizeof(s_pd_snapshot));
    laser_controller_board_clear_pd_readback();
    s_pd_snapshot.updated_ms = now_ms;
    s_pd_snapshot.source = source;

    if (!laser_controller_service_module_expected(LASER_CONTROLLER_MODULE_PD)) {
        return;
    }

    err = laser_controller_board_i2c_probe(LASER_CONTROLLER_STUSB4500_ADDR);
    if (err != ESP_OK) {
        laser_controller_board_service_report_probe(
            LASER_CONTROLLER_MODULE_PD,
            false,
            false);
        return;
    }
    s_pd_readback.reachable = true;
    laser_controller_board_service_report_probe(
        LASER_CONTROLLER_MODULE_PD,
        true,
        true);

    err = laser_controller_board_i2c_read_reg_u8(
        LASER_CONTROLLER_STUSB4500_ADDR,
        LASER_CONTROLLER_STUSB_CC_STATUS_REG,
        &cc_status);
    if (err != ESP_OK) {
        laser_controller_board_service_report_probe(
            LASER_CONTROLLER_MODULE_PD,
            true,
            false);
        return;
    }
    s_pd_readback.cc_status_reg = cc_status;

    cc_current_a = laser_controller_board_decode_cc_current_a(cc_status);
    s_pd_snapshot.attached = cc_current_a > 0.0f;
    s_pd_readback.attached = s_pd_snapshot.attached;
    if (!s_pd_snapshot.attached) {
        return;
    }

    err = laser_controller_board_i2c_read_reg_u8(
        LASER_CONTROLLER_STUSB4500_ADDR,
        LASER_CONTROLLER_STUSB_DPM_PDO_NUMB,
        &pdo_count);
    if (err != ESP_OK) {
        laser_controller_board_service_report_probe(
            LASER_CONTROLLER_MODULE_PD,
            true,
            false);
        return;
    }
    s_pd_readback.pdo_count_reg = pdo_count;

    s_pd_snapshot.sink_profile_count = (uint8_t)(pdo_count & 0x07U);
    if (s_pd_snapshot.sink_profile_count > LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT) {
        s_pd_snapshot.sink_profile_count = LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT;
    }

    if (s_pd_snapshot.sink_profile_count > 0U) {
        err = laser_controller_board_i2c_read_stusb_block(
            LASER_CONTROLLER_STUSB_DPM_PDO1_0,
            pdo_bytes,
            sizeof(pdo_bytes));
        if (err != ESP_OK) {
            laser_controller_board_service_report_probe(
                LASER_CONTROLLER_MODULE_PD,
                true,
                false);
            return;
        }

        for (uint8_t index = 0U;
             index < LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT;
             ++index) {
            const bool valid =
                index < s_pd_snapshot.sink_profile_count &&
                laser_controller_board_decode_pdo(
                    &pdo_bytes[(size_t)index * 4U],
                    &candidates[index]);

            s_pd_snapshot.sink_profiles[index].enabled = valid;
            s_pd_snapshot.sink_profiles[index].voltage_v =
                valid ? candidates[index].voltage_v : 0.0f;
            s_pd_snapshot.sink_profiles[index].current_a =
                valid ? candidates[index].current_a : 0.0f;
        }
    }

    err = laser_controller_board_i2c_read_stusb_block(
        LASER_CONTROLLER_STUSB_RDO_STATUS_0,
        rdo_bytes,
        sizeof(rdo_bytes));
    if (err != ESP_OK) {
        laser_controller_board_service_report_probe(
            LASER_CONTROLLER_MODULE_PD,
            true,
            false);
        return;
    }

    {
        const uint32_t rdo =
            (uint32_t)rdo_bytes[0] |
            ((uint32_t)rdo_bytes[1] << 8U) |
            ((uint32_t)rdo_bytes[2] << 16U) |
            ((uint32_t)rdo_bytes[3] << 24U);
        const uint8_t object_position = (uint8_t)((rdo >> 28U) & 0x07U);
        const float operating_current_a = (float)((rdo >> 10U) & 0x3FFU) * 0.01f;
        const float max_current_a = (float)(rdo & 0x3FFU) * 0.01f;

        s_pd_readback.rdo_status_raw = rdo;
        s_pd_snapshot.contract_object_position = object_position;
        s_pd_snapshot.operating_current_a = operating_current_a;
        if (object_position == 0U) {
            s_pd_snapshot.contract_valid = true;
            s_pd_snapshot.host_only = cc_current_a <= 0.5f;
            s_pd_snapshot.source_voltage_v = 5.0f;
            s_pd_snapshot.source_current_a = cc_current_a;
            s_pd_snapshot.operating_current_a = cc_current_a;
            s_pd_snapshot.negotiated_power_w = 5.0f * cc_current_a;
            return;
        }

        s_pd_snapshot.contract_valid = true;
        s_pd_snapshot.host_only = false;
        s_pd_snapshot.source_current_a =
            max_current_a > 0.0f ? max_current_a : operating_current_a;

        if (object_position <= s_pd_snapshot.sink_profile_count &&
            candidates[object_position - 1U].voltage_v >= 5.0f) {
            s_pd_snapshot.source_voltage_v =
                candidates[object_position - 1U].voltage_v;
        } else {
            s_pd_snapshot.source_voltage_v = 5.0f;
            for (int index = (int)s_pd_snapshot.sink_profile_count - 1;
                 index >= 0;
                 --index) {
                if (candidates[index].voltage_v >= 5.0f &&
                    candidates[index].current_a + 0.01f >= operating_current_a) {
                    s_pd_snapshot.source_voltage_v = candidates[index].voltage_v;
                    break;
                }
            }
        }

        s_pd_snapshot.negotiated_power_w =
            s_pd_snapshot.source_voltage_v * operating_current_a;
    }
}

static esp_err_t laser_controller_board_imu_transfer(
    uint8_t reg,
    uint8_t *value,
    bool write)
{
    uint8_t tx_data[2] = { 0 };
    uint8_t rx_data[2] = { 0 };
    spi_transaction_t transaction = { 0 };
    esp_err_t err;

    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    err = laser_controller_board_ensure_spi_ready();
    if (err != ESP_OK) {
        return err;
    }

    tx_data[0] = write ? (uint8_t)(reg & 0x7FU) : (uint8_t)(reg | 0x80U);
    tx_data[1] = *value;

    transaction.length = 16U;
    transaction.tx_buffer = tx_data;
    transaction.rx_buffer = rx_data;

    laser_controller_board_lock_bus();
    err = spi_device_transmit(s_imu_spi, &transaction);
    laser_controller_board_unlock_bus();

    if (err == ESP_OK && !write) {
        *value = rx_data[1];
    }

    return err;
}

static esp_err_t laser_controller_board_imu_read_block(
    uint8_t start_reg,
    uint8_t *buffer,
    size_t len)
{
    uint8_t tx_data[16] = { 0 };
    uint8_t rx_data[16] = { 0 };
    spi_transaction_t transaction = { 0 };
    esp_err_t err;

    if (buffer == NULL || len == 0U || len > 15U) {
        return ESP_ERR_INVALID_ARG;
    }

    err = laser_controller_board_ensure_spi_ready();
    if (err != ESP_OK) {
        return err;
    }

    tx_data[0] = (uint8_t)(start_reg | 0x80U);
    transaction.length = (uint32_t)((len + 1U) * 8U);
    transaction.tx_buffer = tx_data;
    transaction.rx_buffer = rx_data;

    laser_controller_board_lock_bus();
    err = spi_device_transmit(s_imu_spi, &transaction);
    laser_controller_board_unlock_bus();
    if (err != ESP_OK) {
        return err;
    }

    memcpy(buffer, &rx_data[1], len);
    return ESP_OK;
}

static void laser_controller_board_apply_axis_transform(
    const laser_controller_config_t *config,
    const float imu_vector[3],
    float beam_vector[3])
{
    if (beam_vector == NULL || imu_vector == NULL) {
        return;
    }

    if (config == NULL) {
        beam_vector[0] = imu_vector[0];
        beam_vector[1] = imu_vector[1];
        beam_vector[2] = imu_vector[2];
        return;
    }

    for (size_t row = 0U; row < 3U; ++row) {
        beam_vector[row] =
            (config->axis_transform.beam_from_imu[row][0] * imu_vector[0]) +
            (config->axis_transform.beam_from_imu[row][1] * imu_vector[1]) +
            (config->axis_transform.beam_from_imu[row][2] * imu_vector[2]);
    }
}

static laser_controller_radians_t laser_controller_board_compute_beam_pitch(
    const laser_controller_config_t *config,
    const float accel_g[3])
{
    float beam_accel[3] = { 0.0f, 0.0f, 0.0f };
    float norm;
    float normalized_forward_gravity;

    laser_controller_board_apply_axis_transform(config, accel_g, beam_accel);
    norm = sqrtf(
        (beam_accel[0] * beam_accel[0]) +
        (beam_accel[1] * beam_accel[1]) +
        (beam_accel[2] * beam_accel[2]));
    if (norm < 0.001f) {
        return 0.0f;
    }

    normalized_forward_gravity =
        laser_controller_board_clamp_float(-beam_accel[0] / norm, -1.0f, 1.0f);
    return asinf(normalized_forward_gravity);
}

static laser_controller_radians_t laser_controller_board_compute_beam_roll(
    const laser_controller_config_t *config,
    const float accel_g[3])
{
    float beam_accel[3] = { 0.0f, 0.0f, 0.0f };
    float norm;

    laser_controller_board_apply_axis_transform(config, accel_g, beam_accel);
    norm = sqrtf(
        (beam_accel[0] * beam_accel[0]) +
        (beam_accel[1] * beam_accel[1]) +
        (beam_accel[2] * beam_accel[2]));
    if (norm < 0.001f) {
        return 0.0f;
    }

    return atan2f(beam_accel[1], -beam_accel[2]);
}

static laser_controller_radians_t laser_controller_board_wrap_radians(
    laser_controller_radians_t angle_rad)
{
    while (angle_rad > LASER_CONTROLLER_PI_RAD) {
        angle_rad -= LASER_CONTROLLER_TWO_PI_RAD;
    }

    while (angle_rad < -LASER_CONTROLLER_PI_RAD) {
        angle_rad += LASER_CONTROLLER_TWO_PI_RAD;
    }

    return angle_rad;
}

static esp_err_t laser_controller_board_ensure_imu_runtime_ready(void)
{
    uint8_t value = 0U;
    uint8_t ctrl1_xl = 0U;
    uint8_t ctrl2_g = 0U;
    uint8_t ctrl3_c = 0U;
    uint8_t ctrl4_c = 0U;
    uint8_t ctrl10_c = 0U;
    const laser_controller_time_ms_t now_ms = laser_controller_board_uptime_ms();
    esp_err_t err;

    if (s_imu_runtime.config.odr_hz == 0U) {
        laser_controller_board_load_default_imu_config(&s_imu_runtime.config);
    }

    if (s_imu_runtime.configured) {
        s_imu_readback.reachable = true;
        s_imu_readback.configured = true;
        return ESP_OK;
    }

    if (s_imu_runtime.last_error != ESP_OK &&
        (now_ms - s_imu_runtime.last_attempt_ms) < LASER_CONTROLLER_IMU_RETRY_MS) {
        return s_imu_runtime.last_error;
    }

    s_imu_runtime.last_attempt_ms = now_ms;
    err = laser_controller_board_imu_spi_read(
        LASER_CONTROLLER_LSM6DSO_WHOAMI_REG,
        &value);
    s_imu_readback.who_am_i = value;
    if (err != ESP_OK || value != LASER_CONTROLLER_LSM6DSO_WHOAMI_VALUE) {
        s_imu_runtime.last_error =
            err == ESP_OK ? ESP_ERR_INVALID_RESPONSE : err;
        s_imu_readback.last_error = (int32_t)s_imu_runtime.last_error;
        s_imu_runtime.identified = false;
        laser_controller_board_service_report_probe(
            LASER_CONTROLLER_MODULE_IMU,
            false,
            false);
        return s_imu_runtime.last_error;
    }

    s_imu_readback.reachable = true;

    value = LASER_CONTROLLER_LSM6DSO_RESET_BIT;
    err = laser_controller_board_imu_spi_write(
        LASER_CONTROLLER_LSM6DSO_CTRL3_C_REG,
        value);
    if (err != ESP_OK) {
        s_imu_runtime.last_error = err;
        return err;
    }

    for (uint32_t attempt = 0U; attempt < 20U; ++attempt) {
        esp_rom_delay_us(1000U);
        value = 0U;
        err = laser_controller_board_imu_spi_read(
            LASER_CONTROLLER_LSM6DSO_CTRL3_C_REG,
            &value);
        if (err != ESP_OK) {
            s_imu_runtime.last_error = err;
            s_imu_readback.last_error = (int32_t)err;
            return err;
        }
        if ((value & LASER_CONTROLLER_LSM6DSO_RESET_BIT) == 0U) {
            break;
        }
        if (attempt == 19U) {
            s_imu_runtime.last_error = ESP_ERR_TIMEOUT;
            return ESP_ERR_TIMEOUT;
        }
    }

    ctrl1_xl = (uint8_t)(
        (laser_controller_board_imu_odr_bits(s_imu_runtime.config.odr_hz) << 4U) |
        (laser_controller_board_imu_accel_fs_bits(
             s_imu_runtime.config.accel_range_g) << 2U) |
        (s_imu_runtime.config.lpf2_enabled ? 0x02U : 0x00U));
    ctrl2_g = laser_controller_board_imu_gyro_ctrl2_value(
        s_imu_runtime.config.odr_hz,
        s_imu_runtime.config.gyro_range_dps,
        s_imu_runtime.config.gyro_enabled);
    ctrl3_c = (uint8_t)(
        (s_imu_runtime.config.bdu_enabled ? LASER_CONTROLLER_LSM6DSO_BDU_BIT : 0U) |
        (s_imu_runtime.config.if_inc_enabled ? LASER_CONTROLLER_LSM6DSO_IF_INC_BIT : 0U));
    ctrl4_c = s_imu_runtime.config.i2c_disabled ?
                  LASER_CONTROLLER_LSM6DSO_I2C_DISABLE_BIT :
                  0U;
    ctrl10_c = s_imu_runtime.config.timestamp_enabled ?
                   LASER_CONTROLLER_LSM6DSO_TIMESTAMP_BIT :
                   0U;

    err = laser_controller_board_imu_spi_write(
        LASER_CONTROLLER_LSM6DSO_CTRL3_C_REG,
        ctrl3_c);
    if (err == ESP_OK) {
        err = laser_controller_board_imu_spi_write(
            LASER_CONTROLLER_LSM6DSO_CTRL4_C_REG,
            ctrl4_c);
    }
    if (err == ESP_OK) {
        err = laser_controller_board_imu_spi_write(
            LASER_CONTROLLER_LSM6DSO_CTRL10_C_REG,
            ctrl10_c);
    }
    if (err == ESP_OK) {
        err = laser_controller_board_imu_spi_write(
            LASER_CONTROLLER_LSM6DSO_CTRL2_G_REG,
            ctrl2_g);
    }
    if (err == ESP_OK) {
        err = laser_controller_board_imu_spi_write(
            LASER_CONTROLLER_LSM6DSO_CTRL1_XL_REG,
            ctrl1_xl);
    }
    if (err != ESP_OK) {
        s_imu_runtime.last_error = err;
        s_imu_readback.last_error = (int32_t)err;
        s_imu_runtime.configured = false;
        return err;
    }

    s_imu_runtime.identified = true;
    s_imu_runtime.configured = true;
    s_imu_runtime.last_error = ESP_OK;
    s_imu_readback.configured = true;
    s_imu_readback.ctrl1_xl_reg = ctrl1_xl;
    s_imu_readback.ctrl2_g_reg = ctrl2_g;
    s_imu_readback.ctrl3_c_reg = ctrl3_c;
    s_imu_readback.ctrl4_c_reg = ctrl4_c;
    s_imu_readback.ctrl10_c_reg = ctrl10_c;
    s_imu_readback.last_error = ESP_OK;
    laser_controller_board_service_report_probe(
        LASER_CONTROLLER_MODULE_IMU,
        true,
        true);
    return ESP_OK;
}

static void laser_controller_board_read_imu_inputs(
    laser_controller_board_inputs_t *inputs,
    const laser_controller_config_t *config,
    laser_controller_time_ms_t now_ms)
{
    laser_controller_service_imu_config_t imu_config;
    uint8_t status_reg = 0U;
    uint8_t raw_bytes[12] = { 0 };
    esp_err_t err;

    if (inputs == NULL) {
        return;
    }

    laser_controller_service_get_imu_config(&imu_config);
    if (s_imu_runtime.config.odr_hz != imu_config.odr_hz ||
        s_imu_runtime.config.accel_range_g != imu_config.accel_range_g ||
        s_imu_runtime.config.gyro_range_dps != imu_config.gyro_range_dps ||
        s_imu_runtime.config.gyro_enabled != imu_config.gyro_enabled ||
        s_imu_runtime.config.lpf2_enabled != imu_config.lpf2_enabled ||
        s_imu_runtime.config.timestamp_enabled !=
            imu_config.timestamp_enabled ||
        s_imu_runtime.config.bdu_enabled != imu_config.bdu_enabled ||
        s_imu_runtime.config.if_inc_enabled != imu_config.if_inc_enabled ||
        s_imu_runtime.config.i2c_disabled != imu_config.i2c_disabled) {
        s_imu_runtime.config.odr_hz = imu_config.odr_hz;
        s_imu_runtime.config.accel_range_g = imu_config.accel_range_g;
        s_imu_runtime.config.gyro_range_dps = imu_config.gyro_range_dps;
        s_imu_runtime.config.gyro_enabled = imu_config.gyro_enabled;
        s_imu_runtime.config.lpf2_enabled = imu_config.lpf2_enabled;
        s_imu_runtime.config.timestamp_enabled =
            imu_config.timestamp_enabled;
        s_imu_runtime.config.bdu_enabled = imu_config.bdu_enabled;
        s_imu_runtime.config.if_inc_enabled = imu_config.if_inc_enabled;
        s_imu_runtime.config.i2c_disabled = imu_config.i2c_disabled;
        s_imu_runtime.configured = false;
        s_imu_runtime.identified = false;
        s_imu_runtime.last_error = ESP_OK;
        s_imu_runtime.last_attempt_ms = 0U;
        s_imu_runtime.last_sample_ms = 0U;
        s_imu_runtime.last_orientation_ms = 0U;
        memset(s_imu_runtime.accel_g, 0, sizeof(s_imu_runtime.accel_g));
        memset(s_imu_runtime.gyro_dps, 0, sizeof(s_imu_runtime.gyro_dps));
        s_imu_runtime.beam_pitch_rad = 0.0f;
        s_imu_runtime.beam_roll_rad = 0.0f;
        s_imu_runtime.beam_yaw_rad = 0.0f;
    }

    err = laser_controller_board_ensure_imu_runtime_ready();
    if (err != ESP_OK) {
        s_imu_readback.last_error = (int32_t)err;
        inputs->imu_data_valid = false;
        inputs->imu_data_fresh = false;
        inputs->beam_pitch_rad = 0.0f;
        inputs->beam_roll_rad = 0.0f;
        inputs->beam_yaw_rad = 0.0f;
        return;
    }

    err = laser_controller_board_imu_spi_read(
        LASER_CONTROLLER_LSM6DSO_STATUS_REG,
        &status_reg);
    if (err == ESP_OK) {
        s_imu_readback.status_reg = status_reg;
        s_imu_readback.last_error = ESP_OK;
    }
    if (err == ESP_OK &&
        (((status_reg & LASER_CONTROLLER_LSM6DSO_XLDA_BIT) != 0U) ||
         (s_imu_runtime.config.gyro_enabled &&
          (status_reg & LASER_CONTROLLER_LSM6DSO_GDA_BIT) != 0U) ||
         s_imu_runtime.last_sample_ms == 0U)) {
        err = laser_controller_board_imu_read_block(
            s_imu_runtime.config.gyro_enabled ?
                LASER_CONTROLLER_LSM6DSO_OUTX_L_G_REG :
                LASER_CONTROLLER_LSM6DSO_OUTX_L_A_REG,
            raw_bytes,
            s_imu_runtime.config.gyro_enabled ? 12U : 6U);
        if (err == ESP_OK) {
            const float g_per_lsb =
                laser_controller_board_imu_accel_g_per_lsb(
                    s_imu_runtime.config.accel_range_g);
            const size_t accel_offset =
                s_imu_runtime.config.gyro_enabled ? 6U : 0U;
            const int16_t raw_accel_x =
                (int16_t)(((uint16_t)raw_bytes[accel_offset + 1U] << 8U) |
                          raw_bytes[accel_offset + 0U]);
            const int16_t raw_accel_y =
                (int16_t)(((uint16_t)raw_bytes[accel_offset + 3U] << 8U) |
                          raw_bytes[accel_offset + 2U]);
            const int16_t raw_accel_z =
                (int16_t)(((uint16_t)raw_bytes[accel_offset + 5U] << 8U) |
                          raw_bytes[accel_offset + 4U]);

            s_imu_runtime.accel_g[0] = (float)raw_accel_x * g_per_lsb;
            s_imu_runtime.accel_g[1] = (float)raw_accel_y * g_per_lsb;
            s_imu_runtime.accel_g[2] = (float)raw_accel_z * g_per_lsb;

            if (s_imu_runtime.config.gyro_enabled) {
                const float dps_per_lsb =
                    laser_controller_board_imu_gyro_dps_per_lsb(
                        s_imu_runtime.config.gyro_range_dps);
                const int16_t raw_gyro_x =
                    (int16_t)(((uint16_t)raw_bytes[1] << 8U) | raw_bytes[0]);
                const int16_t raw_gyro_y =
                    (int16_t)(((uint16_t)raw_bytes[3] << 8U) | raw_bytes[2]);
                const int16_t raw_gyro_z =
                    (int16_t)(((uint16_t)raw_bytes[5] << 8U) | raw_bytes[4]);
                float beam_gyro_dps[3] = { 0.0f, 0.0f, 0.0f };

                s_imu_runtime.gyro_dps[0] = (float)raw_gyro_x * dps_per_lsb;
                s_imu_runtime.gyro_dps[1] = (float)raw_gyro_y * dps_per_lsb;
                s_imu_runtime.gyro_dps[2] = (float)raw_gyro_z * dps_per_lsb;
                laser_controller_board_apply_axis_transform(
                    config,
                    s_imu_runtime.gyro_dps,
                    beam_gyro_dps);
                if (s_imu_runtime.last_orientation_ms > 0U &&
                    now_ms > s_imu_runtime.last_orientation_ms) {
                    const float dt_s =
                        (float)(now_ms - s_imu_runtime.last_orientation_ms) / 1000.0f;
                    s_imu_runtime.beam_yaw_rad = laser_controller_board_wrap_radians(
                        s_imu_runtime.beam_yaw_rad +
                        (beam_gyro_dps[2] * dt_s * LASER_CONTROLLER_RAD_PER_DEG));
                }
            } else {
                memset(s_imu_runtime.gyro_dps, 0, sizeof(s_imu_runtime.gyro_dps));
                s_imu_runtime.beam_yaw_rad = 0.0f;
            }

            s_imu_runtime.beam_pitch_rad = laser_controller_board_compute_beam_pitch(
                config,
                s_imu_runtime.accel_g);
            s_imu_runtime.beam_roll_rad = laser_controller_board_compute_beam_roll(
                config,
                s_imu_runtime.accel_g);
            s_imu_runtime.last_sample_ms = now_ms;
            s_imu_runtime.last_orientation_ms = now_ms;
        }
    }

    if (err != ESP_OK) {
        s_imu_readback.last_error = (int32_t)err;
        laser_controller_board_service_report_probe(
            LASER_CONTROLLER_MODULE_IMU,
            true,
            false);
        inputs->imu_data_valid = false;
        inputs->imu_data_fresh = false;
        inputs->beam_pitch_rad = 0.0f;
        inputs->beam_roll_rad = 0.0f;
        inputs->beam_yaw_rad = 0.0f;
        return;
    }

    if (s_imu_runtime.last_sample_ms > 0U) {
        const float norm_g = sqrtf(
            (s_imu_runtime.accel_g[0] * s_imu_runtime.accel_g[0]) +
            (s_imu_runtime.accel_g[1] * s_imu_runtime.accel_g[1]) +
            (s_imu_runtime.accel_g[2] * s_imu_runtime.accel_g[2]));
        const uint32_t stale_ms =
            config != NULL ? config->timeouts.imu_stale_ms : 50U;

        inputs->imu_data_valid = norm_g > 0.35f && norm_g < 1.65f;
        inputs->imu_data_fresh =
            (now_ms - s_imu_runtime.last_sample_ms) <= stale_ms;
        inputs->beam_pitch_rad =
            inputs->imu_data_valid ? s_imu_runtime.beam_pitch_rad : 0.0f;
        inputs->beam_roll_rad =
            inputs->imu_data_valid ? s_imu_runtime.beam_roll_rad : 0.0f;
        inputs->beam_yaw_rad =
            inputs->imu_data_valid && s_imu_runtime.config.gyro_enabled ?
                s_imu_runtime.beam_yaw_rad :
                0.0f;
        laser_controller_board_service_report_probe(
            LASER_CONTROLLER_MODULE_IMU,
            true,
            inputs->imu_data_valid && inputs->imu_data_fresh);
        return;
    }

    inputs->imu_data_valid = false;
    inputs->imu_data_fresh = false;
    inputs->beam_pitch_rad = 0.0f;
    inputs->beam_roll_rad = 0.0f;
    inputs->beam_yaw_rad = 0.0f;
}

static uint8_t laser_controller_board_haptic_mode_value(
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

static esp_err_t laser_controller_board_drv2605_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t payload[2] = { reg, value };

    return laser_controller_board_i2c_tx(
        LASER_CONTROLLER_DRV2605_ADDR,
        payload,
        sizeof(payload));
}

static void laser_controller_board_disable_pcn_pwm_and_drive(bool low_current)
{
    if (s_pwm_active) {
        (void)ledc_stop(
            LASER_CONTROLLER_PCN_PWM_SPEED_MODE,
            LASER_CONTROLLER_PCN_PWM_CHANNEL,
            0U);
        s_pwm_active = false;
    }

    (void)gpio_set_direction(LASER_CONTROLLER_GPIO_LD_PCN, GPIO_MODE_INPUT_OUTPUT);
    (void)gpio_set_level(
        LASER_CONTROLLER_GPIO_LD_PCN,
        low_current ? 0 : 1);
}

void laser_controller_board_init_safe_defaults(void)
{
    (void)memset(&s_mock_inputs, 0, sizeof(s_mock_inputs));
    (void)memset(&s_imu_runtime, 0, sizeof(s_imu_runtime));
    s_last_outputs = kSafeOutputs;
    s_pd_snapshot.updated_ms = 0U;
    s_pd_snapshot.source = LASER_CONTROLLER_PD_SNAPSHOT_SOURCE_NONE;
    s_pd_refresh_allowed = false;
    s_pd_refresh_requested_source = LASER_CONTROLLER_PD_SNAPSHOT_SOURCE_NONE;
    s_tof_runtime_owns_sideband = false;
    s_last_ld_voltage_v = -1.0f;
    s_last_tec_voltage_v = -1.0f;
    s_tof_last_poll_ms = 0U;
    s_tof_last_error = ESP_OK;
    s_dac_ready = false;
    s_dac_last_error = ESP_OK;
    s_dac_last_attempt_ms = 0U;
    s_tof_illumination_pwm_active = false;
    s_tof_sideband_mode = LASER_CONTROLLER_TOF_SIDEBAND_MODE_FLOAT;
    s_tof_sideband_applied_duty_cycle_pct = 0U;
    s_tof_sideband_applied_frequency_hz = 0U;
    laser_controller_board_clear_dac_readback();
    laser_controller_board_clear_pd_readback();
    laser_controller_board_clear_imu_readback();
    laser_controller_board_clear_haptic_readback();
    laser_controller_board_clear_tof_runtime();
    laser_controller_board_clear_tof_readback();
    memset(&s_tof_illumination, 0, sizeof(s_tof_illumination));
    s_tof_illumination.frequency_hz = LASER_CONTROLLER_TOF_LED_PWM_DEFAULT_FREQ_HZ;
    memset(&s_gpio_inspector, 0, sizeof(s_gpio_inspector));
    laser_controller_board_load_default_imu_config(&s_imu_runtime.config);

    if (s_bus_mutex == NULL) {
        s_bus_mutex = xSemaphoreCreateMutexStatic(&s_bus_mutex_buffer);
    }

    (void)laser_controller_board_ensure_gpio_ready();
    (void)laser_controller_board_ensure_adc_ready();
    (void)laser_controller_board_ensure_i2c_ready();
    (void)laser_controller_board_ensure_spi_ready();
    laser_controller_board_apply_tof_sideband_state(false);
    laser_controller_board_disable_pcn_pwm_and_drive(true);

    /*
     * Button-board init must run AFTER I2C is ready and BEFORE the
     * control task starts. Failures here are non-fatal — the rest of
     * the system boots with `board_reachable=false` and the safety
     * decision falls back to host-only request routing.
     */
    memset(&s_button_readback, 0, sizeof(s_button_readback));
    memset(&s_rgb_readback, 0, sizeof(s_rgb_readback));
    memset(&s_last_button_state, 0, sizeof(s_last_button_state));
    (void)laser_controller_buttons_init(&s_button_readback);
    (void)laser_controller_rgb_led_init(&s_rgb_readback);
    (void)laser_controller_rgb_led_force_off(&s_rgb_readback);

    /*
     * 2026-04-17 (user directive): every non-rail peripheral must be
     * initialized and probed on USB power from boot, not deferred to
     * service/deployment entry. Only TEC and LD power supplies stay
     * gated on external PD. Failures are non-fatal — each peripheral's
     * capture_*_readback in read_inputs runs every tick and re-probes,
     * so a peripheral plugged in after boot still comes online.
     */
    (void)laser_controller_board_dac_init_safe();
}

void laser_controller_board_read_inputs(
    laser_controller_board_inputs_t *inputs,
    const laser_controller_config_t *config)
{
    float voltage_v = 0.0f;
    const laser_controller_time_ms_t now_ms = laser_controller_board_uptime_ms();
    const bool imu_expected =
        laser_controller_service_module_expected(
            LASER_CONTROLLER_MODULE_IMU);

    if (inputs == NULL) {
        return;
    }

    if (s_mock_override_enabled) {
        *inputs = s_mock_inputs;
        return;
    }

    memset(inputs, 0, sizeof(*inputs));
    inputs->comms_alive = true;
    inputs->watchdog_ok = true;
    inputs->tec_command_voltage_v =
        s_last_tec_voltage_v >= 0.0f ? s_last_tec_voltage_v : 0.0f;
    inputs->ld_rail_pgood =
        gpio_get_level(LASER_CONTROLLER_GPIO_PWR_LD_PGOOD) != 0;
    inputs->tec_rail_pgood =
        gpio_get_level(LASER_CONTROLLER_GPIO_PWR_TEC_PGOOD) != 0;
    inputs->ld_telemetry_valid =
        inputs->ld_rail_pgood &&
        (gpio_get_level(LASER_CONTROLLER_GPIO_LD_SBDN) != 0);
    inputs->tec_telemetry_valid = inputs->tec_rail_pgood;
    inputs->tec_temp_good =
        inputs->tec_telemetry_valid &&
        (gpio_get_level(LASER_CONTROLLER_GPIO_TEC_TEMPGD) != 0);

    if (inputs->ld_telemetry_valid) {
        inputs->driver_loop_good =
            gpio_get_level(LASER_CONTROLLER_GPIO_LD_LPGD) != 0;

        /*
         * LD TMO (driver temperature monitor) — ADC0. 8x oversampled
         * inside `read_adc_voltage`, plus single-pole IIR (alpha=0.25)
         * here. TMO previously jumped several °C tick-to-tick from raw
         * ADC noise; this stack is dead-stable for operator display.
         * User directive 2026-04-15 (late).
         */
        if (laser_controller_board_read_adc_voltage(ADC_CHANNEL_0, &voltage_v) == ESP_OK) {
            const float filtered_v = laser_controller_adc_filter_update(
                &s_adc_filter_ld_temp_v, voltage_v);
            inputs->laser_driver_temp_voltage_v = filtered_v;
            inputs->laser_driver_temp_c = 192.5576f - (90.1040f * filtered_v);
        }

        /*
         * LD LIO (laser current monitor) — ADC1. Same oversample + IIR
         * stack as TMO. This feeds the `unexpected current` safety
         * interlock; a jittery reading used to hover near the
         * `off_current_threshold_a` boundary and briefly trip when idle.
         */
        if (laser_controller_board_read_adc_voltage(ADC_CHANNEL_1, &voltage_v) == ESP_OK) {
            const float filtered_v = laser_controller_adc_filter_update(
                &s_adc_filter_ld_current_v, voltage_v);
            /*
             * Apply per-unit LIO voltage calibration offset (Bug 5 fix
             * 2026-04-17). User reported the raw readback was ~70 mV
             * lower than the actual driver LIO pin voltage; offset is a
             * config field with default 0.07 V and ±0.5 V validation.
             */
            const float corrected_v =
                filtered_v + (config != NULL ?
                    (float)config->thresholds.lio_voltage_offset_v : 0.0f);
            inputs->laser_current_monitor_voltage_v = corrected_v;
            inputs->measured_laser_current_a = 2.4f * corrected_v;
        }
    } else {
        inputs->driver_loop_good = false;
        inputs->laser_driver_temp_voltage_v = 0.0f;
        inputs->laser_driver_temp_c = 0.0f;
        inputs->laser_current_monitor_voltage_v = 0.0f;
        inputs->measured_laser_current_a = 0.0f;
        /*
         * Reset the LD filters when telemetry becomes invalid (rail off
         * or SBDN OFF). The next valid sample re-primes the filter to
         * the raw value rather than interpolating from a stale frame.
         */
        laser_controller_adc_filter_reset(&s_adc_filter_ld_temp_v);
        laser_controller_adc_filter_reset(&s_adc_filter_ld_current_v);
    }

    /*
     * 2026-04-17 (user directive): everything except TEC and LD rails
     * must be testable on USB power without requiring bring-up
     * declaration. Probe DAC on every tick; if it is not physically
     * present, the probe fails and reachable stays false naturally.
     * (Before this change the capture was gated on bring-up-expected,
     * so operators saw "Not installed" on every status-rail card
     * until they explicitly declared the module.)
     */
    laser_controller_board_capture_dac_readback(s_dac_ready);

    if (inputs->tec_telemetry_valid) {
        if (laser_controller_board_read_adc_voltage(ADC_CHANNEL_7, &voltage_v) == ESP_OK) {
            const float filtered_v = laser_controller_adc_filter_update(
                &s_adc_filter_tec_temp_v, voltage_v);
            inputs->tec_temp_adc_voltage_v = filtered_v;
            inputs->tec_temp_c = laser_controller_board_lookup_tec_temp_c(filtered_v);
        }

        if (laser_controller_board_read_adc_voltage(ADC_CHANNEL_8, &voltage_v) == ESP_OK) {
            const float filtered_v = laser_controller_adc_filter_update(
                &s_adc_filter_tec_current, voltage_v);
            inputs->tec_current_a = (filtered_v - 1.25f) / 0.285f;
        }

        if (laser_controller_board_read_adc_voltage(ADC_CHANNEL_9, &voltage_v) == ESP_OK) {
            const float filtered_v = laser_controller_adc_filter_update(
                &s_adc_filter_tec_voltage, voltage_v);
            inputs->tec_voltage_v = filtered_v * 2.0f;
        }
    } else {
        inputs->tec_temp_c = 0.0f;
        inputs->tec_temp_adc_voltage_v = 0.0f;
        inputs->tec_current_a = 0.0f;
        inputs->tec_voltage_v = 0.0f;
        inputs->tec_temp_good = false;
        laser_controller_adc_filter_reset(&s_adc_filter_tec_temp_v);
        laser_controller_adc_filter_reset(&s_adc_filter_tec_current);
        laser_controller_adc_filter_reset(&s_adc_filter_tec_voltage);
    }

    /*
     * 2026-04-17 (user directive): IMU probed on every tick from
     * boot. USB power alone is enough to run the LSM6DSO; bring-up
     * declaration should not gate the physical probe. If the IMU is
     * not present the SPI read fails and reachable stays false.
     */
    (void)imu_expected;
    laser_controller_board_read_imu_inputs(inputs, config, now_ms);
    inputs->tof_data_valid = false;
    inputs->tof_data_fresh = false;
    inputs->tof_distance_m = 0.0f;
    inputs->pcn_pwm_active = s_pwm_active;
    inputs->tof_illumination_pwm_active = s_tof_illumination_pwm_active;
    laser_controller_board_capture_tof_readback(now_ms, config, inputs);

    laser_controller_board_capture_haptic_readback();
    laser_controller_board_refresh_pd_snapshot(now_ms);
    inputs->pd_contract_valid = s_pd_snapshot.contract_valid;
    inputs->pd_snapshot_fresh =
        s_pd_snapshot.updated_ms > 0U &&
        (now_ms - s_pd_snapshot.updated_ms) <= LASER_CONTROLLER_PD_SNAPSHOT_FRESH_MS;
    inputs->pd_source_is_host_only = s_pd_snapshot.host_only;
    inputs->pd_negotiated_power_w = s_pd_snapshot.negotiated_power_w;
    inputs->pd_source_voltage_v = s_pd_snapshot.source_voltage_v;
    inputs->pd_source_current_a = s_pd_snapshot.source_current_a;
    inputs->pd_operating_current_a = s_pd_snapshot.operating_current_a;
    inputs->pd_contract_object_position = s_pd_snapshot.contract_object_position;
    inputs->pd_sink_profile_count = s_pd_snapshot.sink_profile_count;
    inputs->pd_last_updated_ms = s_pd_snapshot.updated_ms;
    inputs->pd_source =
        inputs->pd_snapshot_fresh ?
            s_pd_snapshot.source :
        s_pd_snapshot.updated_ms > 0U ?
            LASER_CONTROLLER_PD_SNAPSHOT_SOURCE_CACHED :
            LASER_CONTROLLER_PD_SNAPSHOT_SOURCE_NONE;
    memcpy(
        inputs->pd_sink_profiles,
        s_pd_snapshot.sink_profiles,
        sizeof(inputs->pd_sink_profiles));
    inputs->dac = s_dac_readback;
    inputs->pd_readback = s_pd_readback;
    inputs->imu_readback = s_imu_readback;
    inputs->haptic_readback = s_haptic_readback;
    inputs->tof_readback = s_tof_readback;

    /*
     * Button board read. Failures clear pressed state automatically (see
     * laser_controller_buttons_refresh fail-safe) so a stuck bus cannot
     * latch the laser on. The previous-tick state lives in
     * s_last_button_state for edge detection — kept on the control task
     * (the only caller of read_inputs).
     */
    (void)laser_controller_buttons_refresh(
        &inputs->button,
        &s_last_button_state,
        &s_button_readback);
    s_last_button_state = inputs->button;
    inputs->buttons_readback = s_button_readback;
    inputs->rgb_readback = s_rgb_readback;

    laser_controller_board_capture_gpio_inspector();
    inputs->gpio_inspector = s_gpio_inspector;
}

void laser_controller_board_apply_outputs(const laser_controller_board_outputs_t *outputs)
{
    if (outputs == NULL) {
        return;
    }

    s_last_outputs = *outputs;
    laser_controller_board_drive_safe_gpio_levels(outputs);
    laser_controller_board_apply_gpio_overrides();
    laser_controller_board_apply_tof_sideband_state(
        laser_controller_board_any_owns_tof_sideband());
    /*
     * Apply RGB status LED. Dirty-compare lives inside the driver — the
     * I2C transaction only runs when the requested state actually changed.
     * Failures are non-fatal at this layer; the next tick retries because
     * the dirty-compare check sees `last_applied != requested`.
     */
    (void)laser_controller_rgb_led_apply(&outputs->rgb_led, &s_rgb_readback);
}

void laser_controller_board_apply_debug_gpio_state_now(void)
{
    if (!s_gpio_ready) {
        (void)laser_controller_board_ensure_gpio_ready();
    }

    laser_controller_board_drive_safe_gpio_levels(&s_last_outputs);
    laser_controller_board_apply_gpio_overrides();
    laser_controller_board_apply_tof_sideband_state(
        laser_controller_board_any_owns_tof_sideband());
    laser_controller_board_refresh_haptic_gpio_levels();
    laser_controller_board_capture_gpio_inspector();
}

void laser_controller_board_get_last_outputs(laser_controller_board_outputs_t *outputs)
{
    if (outputs == NULL) {
        return;
    }

    *outputs = s_last_outputs;
}

laser_controller_time_ms_t laser_controller_board_uptime_ms(void)
{
    return (laser_controller_time_ms_t)(esp_timer_get_time() / 1000LL);
}

esp_err_t laser_controller_board_i2c_probe(uint32_t address)
{
    esp_err_t err = ESP_OK;

    for (uint32_t attempt = 0U; attempt < 2U; ++attempt) {
        err = laser_controller_board_ensure_i2c_ready();
        if (err != ESP_OK) {
            if (attempt == 0U && laser_controller_board_i2c_error_needs_recovery(err)) {
                laser_controller_board_recover_shared_i2c_bus();
                continue;
            }
            return err;
        }

        laser_controller_board_lock_bus();
        err = i2c_master_probe(
            s_i2c_bus,
            (uint16_t)(address & 0x7FU),
            LASER_CONTROLLER_I2C_TIMEOUT_MS);
        laser_controller_board_unlock_bus();

        if (err == ESP_OK || !laser_controller_board_i2c_error_needs_recovery(err) ||
            attempt > 0U) {
            return err;
        }

        laser_controller_board_recover_shared_i2c_bus();
    }

    return err;
}

void laser_controller_board_get_shared_i2c_line_levels(
    bool *sda_high,
    bool *scl_high)
{
    laser_controller_board_get_shared_i2c_levels_internal(sda_high, scl_high);
}

esp_err_t laser_controller_board_i2c_write(
    uint32_t address,
    const uint8_t *tx_data,
    size_t tx_len)
{
    return laser_controller_board_i2c_tx(address, tx_data, tx_len);
}

esp_err_t laser_controller_board_i2c_write_read(
    uint32_t address,
    const uint8_t *tx_data,
    size_t tx_len,
    uint8_t *rx_data,
    size_t rx_len)
{
    return laser_controller_board_i2c_txrx(
        address,
        tx_data,
        tx_len,
        rx_data,
        rx_len);
}

/*
 * 2026-04-16 HARD RULE enforcement helpers: see header comment on
 * `laser_controller_board_authorize_pd_write_window`.
 */
void laser_controller_board_authorize_pd_write_window(uint32_t hold_ms)
{
    const laser_controller_time_ms_t now = laser_controller_board_uptime_ms();
    s_pd_write_authorization_until_ms = now + hold_ms;
}

static bool laser_controller_board_pd_write_authorized(void)
{
    const laser_controller_time_ms_t now = laser_controller_board_uptime_ms();
    return s_pd_write_authorization_until_ms != 0U &&
           now <= s_pd_write_authorization_until_ms;
}

esp_err_t laser_controller_board_configure_pd_debug(
    const laser_controller_service_pd_profile_t *profiles,
    size_t profile_count)
{
    /*
     * 2026-04-16 HARD RULE: only proceed if the operator-initiated
     * authorization window is open. Comms task arms the window via
     * `laser_controller_board_authorize_pd_write_window()` immediately
     * before invoking this function. Any other caller (e.g. a future
     * regression that adds a control-loop auto-write) hits this gate
     * and returns ESP_ERR_INVALID_STATE without writing anything.
     */
    if (!laser_controller_board_pd_write_authorized()) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Consume the authorization on first use so a second write needs a
     * fresh operator click. */
    s_pd_write_authorization_until_ms = 0U;
    laser_controller_service_pd_profile_t
        normalized_profiles[LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT];
    uint8_t encoded_profile_count = 1U;
    uint8_t count_payload[2] = {
        LASER_CONTROLLER_STUSB_DPM_PDO_NUMB,
        1U,
    };
    uint8_t single_pdo_payload[5] = { 0 };
    uint8_t verify_count = 0U;
    uint8_t pdo_payload[1U + (LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT * 4U)] = {
        LASER_CONTROLLER_STUSB_DPM_PDO1_0,
    };
    uint8_t verify_pdo_payload[LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT * 4U] = { 0 };
    esp_err_t err;

    if (profiles == NULL ||
        profile_count != LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!laser_controller_service_module_write_enabled(LASER_CONTROLLER_MODULE_PD)) {
        return ESP_ERR_INVALID_STATE;
    }

    err = laser_controller_board_i2c_probe(LASER_CONTROLLER_STUSB4500_ADDR);
    if (err != ESP_OK) {
        laser_controller_board_service_report_probe(
            LASER_CONTROLLER_MODULE_PD,
            false,
            false);
        return err;
    }

    laser_controller_board_normalize_pd_profiles(
        profiles,
        normalized_profiles,
        &encoded_profile_count);

    count_payload[1] = encoded_profile_count;
    for (uint8_t index = 0U;
         index < LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT;
         ++index) {
        laser_controller_board_encode_fixed_pdo_bytes(
            normalized_profiles[index].enabled ?
                normalized_profiles[index].voltage_v :
                0.0f,
            normalized_profiles[index].enabled ?
                normalized_profiles[index].current_a :
                0.0f,
            &pdo_payload[1U + ((size_t)index * 4U)]);
    }

    for (uint8_t index = 0U;
         err == ESP_OK && index < LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT;
         ++index) {
        single_pdo_payload[0] =
            (uint8_t)(LASER_CONTROLLER_STUSB_DPM_PDO1_0 + (index * 4U));
        memcpy(
            &single_pdo_payload[1],
            &pdo_payload[1U + ((size_t)index * 4U)],
            4U);
        err = laser_controller_board_i2c_write(
            LASER_CONTROLLER_STUSB4500_ADDR,
            single_pdo_payload,
            sizeof(single_pdo_payload));
    }
    if (err == ESP_OK) {
        err = laser_controller_board_i2c_write(
            LASER_CONTROLLER_STUSB4500_ADDR,
            count_payload,
            sizeof(count_payload));
    }
    if (err == ESP_OK) {
        err = laser_controller_board_i2c_read_reg_u8(
            LASER_CONTROLLER_STUSB4500_ADDR,
            LASER_CONTROLLER_STUSB_DPM_PDO_NUMB,
            &verify_count);
    }
    if (err == ESP_OK &&
        (verify_count & 0x07U) != encoded_profile_count) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    if (err == ESP_OK) {
        err = laser_controller_board_i2c_read_stusb_block(
            LASER_CONTROLLER_STUSB_DPM_PDO1_0,
            verify_pdo_payload,
            sizeof(verify_pdo_payload));
    }
    if (err == ESP_OK &&
        memcmp(
            verify_pdo_payload,
            &pdo_payload[1],
            sizeof(verify_pdo_payload)) != 0) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    if (err == ESP_OK) {
        err = laser_controller_board_stusb_send_soft_reset();
    }

    laser_controller_board_service_report_probe(
        LASER_CONTROLLER_MODULE_PD,
        true,
        err == ESP_OK);

    if (err == ESP_OK) {
        memset(&s_pd_snapshot, 0, sizeof(s_pd_snapshot));
    }

    return err;
}

esp_err_t laser_controller_board_burn_pd_nvm(
    const laser_controller_service_pd_profile_t *profiles,
    size_t profile_count)
{
    /*
     * 2026-04-16 HARD RULE: same authorization gate as configure_pd_debug.
     * NVM burn is the most destructive PD operation (changes startup
     * defaults), so this gate is doubly important.
     */
    if (!laser_controller_board_pd_write_authorized()) {
        return ESP_ERR_INVALID_STATE;
    }
    s_pd_write_authorization_until_ms = 0U;
    uint8_t current_map[LASER_CONTROLLER_STUSB_NVM_BANK_COUNT]
                       [LASER_CONTROLLER_STUSB_NVM_BANK_SIZE] = { { 0 } };
    uint8_t target_map[LASER_CONTROLLER_STUSB_NVM_BANK_COUNT]
                      [LASER_CONTROLLER_STUSB_NVM_BANK_SIZE] = { { 0 } };
    uint8_t verify_map[LASER_CONTROLLER_STUSB_NVM_BANK_COUNT]
                      [LASER_CONTROLLER_STUSB_NVM_BANK_SIZE] = { { 0 } };
    esp_err_t err;

    if (profiles == NULL ||
        profile_count != LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!laser_controller_service_module_write_enabled(LASER_CONTROLLER_MODULE_PD)) {
        return ESP_ERR_INVALID_STATE;
    }

    err = laser_controller_board_i2c_probe(LASER_CONTROLLER_STUSB4500_ADDR);
    if (err != ESP_OK) {
        laser_controller_board_service_report_probe(
            LASER_CONTROLLER_MODULE_PD,
            false,
            false);
        return err;
    }

    err = laser_controller_board_stusb_nvm_read_all(current_map);
    if (err != ESP_OK) {
        laser_controller_board_service_report_probe(
            LASER_CONTROLLER_MODULE_PD,
            true,
            false);
        return err;
    }

    if (!laser_controller_board_prepare_pd_nvm_map(
            profiles,
            profile_count,
            current_map,
            target_map)) {
        laser_controller_board_service_report_probe(
            LASER_CONTROLLER_MODULE_PD,
            true,
            false);
        return ESP_ERR_INVALID_ARG;
    }

    err = laser_controller_board_stusb_nvm_enter_write_mode(
        LASER_CONTROLLER_STUSB_NVM_ALL_BANKS_MASK);
    if (err != ESP_OK) {
        laser_controller_board_service_report_probe(
            LASER_CONTROLLER_MODULE_PD,
            true,
            false);
        return err;
    }

    for (uint8_t bank_index = 0U;
         err == ESP_OK && bank_index < LASER_CONTROLLER_STUSB_NVM_BANK_COUNT;
         ++bank_index) {
        err = laser_controller_board_stusb_nvm_write_bank(
            bank_index,
            target_map[bank_index]);
    }

    (void)laser_controller_board_stusb_nvm_exit_test_mode();

    if (err != ESP_OK) {
        laser_controller_board_service_report_probe(
            LASER_CONTROLLER_MODULE_PD,
            true,
            false);
        return err;
    }

    err = laser_controller_board_stusb_nvm_read_all(verify_map);
    if (err != ESP_OK) {
        laser_controller_board_service_report_probe(
            LASER_CONTROLLER_MODULE_PD,
            true,
            false);
        return err;
    }

    if (memcmp(target_map, verify_map, sizeof(target_map)) != 0) {
        laser_controller_board_service_report_probe(
            LASER_CONTROLLER_MODULE_PD,
            true,
            false);
        return ESP_ERR_INVALID_RESPONSE;
    }

    laser_controller_board_service_report_probe(
        LASER_CONTROLLER_MODULE_PD,
        true,
        true);
    return ESP_OK;
}

void laser_controller_board_force_pd_refresh(void)
{
    laser_controller_board_force_pd_refresh_with_source(
        LASER_CONTROLLER_PD_SNAPSHOT_SOURCE_INTEGRATE_REFRESH);
}

/*
 * Public PCN low-drive helper (2026-04-16 user directive). Stops the
 * LEDC PWM channel if active and drives the GPIO low. Used by the
 * deployment-checklist start path so the chip enters the sequence from
 * a clean LISL idle posture even if the runtime was modulating PCN
 * just before.
 */
void laser_controller_board_force_pcn_low(void)
{
    laser_controller_board_disable_pcn_pwm_and_drive(true);
}

void laser_controller_board_force_pd_refresh_with_source(
    laser_controller_pd_snapshot_source_t source)
{
    s_pd_refresh_allowed = true;
    s_pd_refresh_requested_source = source;
    s_pd_snapshot.updated_ms = 0U;
}

void laser_controller_board_reset_gpio_debug_state(void)
{
    bool preserve_i2c_pins = false;
    bool preserve_spi_pins = false;

    laser_controller_board_stop_tof_illumination_pwm();
    s_tof_sideband_mode = LASER_CONTROLLER_TOF_SIDEBAND_MODE_FLOAT;
    s_tof_sideband_applied_duty_cycle_pct = 0U;
    s_tof_sideband_applied_frequency_hz = 0U;
    /*
     * 2026-04-17 (audit round 2, S1 safety): drive rails to the safe
     * default BEFORE taking the bus mutex. The I2C/SPI teardown that
     * follows holds s_bus_mutex for 50-300 ms; during that window the
     * control task's 5 ms tick is starved and cannot react to a
     * TEC_PGOOD fall by pulling LD safe. That violates SOP rule #3
     * ("TEC loss = immediate LD shutdown"). Driving safe rails here
     * first guarantees the rails are already OFF before the stall
     * window begins — if TEC goes down during the teardown there is
     * no LD to shut down. kSafeOutputs is the fail-safe posture:
     * TEC_EN=0, LD_EN=0, SBDN=OFF, PCN=low, green/red/LEDs off.
     * The post-teardown restore (below) still uses s_last_outputs,
     * so non-service-mode callers see no visible rail interruption
     * because s_last_outputs already matches the safe posture in
     * normal flow.
     */
    laser_controller_board_drive_safe_gpio_levels(&kSafeOutputs);
    laser_controller_board_lock_bus();

    if (s_i2c_ready && s_i2c_bus != NULL) {
        esp_err_t detach_err = ESP_OK;

        (void)i2c_master_bus_wait_all_done(
            s_i2c_bus,
            LASER_CONTROLLER_I2C_TIMEOUT_MS);
        (void)i2c_master_bus_reset(s_i2c_bus);
        detach_err = i2c_del_master_bus(s_i2c_bus);
        if (detach_err == ESP_OK) {
            s_i2c_bus = NULL;
            s_i2c_ready = false;
        } else {
            preserve_i2c_pins = true;
        }
    }

    if (s_spi_ready) {
        esp_err_t free_err = ESP_OK;

        if (s_imu_spi != NULL) {
            (void)spi_bus_remove_device(s_imu_spi);
            s_imu_spi = NULL;
        }
        free_err = spi_bus_free(LASER_CONTROLLER_SPI_HOST);
        if (free_err == ESP_OK) {
            s_spi_ready = false;
        } else {
            preserve_spi_pins = true;
        }
    }

    for (size_t index = 0U;
         index < LASER_CONTROLLER_BOARD_GPIO_INSPECTOR_PIN_COUNT;
         ++index) {
        const uint32_t gpio_num = kGpioInspectorPins[index].gpio_num;

        if (laser_controller_board_is_transport_or_strap_gpio(gpio_num) ||
            laser_controller_board_is_adc_input_gpio(gpio_num) ||
            (preserve_i2c_pins &&
             laser_controller_board_is_shared_i2c_gpio(gpio_num)) ||
            (preserve_spi_pins &&
             laser_controller_board_is_imu_spi_gpio(gpio_num))) {
            continue;
        }

        (void)gpio_reset_pin((gpio_num_t)gpio_num);
    }

    s_gpio_ready = false;
    (void)laser_controller_board_ensure_gpio_ready();
    if (!preserve_i2c_pins) {
        (void)laser_controller_board_ensure_i2c_ready();
    }
    if (!preserve_spi_pins) {
        (void)laser_controller_board_ensure_spi_ready();
    }
    laser_controller_board_unlock_bus();
    laser_controller_board_drive_safe_gpio_levels(&s_last_outputs);
    laser_controller_board_apply_gpio_overrides();
    laser_controller_board_apply_tof_sideband_state(
        laser_controller_board_any_owns_tof_sideband());
    laser_controller_board_capture_gpio_inspector();
}

esp_err_t laser_controller_board_imu_spi_read(uint8_t reg, uint8_t *value)
{
    return laser_controller_board_imu_transfer(reg, value, false);
}

esp_err_t laser_controller_board_imu_spi_write(uint8_t reg, uint8_t value)
{
    return laser_controller_board_imu_transfer(reg, &value, true);
}

esp_err_t laser_controller_board_configure_dac_debug(
    laser_controller_service_dac_reference_t reference,
    bool gain_2x,
    bool ref_div,
    laser_controller_service_dac_sync_t sync_mode)
{
    uint16_t sync_reg = LASER_CONTROLLER_DAC_SYNC_DEFAULT;
    uint16_t config_reg = LASER_CONTROLLER_DAC_CONFIG_DEFAULT;
    uint16_t gain_reg = 0x0000U;
    esp_err_t err;

    if (!laser_controller_service_module_write_enabled(LASER_CONTROLLER_MODULE_DAC)) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!laser_controller_board_dac_config_is_safe(reference, gain_2x, ref_div)) {
        laser_controller_board_service_report_probe(
            LASER_CONTROLLER_MODULE_DAC,
            true,
            false);
        return ESP_ERR_INVALID_ARG;
    }

    err = laser_controller_board_dac_init_safe();
    if (err != ESP_OK) {
        return err;
    }

    if (sync_mode == LASER_CONTROLLER_SERVICE_DAC_SYNC_SYNC) {
        sync_reg = 0x0303U;
    }
    if (reference == LASER_CONTROLLER_SERVICE_DAC_REFERENCE_EXTERNAL) {
        config_reg |= 0x0001U;
    }
    if (gain_2x) {
        gain_reg |= 0x0003U;
    }
    if (ref_div) {
        gain_reg |= 0x0100U;
    }

    err = laser_controller_board_dac_write_command(
        LASER_CONTROLLER_DAC_SYNC_REG,
        sync_reg);
    if (err == ESP_OK) {
        err = laser_controller_board_dac_write_command(
            LASER_CONTROLLER_DAC_CONFIG_REG,
            config_reg);
    }
    if (err == ESP_OK) {
        err = laser_controller_board_dac_write_command(
            LASER_CONTROLLER_DAC_GAIN_REG,
            gain_reg);
    }
    if (err == ESP_OK) {
        err = laser_controller_board_dac_check_status();
    }

    laser_controller_board_capture_dac_readback(err == ESP_OK);

    laser_controller_board_service_report_probe(
        LASER_CONTROLLER_MODULE_DAC,
        err == ESP_OK,
        err == ESP_OK);
    return err;
}

esp_err_t laser_controller_board_set_dac_debug_output(
    bool tec_channel,
    float voltage_v)
{
    const uint8_t reg =
        tec_channel ? LASER_CONTROLLER_DAC_DATA_B_REG :
                      LASER_CONTROLLER_DAC_DATA_A_REG;
    const float clamped_v =
        laser_controller_board_clamp_float(
            voltage_v,
            0.0f,
            LASER_CONTROLLER_DAC_FULL_SCALE_V);
    esp_err_t err;

    if (!laser_controller_service_module_write_enabled(LASER_CONTROLLER_MODULE_DAC)) {
        return ESP_ERR_INVALID_STATE;
    }

    err = laser_controller_board_dac_init_safe();
    if (err != ESP_OK) {
        return err;
    }

    err = laser_controller_board_dac_write_command(
            reg,
            laser_controller_board_voltage_to_dac_code(clamped_v));
    if (err != ESP_OK) {
        laser_controller_board_service_report_probe(
            LASER_CONTROLLER_MODULE_DAC,
            true,
            false);
        return err;
    }

    if (tec_channel) {
        s_last_tec_voltage_v = clamped_v;
    } else {
        s_last_ld_voltage_v = clamped_v;
    }

    laser_controller_board_capture_dac_readback(true);

    laser_controller_board_service_report_probe(
        LASER_CONTROLLER_MODULE_DAC,
        true,
        true);
    return ESP_OK;
}

esp_err_t laser_controller_board_configure_imu_runtime(
    uint32_t odr_hz,
    uint32_t accel_range_g,
    uint32_t gyro_range_dps,
    bool gyro_enabled,
    bool lpf2_enabled,
    bool timestamp_enabled,
    bool bdu_enabled,
    bool if_inc_enabled,
    bool i2c_disabled)
{
    s_imu_runtime.config.odr_hz = odr_hz;
    s_imu_runtime.config.accel_range_g = accel_range_g;
    s_imu_runtime.config.gyro_range_dps = gyro_range_dps;
    s_imu_runtime.config.gyro_enabled = gyro_enabled;
    s_imu_runtime.config.lpf2_enabled = lpf2_enabled;
    s_imu_runtime.config.timestamp_enabled = timestamp_enabled;
    s_imu_runtime.config.bdu_enabled = bdu_enabled;
    s_imu_runtime.config.if_inc_enabled = if_inc_enabled;
    s_imu_runtime.config.i2c_disabled = i2c_disabled;
    s_imu_runtime.configured = false;
    s_imu_runtime.identified = false;
    s_imu_runtime.last_error = ESP_OK;
    s_imu_runtime.last_attempt_ms = 0U;
    s_imu_runtime.last_sample_ms = 0U;
    laser_controller_board_clear_imu_readback();

    if (!laser_controller_service_module_write_enabled(LASER_CONTROLLER_MODULE_IMU)) {
        return ESP_ERR_INVALID_STATE;
    }

    return laser_controller_board_ensure_imu_runtime_ready();
}

esp_err_t laser_controller_board_configure_haptic_debug(
    uint32_t effect_id,
    laser_controller_service_haptic_mode_t mode,
    uint32_t library,
    laser_controller_service_haptic_actuator_t actuator,
    uint32_t rtp_level)
{
    esp_err_t err;
    const uint8_t mode_reg = laser_controller_board_haptic_mode_value(mode);
    const uint8_t feedback_reg =
        actuator == LASER_CONTROLLER_SERVICE_HAPTIC_ACTUATOR_LRA ? 0x80U : 0x00U;

    if (!laser_controller_service_module_write_enabled(LASER_CONTROLLER_MODULE_HAPTIC)) {
        return ESP_ERR_INVALID_STATE;
    }

    laser_controller_board_clear_haptic_readback();
    err = laser_controller_board_i2c_probe(LASER_CONTROLLER_DRV2605_ADDR);
    if (err != ESP_OK) {
        s_haptic_readback.last_error = (int32_t)err;
        laser_controller_board_service_report_probe(
            LASER_CONTROLLER_MODULE_HAPTIC,
            false,
            false);
        return err;
    }
    s_haptic_readback.reachable = true;

    err = laser_controller_board_drv2605_write_reg(
        LASER_CONTROLLER_DRV2605_MODE_REG,
        LASER_CONTROLLER_DRV2605_RESET_BIT | LASER_CONTROLLER_DRV2605_STANDBY_BIT);
    if (err == ESP_OK) {
        esp_rom_delay_us(1000U);
        err = laser_controller_board_drv2605_write_reg(
            LASER_CONTROLLER_DRV2605_FEEDBACK_REG,
            feedback_reg);
    }
    if (err == ESP_OK) {
        err = laser_controller_board_drv2605_write_reg(
            LASER_CONTROLLER_DRV2605_LIBRARY_REG,
            (uint8_t)(library & 0x07U));
    }
    if (err == ESP_OK) {
        err = laser_controller_board_drv2605_write_reg(
            LASER_CONTROLLER_DRV2605_RTP_REG,
            (uint8_t)(rtp_level & 0xFFU));
    }
    if (err == ESP_OK) {
        err = laser_controller_board_drv2605_write_reg(
            LASER_CONTROLLER_DRV2605_WAVESEQ1_REG,
            (uint8_t)(effect_id & 0x7FU));
    }
    if (err == ESP_OK) {
        err = laser_controller_board_drv2605_write_reg(
            LASER_CONTROLLER_DRV2605_MODE_REG,
            mode_reg);
    }

    if (err == ESP_OK) {
        laser_controller_board_capture_haptic_readback();
    } else {
        s_haptic_readback.last_error = (int32_t)err;
    }
    laser_controller_board_service_report_probe(
        LASER_CONTROLLER_MODULE_HAPTIC,
        err == ESP_OK,
        err == ESP_OK);
    return err;
}

esp_err_t laser_controller_board_set_tof_illumination(
    bool enabled,
    uint32_t duty_cycle_pct,
    uint32_t frequency_hz)
{
    if (enabled &&
        !laser_controller_service_module_write_enabled(LASER_CONTROLLER_MODULE_TOF)) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Hard safety cap — clamp BEFORE storing / applying. See
     * s_tof_led_max_duty_pct commentary. */
    const uint32_t capped_duty =
        duty_cycle_pct > s_tof_led_max_duty_pct
            ? s_tof_led_max_duty_pct
            : duty_cycle_pct;
    s_tof_illumination.enabled = enabled && capped_duty > 0U;
    s_tof_illumination.duty_cycle_pct =
        laser_controller_board_clamp_u32(capped_duty, 0U, 100U);
    s_tof_illumination.frequency_hz = laser_controller_board_clamp_u32(
        frequency_hz > 0U ? frequency_hz : LASER_CONTROLLER_TOF_LED_PWM_DEFAULT_FREQ_HZ,
        LASER_CONTROLLER_TOF_LED_PWM_MIN_FREQ_HZ,
        LASER_CONTROLLER_TOF_LED_PWM_MAX_FREQ_HZ);
    s_tof_runtime_owns_sideband = false;

    return laser_controller_board_apply_tof_sideband_state_locked(
        laser_controller_board_service_owns_tof_sideband());
}

esp_err_t laser_controller_board_set_runtime_tof_illumination(
    bool enabled,
    uint32_t duty_cycle_pct,
    uint32_t frequency_hz)
{
    /*
     * GPIO6 Dual-Driver Rule (AGENT.md): the runtime path MUST bail out
     * when service mode owns the sideband. Without this guard, the control
     * task's 5ms tick overwrites s_tof_illumination every cycle and stomps
     * the service's desired state — producing the "flash on, then stuck
     * off" behavior observed on the Integrate page.
     */
    if (laser_controller_board_service_owns_tof_sideband()) {
        s_tof_runtime_owns_sideband = false;
        return ESP_OK;
    }

    /* Hard safety cap — same clamp as the service path. A runtime caller
     * requesting duty above the cap is silently reduced to the cap. */
    const uint32_t capped_duty =
        duty_cycle_pct > s_tof_led_max_duty_pct
            ? s_tof_led_max_duty_pct
            : duty_cycle_pct;
    s_tof_illumination.enabled = enabled && capped_duty > 0U;
    s_tof_illumination.duty_cycle_pct =
        laser_controller_board_clamp_u32(capped_duty, 0U, 100U);
    s_tof_illumination.frequency_hz = laser_controller_board_clamp_u32(
        frequency_hz > 0U ? frequency_hz : LASER_CONTROLLER_TOF_LED_PWM_DEFAULT_FREQ_HZ,
        LASER_CONTROLLER_TOF_LED_PWM_MIN_FREQ_HZ,
        LASER_CONTROLLER_TOF_LED_PWM_MAX_FREQ_HZ);
    s_tof_runtime_owns_sideband = s_tof_illumination.enabled;

    return laser_controller_board_apply_tof_sideband_state_locked(
        laser_controller_board_any_owns_tof_sideband());
}

void laser_controller_board_set_tof_led_max_duty_pct(uint32_t cap)
{
    s_tof_led_max_duty_pct =
        laser_controller_board_clamp_u32(cap, 0U, 100U);
}

uint32_t laser_controller_board_get_tof_led_max_duty_pct(void)
{
    return s_tof_led_max_duty_pct;
}

esp_err_t laser_controller_board_fire_haptic_test(void)
{
    laser_controller_service_haptic_config_t haptic_config;
    esp_err_t err;

    if (!laser_controller_service_module_write_enabled(LASER_CONTROLLER_MODULE_HAPTIC)) {
        return ESP_ERR_INVALID_STATE;
    }

    if (gpio_get_level(LASER_CONTROLLER_GPIO_ERM_EN) == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    laser_controller_service_get_haptic_config(&haptic_config);
    err = laser_controller_board_configure_haptic_debug(
        haptic_config.effect_id,
        haptic_config.mode,
        haptic_config.library,
        haptic_config.actuator,
        haptic_config.rtp_level);
    if (err != ESP_OK) {
        return err;
    }

    return laser_controller_board_drv2605_write_reg(
        LASER_CONTROLLER_DRV2605_GO_REG,
        0x01U);
}

void laser_controller_board_apply_actuator_targets(
    const laser_controller_config_t *config,
    laser_controller_amps_t high_state_current_a,
    laser_controller_celsius_t target_temp_c,
    bool modulation_enabled,
    uint32_t modulation_frequency_hz,
    uint32_t modulation_duty_cycle_pct,
    bool nir_output_enable,
    bool select_driver_low_current)
{
    laser_controller_service_dac_runtime_t dac_runtime;
    laser_controller_service_gpio_override_t pcn_override = { 0 };
    const float ld_volts_per_amp =
            config != NULL && config->analog.ld_command_volts_per_amp > 0.0f ?
            config->analog.ld_command_volts_per_amp :
            (LASER_CONTROLLER_DAC_FULL_SCALE_V / 6.0f);
    const float tec_volts_per_c =
        config != NULL && config->analog.tec_command_volts_per_c > 0.0f ?
            config->analog.tec_command_volts_per_c :
            (2.5f / 65.0f);
    float ld_voltage_v;
    float tec_voltage_v;

    laser_controller_service_get_dac_runtime(&dac_runtime);
    if (dac_runtime.service_mode_requested) {
        ld_voltage_v = laser_controller_board_clamp_float(
            dac_runtime.dac_ld_channel_v,
            0.0f,
            LASER_CONTROLLER_DAC_FULL_SCALE_V);
        tec_voltage_v = laser_controller_board_clamp_float(
            dac_runtime.dac_tec_channel_v,
            0.0f,
            2.5f);
    } else {
        ld_voltage_v = laser_controller_board_clamp_float(
            high_state_current_a * ld_volts_per_amp,
            0.0f,
            LASER_CONTROLLER_DAC_FULL_SCALE_V);
        tec_voltage_v = laser_controller_board_clamp_float(
            target_temp_c * tec_volts_per_c,
            0.0f,
            2.5f);
    }

    if (laser_controller_service_module_expected(LASER_CONTROLLER_MODULE_DAC) &&
        laser_controller_board_dac_init_safe() == ESP_OK) {
        if (fabsf(ld_voltage_v - s_last_ld_voltage_v) > 0.002f) {
            (void)laser_controller_board_dac_write_command(
                LASER_CONTROLLER_DAC_DATA_A_REG,
                laser_controller_board_voltage_to_dac_code(ld_voltage_v));
            s_last_ld_voltage_v = ld_voltage_v;
        }
        if (fabsf(tec_voltage_v - s_last_tec_voltage_v) > 0.002f) {
            (void)laser_controller_board_dac_write_command(
                LASER_CONTROLLER_DAC_DATA_B_REG,
                laser_controller_board_voltage_to_dac_code(tec_voltage_v));
            s_last_tec_voltage_v = tec_voltage_v;
        }
    }

    if (laser_controller_service_get_gpio_override(
            LASER_CONTROLLER_GPIO_LD_PCN,
            &pcn_override) &&
        pcn_override.active) {
        laser_controller_board_release_pcn_pwm_if_needed(
            LASER_CONTROLLER_GPIO_LD_PCN);
        laser_controller_board_apply_gpio_overrides();
        return;
    }

    if (nir_output_enable && modulation_enabled) {
        if (modulation_frequency_hz == 0U) {
            laser_controller_board_disable_pcn_pwm_and_drive(
                modulation_duty_cycle_pct < 50U);
            return;
        }

        ledc_timer_config_t timer_config = {
            .speed_mode = LASER_CONTROLLER_PCN_PWM_SPEED_MODE,
            .duty_resolution = LASER_CONTROLLER_PCN_PWM_RESOLUTION,
            .timer_num = LASER_CONTROLLER_PCN_PWM_TIMER,
            .freq_hz = laser_controller_board_clamp_u32(
                modulation_frequency_hz,
                1U,
                4000U),
            .clk_cfg = LEDC_AUTO_CLK,
            .deconfigure = false,
        };
        ledc_channel_config_t channel_config = {
            .gpio_num = LASER_CONTROLLER_GPIO_LD_PCN,
            .speed_mode = LASER_CONTROLLER_PCN_PWM_SPEED_MODE,
            .channel = LASER_CONTROLLER_PCN_PWM_CHANNEL,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = LASER_CONTROLLER_PCN_PWM_TIMER,
            .duty = (laser_controller_board_clamp_u32(
                        modulation_duty_cycle_pct,
                        0U,
                        100U) *
                     LASER_CONTROLLER_PCN_PWM_DUTY_MAX) /
                    100U,
            .hpoint = 0,
            .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
            .flags = { .output_invert = 0 },
            .deconfigure = false,
        };

        if (ledc_timer_config(&timer_config) == ESP_OK &&
            ledc_channel_config(&channel_config) == ESP_OK) {
            s_pwm_active = true;
            return;
        }
    }

    laser_controller_board_disable_pcn_pwm_and_drive(select_driver_low_current);
}

void laser_controller_board_inject_mock_inputs(const laser_controller_board_inputs_t *inputs)
{
    if (inputs == NULL) {
        s_mock_override_enabled = false;
        memset(&s_mock_inputs, 0, sizeof(s_mock_inputs));
        return;
    }

    s_mock_inputs = *inputs;
    s_mock_override_enabled = true;
}
