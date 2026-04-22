#include "laser_controller_app.h"

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "laser_controller_faults.h"
#include "laser_controller_board.h"
#include "laser_controller_logger.h"
#include "laser_controller_signature.h"
#include "laser_controller_state.h"
#include "laser_controller_usb_debug_mock.h"
#include "laser_controller_wireless.h"

#define LASER_CONTROLLER_COMMS_TX_STACK_BYTES 6144U
#define LASER_CONTROLLER_COMMS_RX_STACK_BYTES 8192U
#define LASER_CONTROLLER_COMMS_TX_PRIORITY    4U
#define LASER_CONTROLLER_COMMS_RX_PRIORITY    3U
#define LASER_CONTROLLER_COMMS_CMD_PRIORITY   3U
#define LASER_CONTROLLER_COMMS_TX_PERIOD_MS   20U
#define LASER_CONTROLLER_COMMS_FAST_TELEMETRY_PERIOD_MS 60U
#define LASER_CONTROLLER_COMMS_WIRELESS_FAST_TELEMETRY_PERIOD_MS 180U
#define LASER_CONTROLLER_COMMS_LIVE_TELEMETRY_PERIOD_MS 1000U
#define LASER_CONTROLLER_COMMS_STATUS_PERIOD_MS    5000U
/*
 * Tightened 400 -> 80 (2026-04-17 user directive): the 400 ms window
 * starves fast_telemetry on a Wi-Fi session where the host is even
 * lightly active — one GUI-driven command per second was enough to
 * permanently suppress telemetry. 80 ms still lets a command response
 * head out ahead of the next fast tick on a busy link without crushing
 * perceived refresh rate. If the Wi-Fi stack needs more recovery time
 * under congestion, the httpd_queue_work backpressure path already
 * drops frames gracefully.
 */
#define LASER_CONTROLLER_COMMS_WIRELESS_POST_COMMAND_QUIET_MS 80U
#define LASER_CONTROLLER_COMMS_MAX_LINE_LEN   768U
#define LASER_CONTROLLER_COMMS_FRAME_BUFFER_BYTES 32768U
#define LASER_CONTROLLER_COMMS_QUEUE_DEPTH    8U
#define LASER_CONTROLLER_DEG_PER_RAD          (57.2957795130823208768f)
#define LASER_CONTROLLER_RAD_PER_DEG          (0.01745329251994329577f)
#define LASER_CONTROLLER_BENCH_MIN_TEMP_C     5.0f
#define LASER_CONTROLLER_BENCH_MAX_TEMP_C     65.0f
#define LASER_CONTROLLER_BENCH_MIN_LAMBDA_NM  771.2f
#define LASER_CONTROLLER_BENCH_MAX_LAMBDA_NM  790.0f
#define LASER_CONTROLLER_PD_REFRESH_WAIT_MS    40U
#define LASER_CONTROLLER_PD_RENEGOTIATE_WAIT_MS 650U
#define LASER_CONTROLLER_SERVICE_MODE_WAIT_MS  1200U
#define LASER_CONTROLLER_SERVICE_MODE_POLL_MS  20U
#define LASER_CONTROLLER_SUPPLY_ENABLE_WAIT_MS 4000U
#define LASER_CONTROLLER_SUPPLY_DISABLE_WAIT_MS 400U
#define LASER_CONTROLLER_HAPTIC_GPIO_WAIT_MS   400U
#define LASER_CONTROLLER_GPIO_OVERRIDE_WAIT_MS 400U
#define LASER_CONTROLLER_STUSB4500_ADDR        0x28U
#define LASER_CONTROLLER_DAC80502_ADDR         0x48U
#define LASER_CONTROLLER_DRV2605_ADDR          0x5AU

static StaticTask_t s_tx_task_tcb;
static StackType_t s_tx_task_stack[LASER_CONTROLLER_COMMS_TX_STACK_BYTES];
static StaticTask_t s_cmd_task_tcb;
static StackType_t s_cmd_task_stack[LASER_CONTROLLER_COMMS_RX_STACK_BYTES];
#if !CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
static StaticTask_t s_rx_task_tcb;
static StackType_t s_rx_task_stack[LASER_CONTROLLER_COMMS_RX_STACK_BYTES];
#endif
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED
static StaticTask_t s_usb_rx_task_tcb;
static StackType_t s_usb_rx_task_stack[LASER_CONTROLLER_COMMS_RX_STACK_BYTES];
#endif
static laser_controller_log_entry_t s_tx_log_entries[LASER_CONTROLLER_LOG_ENTRY_COUNT];
static char s_frame_buffer[LASER_CONTROLLER_COMMS_FRAME_BUFFER_BYTES];
static bool s_comms_started;
static StaticSemaphore_t s_output_lock_buffer;
static SemaphoreHandle_t s_output_lock;
typedef struct {
    char line[LASER_CONTROLLER_COMMS_MAX_LINE_LEN];
} laser_controller_comms_line_t;
static StaticQueue_t s_command_queue_buffer;
static uint8_t s_command_queue_storage[
    LASER_CONTROLLER_COMMS_QUEUE_DEPTH * sizeof(laser_controller_comms_line_t)];
static QueueHandle_t s_command_queue;
static volatile bool s_command_in_flight;
static volatile laser_controller_time_ms_t s_wireless_telemetry_quiet_until_ms;

typedef struct {
    char *data;
    size_t capacity;
    size_t length;
    bool truncated;
} laser_controller_comms_buffer_t;

typedef enum {
    LASER_CONTROLLER_COMMS_PERIPHERAL_NONE = 0,
    LASER_CONTROLLER_COMMS_PERIPHERAL_DAC = (1U << 0),
    LASER_CONTROLLER_COMMS_PERIPHERAL_PD = (1U << 1),
    LASER_CONTROLLER_COMMS_PERIPHERAL_IMU = (1U << 2),
    LASER_CONTROLLER_COMMS_PERIPHERAL_HAPTIC = (1U << 3),
    LASER_CONTROLLER_COMMS_PERIPHERAL_TOF = (1U << 4),
} laser_controller_comms_peripheral_mask_t;

static bool laser_controller_comms_extract_float(
    const char *line,
    const char *key,
    float *value);
static bool laser_controller_comms_extract_bool(
    const char *line,
    const char *key,
    bool *value);
static bool laser_controller_comms_extract_root_uint(
    const char *line,
    const char *key,
    uint32_t *value);
static bool laser_controller_comms_extract_root_string(
    const char *line,
    const char *key,
    char *value,
    size_t value_len);
static const laser_controller_board_gpio_pin_readback_t *
laser_controller_comms_find_gpio_pin(
    const laser_controller_runtime_status_t *status,
    uint32_t gpio_num);
static bool laser_controller_comms_driver_standby_effective(
    const laser_controller_runtime_status_t *status);
static const char *laser_controller_comms_runtime_mode_switch_block_reason(
    const laser_controller_runtime_status_t *status);

static void laser_controller_comms_output_lock(void)
{
    if (s_output_lock != NULL) {
        (void)xSemaphoreTake(s_output_lock, portMAX_DELAY);
    }
}

static void laser_controller_comms_output_unlock(void)
{
    if (s_output_lock != NULL) {
        (void)xSemaphoreGive(s_output_lock);
    }
}

static void laser_controller_comms_buffer_reset(
    laser_controller_comms_buffer_t *buffer,
    char *storage,
    size_t storage_len)
{
    if (buffer == NULL || storage == NULL || storage_len == 0U) {
        return;
    }

    buffer->data = storage;
    buffer->capacity = storage_len;
    buffer->length = 0U;
    buffer->truncated = false;
    buffer->data[0] = '\0';
}

static void laser_controller_comms_buffer_append_raw(
    laser_controller_comms_buffer_t *buffer,
    const char *text)
{
    size_t remaining = 0U;
    size_t text_len = 0U;

    if (buffer == NULL || buffer->data == NULL || text == NULL) {
        return;
    }

    if (buffer->length >= (buffer->capacity - 1U)) {
        buffer->truncated = true;
        return;
    }

    remaining = buffer->capacity - buffer->length;
    text_len = strnlen(text, remaining);
    if (text_len >= remaining) {
        buffer->truncated = true;
        text_len = remaining - 1U;
    }

    memcpy(&buffer->data[buffer->length], text, text_len);
    buffer->length += text_len;
    buffer->data[buffer->length] = '\0';
}

static void laser_controller_comms_buffer_append_fmt(
    laser_controller_comms_buffer_t *buffer,
    const char *format,
    ...)
{
    va_list args;
    int written = 0;

    if (buffer == NULL || buffer->data == NULL || format == NULL) {
        return;
    }

    if (buffer->length >= (buffer->capacity - 1U)) {
        buffer->truncated = true;
        return;
    }

    va_start(args, format);
    written = vsnprintf(
        &buffer->data[buffer->length],
        buffer->capacity - buffer->length,
        format,
        args);
    va_end(args);

    if (written < 0) {
        buffer->truncated = true;
        return;
    }

    if ((size_t)written >= (buffer->capacity - buffer->length)) {
        buffer->length = buffer->capacity - 1U;
        buffer->data[buffer->length] = '\0';
        buffer->truncated = true;
        return;
    }

    buffer->length += (size_t)written;
}

static void laser_controller_comms_emit_text_locked(const char *text)
{
    if (text == NULL || text[0] == '\0') {
        return;
    }

    fputs(text, stdout);
    laser_controller_wireless_broadcast_text(text);
}

static bool laser_controller_comms_should_pause_wireless_telemetry(
    laser_controller_time_ms_t now_ms)
{
    return laser_controller_wireless_has_clients() &&
           (s_command_in_flight ||
            now_ms < s_wireless_telemetry_quiet_until_ms);
}

static void laser_controller_comms_emit_buffer_locked(
    const laser_controller_comms_buffer_t *buffer,
    const char *frame_name)
{
    char warning[256];
    int written = 0;

    if (buffer == NULL) {
        return;
    }

    if (!buffer->truncated) {
        laser_controller_comms_emit_text_locked(buffer->data);
        return;
    }

    written = snprintf(
        warning,
        sizeof(warning),
        "{\"type\":\"event\",\"event\":\"protocol_warning\",\"timestamp_ms\":0,"
        "\"detail\":\"%s frame exceeded %u-byte host buffer and was dropped.\","
        "\"payload\":{\"category\":\"protocol\",\"message\":\"%s frame exceeded %u-byte host buffer and was dropped.\"}}\n",
        frame_name != NULL ? frame_name : "protocol",
        (unsigned)LASER_CONTROLLER_COMMS_FRAME_BUFFER_BYTES,
        frame_name != NULL ? frame_name : "protocol",
        (unsigned)LASER_CONTROLLER_COMMS_FRAME_BUFFER_BYTES);
    if (written <= 0 || (size_t)written >= sizeof(warning)) {
        return;
    }

    laser_controller_comms_emit_text_locked(warning);
}

static bool laser_controller_comms_config_has_valid_lut(
    const laser_controller_config_t *config)
{
    return config != NULL && config->wavelength_lut.point_count >= 2U;
}

static laser_controller_nm_t laser_controller_comms_lambda_from_temp(
    const laser_controller_config_t *config,
    laser_controller_celsius_t temp_c)
{
    if (laser_controller_comms_config_has_valid_lut(config)) {
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

static laser_controller_nm_t laser_controller_comms_actual_lambda_nm(
    const laser_controller_runtime_status_t *status)
{
    if (status == NULL) {
        return 0.0f;
    }

    if (!status->inputs.tec_telemetry_valid) {
        return status->bench.target_lambda_nm;
    }

    return laser_controller_comms_lambda_from_temp(
        &status->config,
        status->inputs.tec_temp_c);
}

static const char *laser_controller_comms_pd_snapshot_source_name(
    laser_controller_pd_snapshot_source_t source)
{
    switch (source) {
        case LASER_CONTROLLER_PD_SNAPSHOT_SOURCE_BOOT_RECONCILE:
            return "boot_reconcile";
        case LASER_CONTROLLER_PD_SNAPSHOT_SOURCE_INTEGRATE_REFRESH:
            return "integrate_refresh";
        case LASER_CONTROLLER_PD_SNAPSHOT_SOURCE_CACHED:
            return "cached";
        case LASER_CONTROLLER_PD_SNAPSHOT_SOURCE_NONE:
        default:
            return "none";
    }
}

static float laser_controller_comms_lambda_drift_nm(
    const laser_controller_runtime_status_t *status)
{
    const float actual_lambda_nm =
        laser_controller_comms_actual_lambda_nm(status);

    if (status == NULL || !status->inputs.tec_telemetry_valid) {
        return 0.0f;
    }

    return fabsf(actual_lambda_nm - status->bench.target_lambda_nm);
}

static laser_controller_amps_t laser_controller_comms_effective_commanded_current_a(
    const laser_controller_runtime_status_t *status)
{
    laser_controller_amps_t commanded_current_a = 0.0f;

    if (status == NULL || !status->decision.nir_output_enable) {
        return 0.0f;
    }

    commanded_current_a = status->bench.high_state_current_a;
    if (status->deployment.active &&
        status->deployment.max_laser_current_a > 0.0f &&
        commanded_current_a > status->deployment.max_laser_current_a) {
        commanded_current_a = status->deployment.max_laser_current_a;
    }

    return commanded_current_a;
}

static laser_controller_volts_t laser_controller_comms_effective_ld_command_voltage_v(
    const laser_controller_runtime_status_t *status)
{
    const float volts_per_amp =
        status != NULL && status->config.analog.ld_command_volts_per_amp > 0.0f ?
            status->config.analog.ld_command_volts_per_amp :
            (2.5f / 6.0f);

    if (status == NULL) {
        return 0.0f;
    }

    return laser_controller_comms_effective_commanded_current_a(status) * volts_per_amp;
}

static const char *laser_controller_comms_power_tier_name(
    laser_controller_power_tier_t power_tier)
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

static const char *laser_controller_comms_led_owner_name(
    const laser_controller_runtime_status_t *status)
{
    const laser_controller_board_gpio_pin_readback_t *pin = NULL;

    if (status == NULL) {
        return "none";
    }

    if (status->bringup.service_mode_requested &&
        status->bringup.tof_illumination_enabled) {
        return "integrate_service";
    }

    if (status->bench.illumination_enabled) {
        return "operate_runtime";
    }

    /*
     * Button-trigger policy owns the GPIO6 LED when binary_trigger +
     * deployment ready_idle + stage1 pressed (computed in app.c
     * apply_button_board_policy). Reported here so the host can pre-
     * disable the operate LED slider when buttons are driving the LED.
     * Set 2026-04-15.
     */
    if (status->button_runtime.led_owned) {
        return "button_trigger";
    }

    pin = laser_controller_comms_find_gpio_pin(
        status,
        LASER_CONTROLLER_GPIO_TOF_LED_CTRL);
    if (status->deployment.active &&
        pin != NULL &&
        !pin->level_high) {
        return "deployment";
    }

    return "none";
}

static bool laser_controller_comms_gpio_pin_high(
    const laser_controller_runtime_status_t *status,
    uint32_t gpio_num)
{
    const laser_controller_board_gpio_pin_readback_t *pin =
        laser_controller_comms_find_gpio_pin(status, gpio_num);

    return pin != NULL && pin->level_high;
}

static const char *laser_controller_comms_reset_reason_name(esp_reset_reason_t reason)
{
    switch (reason) {
        case ESP_RST_UNKNOWN:
            return "unknown";
        case ESP_RST_POWERON:
            return "power_on_reset";
        case ESP_RST_EXT:
            return "external_reset";
        case ESP_RST_SW:
            return "software_reset";
        case ESP_RST_PANIC:
            return "panic_reset";
        case ESP_RST_INT_WDT:
            return "interrupt_watchdog";
        case ESP_RST_TASK_WDT:
            return "task_watchdog";
        case ESP_RST_WDT:
            return "other_watchdog";
        case ESP_RST_DEEPSLEEP:
            return "deepsleep_reset";
        case ESP_RST_BROWNOUT:
            return "brownout_reset";
        case ESP_RST_SDIO:
            return "sdio_reset";
        default:
            return "unhandled_reset";
    }
}

static void laser_controller_comms_write_escaped_string(
    laser_controller_comms_buffer_t *buffer,
    const char *value)
{
    const char *text = value != NULL ? value : "";

    laser_controller_comms_buffer_append_raw(buffer, "\"");
    while (*text != '\0') {
        const char c = *text++;

        switch (c) {
            case '\"':
                laser_controller_comms_buffer_append_raw(buffer, "\\\"");
                break;
            case '\\':
                laser_controller_comms_buffer_append_raw(buffer, "\\\\");
                break;
            case '\r':
                laser_controller_comms_buffer_append_raw(buffer, "\\r");
                break;
            case '\n':
                laser_controller_comms_buffer_append_raw(buffer, "\\n");
                break;
            case '\t':
                laser_controller_comms_buffer_append_raw(buffer, "\\t");
                break;
            default:
                if ((unsigned char)c < 0x20U) {
                    laser_controller_comms_buffer_append_raw(buffer, "?");
                } else {
                    char raw[2] = { c, '\0' };
                    laser_controller_comms_buffer_append_raw(buffer, raw);
                }
                break;
        }
    }
    laser_controller_comms_buffer_append_raw(buffer, "\"");
}

static int32_t laser_controller_comms_encode_centi(float value)
{
    return (int32_t)lroundf(value * 100.0f);
}

static uint32_t laser_controller_comms_encode_imu_flags(
    const laser_controller_runtime_status_t *status)
{
    uint32_t flags = 0U;

    if (status == NULL) {
        return flags;
    }

    if (status->inputs.imu_data_valid) {
        flags |= (1U << 0);
    }
    if (status->inputs.imu_data_fresh) {
        flags |= (1U << 1);
    }
    flags |= (1U << 2);

    return flags;
}

static uint32_t laser_controller_comms_encode_tof_flags(
    const laser_controller_runtime_status_t *status)
{
    uint32_t flags = 0U;

    if (status == NULL) {
        return flags;
    }

    if (status->inputs.tof_data_valid) {
        flags |= (1U << 0);
    }
    if (status->inputs.tof_data_fresh) {
        flags |= (1U << 1);
    }

    return flags;
}

static uint32_t laser_controller_comms_encode_laser_flags(
    const laser_controller_runtime_status_t *status)
{
    uint32_t flags = 0U;

    if (status == NULL) {
        return flags;
    }

    if (status->outputs.enable_alignment_laser) {
        flags |= (1U << 0);
    }
    if (status->decision.nir_output_enable) {
        flags |= (1U << 1);
    }
    if (laser_controller_comms_driver_standby_effective(status)) {
        flags |= (1U << 2);
    }
    if (status->inputs.driver_loop_good) {
        flags |= (1U << 3);
    }
    if (status->inputs.ld_telemetry_valid) {
        flags |= (1U << 4);
    }

    return flags;
}

static uint32_t laser_controller_comms_encode_tec_flags(
    const laser_controller_runtime_status_t *status)
{
    uint32_t flags = 0U;

    if (status == NULL) {
        return flags;
    }

    if (status->inputs.tec_temp_good) {
        flags |= (1U << 0);
    }
    if (status->inputs.tec_telemetry_valid) {
        flags |= (1U << 1);
    }

    return flags;
}

static uint32_t laser_controller_comms_encode_safety_flags(
    const laser_controller_runtime_status_t *status)
{
    uint32_t flags = 0U;

    if (status == NULL) {
        return flags;
    }

    if (status->decision.allow_alignment) {
        flags |= (1U << 0);
    }
    if (status->decision.allow_nir) {
        flags |= (1U << 1);
    }
    if (status->decision.horizon_blocked) {
        flags |= (1U << 2);
    }
    if (status->decision.distance_blocked) {
        flags |= (1U << 3);
    }
    if (status->decision.lambda_drift_blocked) {
        flags |= (1U << 4);
    }
    if (status->decision.tec_temp_adc_blocked) {
        flags |= (1U << 5);
    }

    return flags;
}

static uint32_t laser_controller_comms_encode_button_flags(
    const laser_controller_runtime_status_t *status)
{
    uint32_t flags = 0U;

    if (status == NULL) {
        return flags;
    }

    if (status->inputs.button.stage1_pressed) {
        flags |= (1U << 0);
    }
    if (status->inputs.button.stage2_pressed) {
        flags |= (1U << 1);
    }
    /*
     * Side buttons packed into bits 2/3 as of 2026-04-15 so the 60 ms
     * fast-telemetry frame reflects live state. Prior to this the fast
     * frame only carried stage1/2, and the host's decodeFastTelemetry
     * path slammed side1/2 back to `false` on every tick, blowing away
     * the correct values from the 1 s live telemetry.
     */
    if (status->inputs.button.side1_pressed) {
        flags |= (1U << 2);
    }
    if (status->inputs.button.side2_pressed) {
        flags |= (1U << 3);
    }

    return flags;
}

static void laser_controller_comms_write_button_state_json(
    laser_controller_comms_buffer_t *buffer,
    const laser_controller_runtime_status_t *status)
{
    if (buffer == NULL || status == NULL) {
        return;
    }

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"buttons\":{\"stage1Pressed\":%s,\"stage2Pressed\":%s,"
        "\"stage1Edge\":%s,\"stage2Edge\":%s,"
        "\"side1Pressed\":%s,\"side2Pressed\":%s,"
        "\"side1Edge\":%s,\"side2Edge\":%s,"
        "\"boardReachable\":%s,\"isrFireCount\":%u},",
        status->inputs.button.stage1_pressed ? "true" : "false",
        status->inputs.button.stage2_pressed ? "true" : "false",
        status->inputs.button.stage1_edge ? "true" : "false",
        status->inputs.button.stage2_edge ? "true" : "false",
        status->inputs.button.side1_pressed ? "true" : "false",
        status->inputs.button.side2_pressed ? "true" : "false",
        status->inputs.button.side1_edge ? "true" : "false",
        status->inputs.button.side2_edge ? "true" : "false",
        status->inputs.button.board_reachable ? "true" : "false",
        (unsigned)status->inputs.button.isr_fire_count);
}

/*
 * Compute the trigger-phase token. Mirrors the firmware RGB policy in
 * app.c — keep these in lockstep when extending the policy.
 */
static const char *laser_controller_comms_trigger_phase(
    const laser_controller_runtime_status_t *status)
{
    if (status == NULL) {
        return "off";
    }
    const bool unrecoverable =
        (status->fault_latched &&
         status->latched_fault_class ==
             LASER_CONTROLLER_FAULT_CLASS_SYSTEM_MAJOR) ||
        (status->decision.fault_present &&
         status->decision.fault_class ==
             LASER_CONTROLLER_FAULT_CLASS_SYSTEM_MAJOR);
    if (unrecoverable) {
        return "unrecoverable";
    }
    if (status->fault_latched ||
        status->decision.fault_present ||
        status->button_runtime.nir_lockout) {
        return status->button_runtime.nir_lockout ? "lockout" : "interlock";
    }
    if (!status->deployment.active || !status->deployment.ready ||
        !status->deployment.ready_idle) {
        return "off";
    }
    if (status->decision.nir_output_enable) {
        return "firing";
    }
    if (status->bench.runtime_mode ==
            LASER_CONTROLLER_RUNTIME_MODE_BINARY_TRIGGER &&
        status->inputs.button.board_reachable &&
        status->inputs.button.stage1_pressed) {
        return "armed";
    }
    return "ready";
}

/*
 * Top-level buttonBoard block — published in every snapshot. Mirrors
 * laser_controller_button_board_readback_t + laser_controller_rgb_led_state_t
 * + the control-task button_runtime_status. Four-place sync target:
 * host-console/src/types.ts ButtonBoardStatus,
 * host-console/src/lib/mock-transport.ts mock helpers,
 * docs/protocol-spec.md.
 */
static void laser_controller_comms_write_button_board_json(
    laser_controller_comms_buffer_t *buffer,
    const laser_controller_runtime_status_t *status)
{
    if (buffer == NULL || status == NULL) {
        return;
    }

    const laser_controller_rgb_led_state_t *rgb = &status->outputs.rgb_led;
    const laser_controller_button_board_readback_t *bb =
        &status->inputs.buttons_readback;
    const laser_controller_rgb_led_readback_t *rb =
        &status->inputs.rgb_readback;

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"buttonBoard\":{"
        "\"mcpAddr\":\"0x%02X\",\"tlcAddr\":\"0x%02X\","
        "\"mcpReachable\":%s,\"mcpConfigured\":%s,"
        "\"mcpLastError\":%d,\"mcpConsecFailures\":%u,"
        "\"tlcReachable\":%s,\"tlcConfigured\":%s,"
        "\"tlcLastError\":%d,"
        "\"isrFireCount\":%u,"
        "\"rgb\":{\"r\":%u,\"g\":%u,\"b\":%u,"
        "\"blink\":%s,\"enabled\":%s,\"testActive\":%s},"
        "\"ledBrightnessPct\":%u,\"ledOwned\":%s,"
        "\"triggerLockout\":%s,\"triggerPhase\":\"%s\","
        "\"nirButtonBlockReason\":",
        (unsigned)LASER_CONTROLLER_BUTTON_MCP23017_I2C_ADDR,
        (unsigned)LASER_CONTROLLER_BUTTON_TLC59116_I2C_ADDR,
        bb->reachable ? "true" : "false",
        bb->configured ? "true" : "false",
        (int)bb->last_error,
        (unsigned)bb->consecutive_read_failures,
        rb->reachable ? "true" : "false",
        rb->configured ? "true" : "false",
        (int)rb->last_error,
        (unsigned)status->inputs.button.isr_fire_count,
        (unsigned)rgb->r,
        (unsigned)rgb->g,
        (unsigned)rgb->b,
        rgb->blink ? "true" : "false",
        rgb->enabled ? "true" : "false",
        status->button_runtime.rgb_test_active ? "true" : "false",
        (unsigned)status->button_runtime.led_brightness_pct,
        status->button_runtime.led_owned ? "true" : "false",
        status->button_runtime.nir_lockout ? "true" : "false",
        laser_controller_comms_trigger_phase(status));
    /*
     * 2026-04-16: append the diagnostic block-reason as a JSON-escaped
     * string so the operator can see WHY a button press isn't firing
     * NIR. Always non-empty (e.g. "none" if NIR is fireable, otherwise
     * a token like "fault: tof_out_of_range" or "lockout-latched").
     */
    laser_controller_comms_write_escaped_string(
        buffer,
        status->button_runtime.nir_block_reason);
    laser_controller_comms_buffer_append_raw(buffer, "},");
}

/*
 * host_control_readiness reasons — terse tokens the GUI renders as
 * pre-disabled reasons on the NIR / LED buttons. See
 * host-console/src/types.ts for the canonical union; any rename here must
 * land in the four-place protocol sync.
 */
static const char *laser_controller_comms_nir_blocked_reason(
    const laser_controller_runtime_status_t *status)
{
    if (status == NULL) {
        return "not-connected";
    }
    if (status->fault_latched) {
        return "fault-latched";
    }
    if (!status->deployment.active) {
        return "deployment-off";
    }
    if (status->deployment.running) {
        return "checklist-running";
    }
    if (!status->deployment.ready) {
        return "checklist-not-ready";
    }
    if (!status->deployment.ready_idle) {
        return "ready-not-idle";
    }
    if (status->bench.runtime_mode !=
            LASER_CONTROLLER_RUNTIME_MODE_MODULATED_HOST) {
        return "not-modulated-host";
    }
    if (status->power_tier != LASER_CONTROLLER_POWER_TIER_FULL) {
        return "power-not-full";
    }
    if (!status->inputs.ld_rail_pgood || !status->inputs.tec_rail_pgood) {
        return "rail-not-good";
    }
    if (status->config.require_tec_for_nir && !status->inputs.tec_temp_good) {
        return "tec-not-settled";
    }
    return "none";
}

static const char *laser_controller_comms_led_blocked_reason(
    const laser_controller_runtime_status_t *status)
{
    if (status == NULL) {
        return "not-connected";
    }
    if (!status->deployment.active) {
        return "deployment-off";
    }
    if (status->deployment.running) {
        return "checklist-running";
    }
    return "none";
}

static const char *laser_controller_comms_sbdn_state_name(
    laser_controller_sbdn_state_t state)
{
    switch (state) {
        case LASER_CONTROLLER_SBDN_STATE_ON:      return "on";
        case LASER_CONTROLLER_SBDN_STATE_STANDBY: return "standby";
        case LASER_CONTROLLER_SBDN_STATE_OFF:
        default:                                   return "off";
    }
}

static void laser_controller_comms_write_bench_json(
    laser_controller_comms_buffer_t *buffer,
    const laser_controller_runtime_status_t *status)
{
    const char *runtime_mode_lock_reason =
        laser_controller_comms_runtime_mode_switch_block_reason(status);

    if (buffer == NULL || status == NULL) {
        return;
    }

    laser_controller_comms_buffer_append_raw(buffer, "\"bench\":{");
    laser_controller_comms_buffer_append_raw(buffer, "\"targetMode\":");
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_bench_target_mode_name(status->bench.target_mode));
    laser_controller_comms_buffer_append_fmt(buffer, ",\"runtimeMode\":");
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_runtime_mode_name(status->bench.runtime_mode));
    laser_controller_comms_buffer_append_fmt(
        buffer,
        ",\"runtimeModeSwitchAllowed\":%s,\"runtimeModeLockReason\":",
        runtime_mode_lock_reason == NULL ? "true" : "false");
    laser_controller_comms_write_escaped_string(
        buffer,
        runtime_mode_lock_reason != NULL ? runtime_mode_lock_reason : "");
    laser_controller_comms_buffer_append_fmt(
        buffer,
        ",\"requestedAlignmentEnabled\":%s,\"appliedAlignmentEnabled\":%s,\"requestedNirEnabled\":%s,\"requestedCurrentA\":%.3f,\"requestedLedEnabled\":%s,\"requestedLedDutyCyclePct\":%lu,\"appliedLedOwner\":",
        status->bench.requested_alignment ? "true" : "false",
        status->outputs.enable_alignment_laser ? "true" : "false",
        status->bench.requested_nir ? "true" : "false",
        status->bench.high_state_current_a,
        status->bench.illumination_enabled ? "true" : "false",
        (unsigned long)status->bench.illumination_duty_cycle_pct);
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_comms_led_owner_name(status));
    laser_controller_comms_buffer_append_fmt(
        buffer,
        ",\"appliedLedPinHigh\":%s,\"illuminationEnabled\":%s,\"illuminationDutyCyclePct\":%lu,\"illuminationFrequencyHz\":%lu,\"modulationEnabled\":%s,\"modulationFrequencyHz\":%lu,\"modulationDutyCyclePct\":%lu,\"lowStateCurrentA\":%.3f",
        laser_controller_comms_gpio_pin_high(
            status,
            LASER_CONTROLLER_GPIO_TOF_LED_CTRL) ? "true" : "false",
        status->bench.illumination_enabled ? "true" : "false",
        (unsigned long)status->bench.illumination_duty_cycle_pct,
        (unsigned long)status->bench.illumination_frequency_hz,
        status->bench.modulation_enabled ? "true" : "false",
        (unsigned long)status->bench.modulation_frequency_hz,
        (unsigned long)status->bench.modulation_duty_cycle_pct,
        status->bench.low_state_current_a);

    /*
     * host_control_readiness block — terse reason tokens the GUI renders as
     * pre-disabled tooltip reasons. Green alignment is always "none" because
     * it is now ungated at the software level. Structured like this so the
     * GUI does not re-implement the firmware's gate ordering.
     */
    laser_controller_comms_buffer_append_raw(
        buffer,
        ",\"hostControlReadiness\":{\"nirBlockedReason\":");
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_comms_nir_blocked_reason(status));
    laser_controller_comms_buffer_append_raw(
        buffer,
        ",\"alignmentBlockedReason\":\"none\",\"ledBlockedReason\":");
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_comms_led_blocked_reason(status));
    laser_controller_comms_buffer_append_raw(buffer, ",\"sbdnState\":");
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_comms_sbdn_state_name(status->outputs.sbdn_state));
    laser_controller_comms_buffer_append_raw(buffer, "}");

    /*
     * usbDebugMock block — operator visibility so the host can render a
     * loud banner whenever telemetry is synthesized. Per AGENT.md, the
     * GUI MUST surface this state every time it is true; failing to do so
     * is a safety-visibility regression.
     */
    {
        laser_controller_usb_debug_mock_status_t mock_status;
        memset(&mock_status, 0, sizeof(mock_status));
        laser_controller_usb_debug_mock_get_status(&mock_status);
        laser_controller_comms_buffer_append_fmt(
            buffer,
            ",\"usbDebugMock\":{\"active\":%s,\"pdConflictLatched\":%s,\"enablePending\":%s,\"activatedAtMs\":%llu,\"deactivatedAtMs\":%llu,\"lastDisableReason\":",
            mock_status.active ? "true" : "false",
            mock_status.pd_conflict_latched ? "true" : "false",
            mock_status.enable_pending ? "true" : "false",
            (unsigned long long)mock_status.activated_at_ms,
            (unsigned long long)mock_status.deactivated_at_ms);
        laser_controller_comms_write_escaped_string(
            buffer,
            mock_status.last_disable_reason != NULL
                ? mock_status.last_disable_reason
                : "");
        laser_controller_comms_buffer_append_raw(buffer, "}}");
    }
}

static void laser_controller_comms_write_fault_json(
    laser_controller_comms_buffer_t *buffer,
    const laser_controller_runtime_status_t *status,
    bool include_counters)
{
    if (buffer == NULL || status == NULL) {
        return;
    }

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"fault\":{\"latched\":%s,\"activeCode\":",
        status->fault_latched ? "true" : "false");
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_fault_code_name(status->active_fault_code));
    laser_controller_comms_buffer_append_raw(buffer, ",\"activeClass\":");
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_fault_class_name(status->active_fault_class));
    laser_controller_comms_buffer_append_raw(buffer, ",\"latchedCode\":");
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_fault_code_name(status->latched_fault_code));
    laser_controller_comms_buffer_append_raw(buffer, ",\"latchedClass\":");
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_fault_class_name(status->latched_fault_class));
    /*
     * Reason strings (added 2026-04-16). Surfaces the fault detail that
     * record_fault captured — e.g. "illegal state transition X -> Y
     * blocked" for UNEXPECTED_STATE. Tolerant parser on the host side
     * treats missing fields as empty.
     */
    laser_controller_comms_buffer_append_raw(buffer, ",\"activeReason\":");
    laser_controller_comms_write_escaped_string(
        buffer,
        status->active_fault_reason);
    laser_controller_comms_buffer_append_raw(buffer, ",\"latchedReason\":");
    laser_controller_comms_write_escaped_string(
        buffer,
        status->latched_fault_reason);

    /*
     * Trigger diagnostic frame — currently populated only for LD_OVERTEMP
     * (added 2026-04-15 late to address spurious trips with no operator
     * visibility into the ADC reading / gate state). When `valid == false`
     * we emit `triggerDiag:null` so the host can render an explicit
     * "no diag" state without a schema-version dance.
     */
    if (status->active_fault_diag.valid) {
        laser_controller_comms_buffer_append_raw(buffer, ",\"triggerDiag\":{\"code\":");
        laser_controller_comms_write_escaped_string(
            buffer,
            laser_controller_fault_code_name(status->active_fault_diag.code));
        laser_controller_comms_buffer_append_fmt(
            buffer,
            ",\"measuredC\":%.3f,\"measuredVoltageV\":%.4f,\"limitC\":%.3f,"
            "\"ldPgoodForMs\":%lu,\"sbdnNotOffForMs\":%lu,\"expr\":",
            (double)status->active_fault_diag.measured_c,
            (double)status->active_fault_diag.measured_voltage_v,
            (double)status->active_fault_diag.limit_c,
            (unsigned long)status->active_fault_diag.ld_pgood_for_ms,
            (unsigned long)status->active_fault_diag.sbdn_not_off_for_ms);
        laser_controller_comms_write_escaped_string(
            buffer,
            status->active_fault_diag.expr);
        laser_controller_comms_buffer_append_raw(buffer, "}");
    } else {
        laser_controller_comms_buffer_append_raw(buffer, ",\"triggerDiag\":null");
    }

    if (!include_counters) {
        laser_controller_comms_buffer_append_raw(buffer, "}");
        return;
    }

    laser_controller_comms_buffer_append_fmt(
        buffer,
        ",\"activeCount\":%lu,\"tripCounter\":%lu,",
        (unsigned long)status->active_fault_count,
        (unsigned long)status->trip_counter);
    if (status->last_fault_ms == 0U) {
        laser_controller_comms_buffer_append_raw(buffer, "\"lastFaultAtIso\":null}");
    } else {
        laser_controller_comms_buffer_append_raw(
            buffer,
            "\"lastFaultAtIso\":\"1970-01-01T00:00:00Z\"}");
    }
}

static void laser_controller_comms_write_module_json(
    laser_controller_comms_buffer_t *buffer,
    const laser_controller_module_status_t *module)
{
    if (module == NULL) {
        laser_controller_comms_buffer_append_raw(
            buffer,
            "{\"expectedPresent\":false,\"debugEnabled\":false,\"detected\":false,\"healthy\":false}");
        return;
    }

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "{\"expectedPresent\":%s,\"debugEnabled\":%s,\"detected\":%s,\"healthy\":%s}",
        module->expected_present ? "true" : "false",
        module->debug_enabled ? "true" : "false",
        module->detected ? "true" : "false",
        module->healthy ? "true" : "false");
}

static void laser_controller_comms_write_wireless_scan_results_json(
    laser_controller_comms_buffer_t *buffer,
    const laser_controller_wireless_status_t *wireless)
{
    uint8_t result_count = 0U;

    laser_controller_comms_buffer_append_raw(buffer, "[");
    if (wireless != NULL) {
        result_count = wireless->scan_result_count;
        if (result_count > LASER_CONTROLLER_WIRELESS_SCAN_RESULT_COUNT) {
            result_count = LASER_CONTROLLER_WIRELESS_SCAN_RESULT_COUNT;
        }
    }

    for (uint8_t index = 0U; index < result_count; ++index) {
        const laser_controller_wireless_scan_result_t *result =
            &wireless->scan_results[index];

        if (index > 0U) {
            laser_controller_comms_buffer_append_raw(buffer, ",");
        }
        laser_controller_comms_buffer_append_raw(
            buffer,
            "{\"ssid\":");
        laser_controller_comms_write_escaped_string(buffer, result->ssid);
        laser_controller_comms_buffer_append_fmt(
            buffer,
            ",\"rssiDbm\":%d,\"channel\":%u,\"secure\":%s}",
            (int)result->rssi_dbm,
            (unsigned int)result->channel,
            result->secure ? "true" : "false");
    }
    laser_controller_comms_buffer_append_raw(buffer, "]");
}

static void laser_controller_comms_write_wireless_json(
    laser_controller_comms_buffer_t *buffer,
    const laser_controller_wireless_status_t *wireless)
{
    if (wireless == NULL) {
        laser_controller_comms_buffer_append_raw(
            buffer,
            "{\"started\":false,\"mode\":\"softap\",\"apReady\":false,"
            "\"stationConfigured\":false,\"stationConnecting\":false,"
            "\"stationConnected\":false,\"clientCount\":0,\"ssid\":\"\","
            "\"stationSsid\":\"\",\"stationRssiDbm\":0,"
            "\"stationChannel\":0,\"scanInProgress\":false,"
            "\"scannedNetworks\":[],\"ipAddress\":\"\",\"wsUrl\":\"\","
            "\"lastError\":\"\"}");
        return;
    }

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "{\"started\":%s,\"mode\":",
        wireless->started ? "true" : "false");
    laser_controller_comms_write_escaped_string(
        buffer,
        wireless->mode == LASER_CONTROLLER_WIRELESS_MODE_STATION ?
            "station" :
            "softap");
    laser_controller_comms_buffer_append_fmt(
        buffer,
        ",\"apReady\":%s,\"stationConfigured\":%s,"
        "\"stationConnecting\":%s,\"stationConnected\":%s,"
        "\"clientCount\":%u,\"ssid\":",
        wireless->ap_ready ? "true" : "false",
        wireless->station_configured ? "true" : "false",
        wireless->station_connecting ? "true" : "false",
        wireless->station_connected ? "true" : "false",
        (unsigned int)wireless->client_count);
    laser_controller_comms_write_escaped_string(buffer, wireless->ssid);
    laser_controller_comms_buffer_append_raw(buffer, ",\"stationSsid\":");
    laser_controller_comms_write_escaped_string(buffer, wireless->station_ssid);
    laser_controller_comms_buffer_append_fmt(
        buffer,
        ",\"stationRssiDbm\":%d,\"stationChannel\":%u,"
        "\"scanInProgress\":%s,\"scannedNetworks\":",
        (int)wireless->station_rssi_dbm,
        (unsigned int)wireless->station_channel,
        wireless->scan_in_progress ? "true" : "false");
    laser_controller_comms_write_wireless_scan_results_json(buffer, wireless);
    laser_controller_comms_buffer_append_raw(buffer, ",\"ipAddress\":");
    laser_controller_comms_write_escaped_string(buffer, wireless->ip_address);
    laser_controller_comms_buffer_append_raw(buffer, ",\"wsUrl\":");
    laser_controller_comms_write_escaped_string(buffer, wireless->ws_url);
    laser_controller_comms_buffer_append_raw(buffer, ",\"lastError\":");
    laser_controller_comms_write_escaped_string(buffer, wireless->last_error);
    laser_controller_comms_buffer_append_raw(buffer, "}");
}

static void laser_controller_comms_write_pd_profile_json(
    laser_controller_comms_buffer_t *buffer,
    const laser_controller_service_pd_profile_t *profile)
{
    if (profile == NULL) {
        laser_controller_comms_buffer_append_raw(
            buffer,
            "{\"enabled\":false,\"voltageV\":0.0,\"currentA\":0.0}");
        return;
    }

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "{\"enabled\":%s,\"voltageV\":%.2f,\"currentA\":%.2f}",
        profile->enabled ? "true" : "false",
        profile->voltage_v,
        profile->current_a);
}

static void laser_controller_comms_write_error_name_json(
    laser_controller_comms_buffer_t *buffer,
    int32_t error_code)
{
    laser_controller_comms_write_escaped_string(
        buffer,
        esp_err_to_name((esp_err_t)error_code));
}

static void laser_controller_comms_write_peripheral_readback_json(
    laser_controller_comms_buffer_t *buffer,
    const laser_controller_runtime_status_t *status)
{
    const laser_controller_board_dac_readback_t *dac = &status->inputs.dac;
    const laser_controller_board_pd_readback_t *pd = &status->inputs.pd_readback;
    const laser_controller_board_imu_readback_t *imu = &status->inputs.imu_readback;
    const laser_controller_board_haptic_readback_t *haptic =
        &status->inputs.haptic_readback;
    const laser_controller_board_tof_readback_t *tof =
        &status->inputs.tof_readback;

    laser_controller_comms_buffer_append_raw(buffer, "\"peripherals\":{");

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"dac\":{\"reachable\":%s,\"configured\":%s,\"refAlarm\":%s,\"syncReg\":%u,\"configReg\":%u,\"gainReg\":%u,\"statusReg\":%u,\"dataAReg\":%u,\"dataBReg\":%u,\"lastErrorCode\":%ld,\"lastError\":",
        dac->reachable ? "true" : "false",
        dac->configured ? "true" : "false",
        dac->ref_alarm ? "true" : "false",
        (unsigned)dac->sync_reg,
        (unsigned)dac->config_reg,
        (unsigned)dac->gain_reg,
        (unsigned)dac->status_reg,
        (unsigned)dac->data_a_reg,
        (unsigned)dac->data_b_reg,
        (long)dac->last_error);
    laser_controller_comms_write_error_name_json(buffer, dac->last_error);
    laser_controller_comms_buffer_append_raw(buffer, "},");

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"pd\":{\"reachable\":%s,\"attached\":%s,\"ccStatusReg\":%u,\"pdoCountReg\":%u,\"rdoStatusRaw\":%lu},",
        pd->reachable ? "true" : "false",
        pd->attached ? "true" : "false",
        (unsigned)pd->cc_status_reg,
        (unsigned)pd->pdo_count_reg,
        (unsigned long)pd->rdo_status_raw);

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"imu\":{\"reachable\":%s,\"configured\":%s,\"whoAmI\":%u,\"statusReg\":%u,\"ctrl1XlReg\":%u,\"ctrl2GReg\":%u,\"ctrl3CReg\":%u,\"ctrl4CReg\":%u,\"ctrl10CReg\":%u,\"lastErrorCode\":%ld,\"lastError\":",
        imu->reachable ? "true" : "false",
        imu->configured ? "true" : "false",
        (unsigned)imu->who_am_i,
        (unsigned)imu->status_reg,
        (unsigned)imu->ctrl1_xl_reg,
        (unsigned)imu->ctrl2_g_reg,
        (unsigned)imu->ctrl3_c_reg,
        (unsigned)imu->ctrl4_c_reg,
        (unsigned)imu->ctrl10_c_reg,
        (long)imu->last_error);
    laser_controller_comms_write_error_name_json(buffer, imu->last_error);
    laser_controller_comms_buffer_append_raw(buffer, "},");

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"haptic\":{\"reachable\":%s,\"enablePinHigh\":%s,\"triggerPinHigh\":%s,\"modeReg\":%u,\"libraryReg\":%u,\"goReg\":%u,\"feedbackReg\":%u,\"lastErrorCode\":%ld,\"lastError\":",
        haptic->reachable ? "true" : "false",
        haptic->enable_pin_high ? "true" : "false",
        haptic->trigger_pin_high ? "true" : "false",
        (unsigned)haptic->mode_reg,
        (unsigned)haptic->library_reg,
        (unsigned)haptic->go_reg,
        (unsigned)haptic->feedback_reg,
        (long)haptic->last_error);
    laser_controller_comms_write_error_name_json(buffer, haptic->last_error);
    laser_controller_comms_buffer_append_raw(buffer, "},");

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"tof\":{\"reachable\":%s,\"configured\":%s,\"interruptLineHigh\":%s,\"ledCtrlAsserted\":%s,\"dataReady\":%s,\"bootState\":%u,\"rangeStatus\":%u,\"sensorId\":%u,\"distanceMm\":%u,\"lastErrorCode\":%ld,\"lastError\":",
        tof->reachable ? "true" : "false",
        tof->configured ? "true" : "false",
        tof->interrupt_line_high ? "true" : "false",
        tof->led_ctrl_asserted ? "true" : "false",
        tof->data_ready ? "true" : "false",
        (unsigned)tof->boot_state,
        (unsigned)tof->range_status,
        (unsigned)tof->sensor_id,
        (unsigned)tof->distance_mm,
        (long)tof->last_error);
    laser_controller_comms_write_error_name_json(buffer, tof->last_error);
    laser_controller_comms_buffer_append_raw(buffer, "}},");
}

static void laser_controller_comms_write_haptic_readback_json(
    laser_controller_comms_buffer_t *buffer,
    const laser_controller_runtime_status_t *status)
{
    const laser_controller_board_haptic_readback_t *haptic =
        &status->inputs.haptic_readback;

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"peripherals\":{\"haptic\":{\"reachable\":%s,\"enablePinHigh\":%s,\"triggerPinHigh\":%s,\"modeReg\":%u,\"libraryReg\":%u,\"goReg\":%u,\"feedbackReg\":%u,\"lastErrorCode\":%ld,\"lastError\":",
        haptic->reachable ? "true" : "false",
        haptic->enable_pin_high ? "true" : "false",
        haptic->trigger_pin_high ? "true" : "false",
        (unsigned)haptic->mode_reg,
        (unsigned)haptic->library_reg,
        (unsigned)haptic->go_reg,
        (unsigned)haptic->feedback_reg,
        (long)haptic->last_error);
    laser_controller_comms_write_error_name_json(buffer, haptic->last_error);
    laser_controller_comms_buffer_append_raw(buffer, "}},");
}

static void laser_controller_comms_write_gpio_inspector_json(
    laser_controller_comms_buffer_t *buffer,
    const laser_controller_runtime_status_t *status)
{
    const laser_controller_board_gpio_inspector_t *gpio =
        &status->inputs.gpio_inspector;

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"gpioInspector\":{\"anyOverrideActive\":%s,\"activeOverrideCount\":%lu,\"pins\":[",
        gpio->any_override_active ? "true" : "false",
        (unsigned long)gpio->active_override_count);

    for (size_t index = 0U;
         index < LASER_CONTROLLER_BOARD_GPIO_INSPECTOR_PIN_COUNT;
         ++index) {
        const laser_controller_board_gpio_pin_readback_t *pin = &gpio->pins[index];
        uint32_t live_flags = 0U;
        uint32_t override_flags = 0U;

        if (index > 0U) {
            laser_controller_comms_buffer_append_raw(buffer, ",");
        }

        if (pin->output_capable) {
            live_flags |= (1U << 0);
        }
        if (pin->input_enabled) {
            live_flags |= (1U << 1);
        }
        if (pin->output_enabled) {
            live_flags |= (1U << 2);
        }
        if (pin->open_drain_enabled) {
            live_flags |= (1U << 3);
        }
        if (pin->pullup_enabled) {
            live_flags |= (1U << 4);
        }
        if (pin->pulldown_enabled) {
            live_flags |= (1U << 5);
        }
        if (pin->level_high) {
            live_flags |= (1U << 6);
        }
        if (pin->override_active) {
            live_flags |= (1U << 7);
        }

        override_flags = (uint32_t)pin->override_mode;
        if (pin->override_level_high) {
            override_flags |= (1U << 2);
        }
        if (pin->override_pullup_enabled) {
            override_flags |= (1U << 3);
        }
        if (pin->override_pulldown_enabled) {
            override_flags |= (1U << 4);
        }

        laser_controller_comms_buffer_append_fmt(
            buffer,
            "[%u,%lu,%lu]",
            (unsigned)pin->gpio_num,
            (unsigned long)live_flags,
            (unsigned long)override_flags);
    }

    laser_controller_comms_buffer_append_raw(buffer, "]},");
}

static void laser_controller_comms_write_modules_only_json(
    laser_controller_comms_buffer_t *buffer,
    const laser_controller_runtime_status_t *status)
{
    laser_controller_comms_buffer_append_raw(buffer, "\"modules\":{");
    laser_controller_comms_buffer_append_raw(buffer, "\"imu\":");
    laser_controller_comms_write_module_json(
        buffer,
        &status->bringup.modules[LASER_CONTROLLER_MODULE_IMU]);
    laser_controller_comms_buffer_append_raw(buffer, ",\"dac\":");
    laser_controller_comms_write_module_json(
        buffer,
        &status->bringup.modules[LASER_CONTROLLER_MODULE_DAC]);
    laser_controller_comms_buffer_append_raw(buffer, ",\"haptic\":");
    laser_controller_comms_write_module_json(
        buffer,
        &status->bringup.modules[LASER_CONTROLLER_MODULE_HAPTIC]);
    laser_controller_comms_buffer_append_raw(buffer, ",\"tof\":");
    laser_controller_comms_write_module_json(
        buffer,
        &status->bringup.modules[LASER_CONTROLLER_MODULE_TOF]);
    laser_controller_comms_buffer_append_raw(buffer, ",\"buttons\":");
    laser_controller_comms_write_module_json(
        buffer,
        &status->bringup.modules[LASER_CONTROLLER_MODULE_BUTTONS]);
    laser_controller_comms_buffer_append_raw(buffer, ",\"pd\":");
    laser_controller_comms_write_module_json(
        buffer,
        &status->bringup.modules[LASER_CONTROLLER_MODULE_PD]);
    laser_controller_comms_buffer_append_raw(buffer, ",\"laserDriver\":");
    laser_controller_comms_write_module_json(
        buffer,
        &status->bringup.modules[LASER_CONTROLLER_MODULE_LASER_DRIVER]);
    laser_controller_comms_buffer_append_raw(buffer, ",\"tec\":");
    laser_controller_comms_write_module_json(
        buffer,
        &status->bringup.modules[LASER_CONTROLLER_MODULE_TEC]);
    laser_controller_comms_buffer_append_raw(buffer, "}");
}

static void laser_controller_comms_write_tools_only_json(
    laser_controller_comms_buffer_t *buffer,
    const laser_controller_runtime_status_t *status)
{
    laser_controller_comms_buffer_append_raw(buffer, "\"tools\":{\"lastI2cScan\":");
    laser_controller_comms_write_escaped_string(buffer, status->bringup.last_i2c_scan);
    laser_controller_comms_buffer_append_raw(buffer, ",\"lastI2cOp\":");
    laser_controller_comms_write_escaped_string(buffer, status->bringup.last_i2c_op);
    laser_controller_comms_buffer_append_raw(buffer, ",\"lastSpiOp\":");
    laser_controller_comms_write_escaped_string(buffer, status->bringup.last_spi_op);
    laser_controller_comms_buffer_append_raw(buffer, ",\"lastAction\":");
    laser_controller_comms_write_escaped_string(buffer, status->bringup.last_action);
    laser_controller_comms_buffer_append_raw(buffer, "}");
}

static void laser_controller_comms_write_peripheral_subset_json(
    laser_controller_comms_buffer_t *buffer,
    const laser_controller_runtime_status_t *status,
    uint32_t peripheral_mask)
{
    bool first = true;

    laser_controller_comms_buffer_append_raw(buffer, "\"peripherals\":{");

    if ((peripheral_mask & LASER_CONTROLLER_COMMS_PERIPHERAL_DAC) != 0U) {
        const laser_controller_board_dac_readback_t *dac = &status->inputs.dac;

        laser_controller_comms_buffer_append_fmt(
            buffer,
            "\"dac\":{\"reachable\":%s,\"configured\":%s,\"refAlarm\":%s,\"syncReg\":%u,\"configReg\":%u,\"gainReg\":%u,\"statusReg\":%u,\"dataAReg\":%u,\"dataBReg\":%u,\"lastErrorCode\":%ld,\"lastError\":",
            dac->reachable ? "true" : "false",
            dac->configured ? "true" : "false",
            dac->ref_alarm ? "true" : "false",
            (unsigned)dac->sync_reg,
            (unsigned)dac->config_reg,
            (unsigned)dac->gain_reg,
            (unsigned)dac->status_reg,
            (unsigned)dac->data_a_reg,
            (unsigned)dac->data_b_reg,
            (long)dac->last_error);
        laser_controller_comms_write_error_name_json(buffer, dac->last_error);
        laser_controller_comms_buffer_append_raw(buffer, "}");
        first = false;
    }

    if ((peripheral_mask & LASER_CONTROLLER_COMMS_PERIPHERAL_PD) != 0U) {
        const laser_controller_board_pd_readback_t *pd = &status->inputs.pd_readback;

        if (!first) {
            laser_controller_comms_buffer_append_raw(buffer, ",");
        }
        laser_controller_comms_buffer_append_fmt(
            buffer,
            "\"pd\":{\"reachable\":%s,\"attached\":%s,\"ccStatusReg\":%u,\"pdoCountReg\":%u,\"rdoStatusRaw\":%lu}",
            pd->reachable ? "true" : "false",
            pd->attached ? "true" : "false",
            (unsigned)pd->cc_status_reg,
            (unsigned)pd->pdo_count_reg,
            (unsigned long)pd->rdo_status_raw);
        first = false;
    }

    if ((peripheral_mask & LASER_CONTROLLER_COMMS_PERIPHERAL_IMU) != 0U) {
        const laser_controller_board_imu_readback_t *imu = &status->inputs.imu_readback;

        if (!first) {
            laser_controller_comms_buffer_append_raw(buffer, ",");
        }
        laser_controller_comms_buffer_append_fmt(
            buffer,
            "\"imu\":{\"reachable\":%s,\"configured\":%s,\"whoAmI\":%u,\"statusReg\":%u,\"ctrl1XlReg\":%u,\"ctrl2GReg\":%u,\"ctrl3CReg\":%u,\"ctrl4CReg\":%u,\"ctrl10CReg\":%u,\"lastErrorCode\":%ld,\"lastError\":",
            imu->reachable ? "true" : "false",
            imu->configured ? "true" : "false",
            (unsigned)imu->who_am_i,
            (unsigned)imu->status_reg,
            (unsigned)imu->ctrl1_xl_reg,
            (unsigned)imu->ctrl2_g_reg,
            (unsigned)imu->ctrl3_c_reg,
            (unsigned)imu->ctrl4_c_reg,
            (unsigned)imu->ctrl10_c_reg,
            (long)imu->last_error);
        laser_controller_comms_write_error_name_json(buffer, imu->last_error);
        laser_controller_comms_buffer_append_raw(buffer, "}");
        first = false;
    }

    if ((peripheral_mask & LASER_CONTROLLER_COMMS_PERIPHERAL_HAPTIC) != 0U) {
        const laser_controller_board_haptic_readback_t *haptic =
            &status->inputs.haptic_readback;

        if (!first) {
            laser_controller_comms_buffer_append_raw(buffer, ",");
        }
        laser_controller_comms_buffer_append_fmt(
            buffer,
            "\"haptic\":{\"reachable\":%s,\"enablePinHigh\":%s,\"triggerPinHigh\":%s,\"modeReg\":%u,\"libraryReg\":%u,\"goReg\":%u,\"feedbackReg\":%u,\"lastErrorCode\":%ld,\"lastError\":",
            haptic->reachable ? "true" : "false",
            haptic->enable_pin_high ? "true" : "false",
            haptic->trigger_pin_high ? "true" : "false",
            (unsigned)haptic->mode_reg,
            (unsigned)haptic->library_reg,
            (unsigned)haptic->go_reg,
            (unsigned)haptic->feedback_reg,
            (long)haptic->last_error);
        laser_controller_comms_write_error_name_json(buffer, haptic->last_error);
        laser_controller_comms_buffer_append_raw(buffer, "}");
        first = false;
    }

    if ((peripheral_mask & LASER_CONTROLLER_COMMS_PERIPHERAL_TOF) != 0U) {
        const laser_controller_board_tof_readback_t *tof =
            &status->inputs.tof_readback;

        if (!first) {
            laser_controller_comms_buffer_append_raw(buffer, ",");
        }
        laser_controller_comms_buffer_append_fmt(
            buffer,
            "\"tof\":{\"reachable\":%s,\"configured\":%s,\"interruptLineHigh\":%s,\"ledCtrlAsserted\":%s,\"dataReady\":%s,\"bootState\":%u,\"rangeStatus\":%u,\"sensorId\":%u,\"distanceMm\":%u,\"lastErrorCode\":%ld,\"lastError\":",
            tof->reachable ? "true" : "false",
            tof->configured ? "true" : "false",
            tof->interrupt_line_high ? "true" : "false",
            tof->led_ctrl_asserted ? "true" : "false",
            tof->data_ready ? "true" : "false",
            (unsigned)tof->boot_state,
            (unsigned)tof->range_status,
            (unsigned)tof->sensor_id,
            (unsigned)tof->distance_mm,
            (long)tof->last_error);
        laser_controller_comms_write_error_name_json(buffer, tof->last_error);
        laser_controller_comms_buffer_append_raw(buffer, "}");
    }

    laser_controller_comms_buffer_append_raw(buffer, "}");
}

static uint8_t laser_controller_comms_expected_pd_profile_count(
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

static bool laser_controller_comms_pd_profiles_match(
    const laser_controller_service_pd_profile_t *expected_profiles,
    const laser_controller_service_pd_profile_t *actual_profiles,
    uint8_t actual_profile_count)
{
    if (expected_profiles == NULL || actual_profiles == NULL) {
        return false;
    }

    if (actual_profile_count !=
        laser_controller_comms_expected_pd_profile_count(expected_profiles)) {
        return false;
    }

    for (size_t index = 0U;
         index < LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT;
         ++index) {
        if (expected_profiles[index].enabled != actual_profiles[index].enabled) {
            return false;
        }

        if (!expected_profiles[index].enabled) {
            continue;
        }

        if (fabsf(expected_profiles[index].voltage_v - actual_profiles[index].voltage_v) > 0.06f ||
            fabsf(expected_profiles[index].current_a - actual_profiles[index].current_a) > 0.02f) {
            return false;
        }
    }

    return true;
}

static void laser_controller_comms_extract_pd_config(
    const char *line,
    laser_controller_power_policy_t *power_policy,
    laser_controller_service_pd_profile_t *profiles,
    size_t profile_count,
    bool *firmware_plan_enabled)
{
    float value_f = 0.0f;

    if (line == NULL || profiles == NULL ||
        profile_count != LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT) {
        return;
    }

    if (firmware_plan_enabled != NULL) {
        (void)laser_controller_comms_extract_bool(
            line,
            "\"firmware_plan_enabled\":",
            firmware_plan_enabled);
    }

    if (power_policy != NULL) {
        if (laser_controller_comms_extract_float(
                line,
                "\"programming_only_max_w\":",
                &value_f)) {
            power_policy->programming_only_max_w = value_f;
        }
        if (laser_controller_comms_extract_float(
                line,
                "\"reduced_mode_min_w\":",
                &value_f)) {
            power_policy->reduced_mode_min_w = value_f;
        }
        if (laser_controller_comms_extract_float(
                line,
                "\"reduced_mode_max_w\":",
                &value_f)) {
            power_policy->reduced_mode_max_w = value_f;
        }
        if (laser_controller_comms_extract_float(
                line,
                "\"full_mode_min_w\":",
                &value_f)) {
            power_policy->full_mode_min_w = value_f;
        }
    }

    for (size_t index = 0U; index < LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT; ++index) {
        char key[48];

        (void)snprintf(
            key,
            sizeof(key),
            "\"pdo%u_enabled\":",
            (unsigned int)(index + 1U));
        (void)laser_controller_comms_extract_bool(
            line,
            key,
            &profiles[index].enabled);

        (void)snprintf(
            key,
            sizeof(key),
            "\"pdo%u_voltage_v\":",
            (unsigned int)(index + 1U));
        if (laser_controller_comms_extract_float(line, key, &value_f)) {
            profiles[index].voltage_v = value_f;
        }

        (void)snprintf(
            key,
            sizeof(key),
            "\"pdo%u_current_a\":",
            (unsigned int)(index + 1U));
        if (laser_controller_comms_extract_float(line, key, &value_f)) {
            profiles[index].current_a = value_f;
        }
    }
}

static bool laser_controller_comms_service_mode_active(
    const laser_controller_runtime_status_t *status)
{
    return status != NULL &&
           status->state == LASER_CONTROLLER_STATE_SERVICE_MODE;
}

static bool laser_controller_comms_wait_for_service_mode(
    bool enabled,
    laser_controller_runtime_status_t *status)
{
    const TickType_t start_ticks = xTaskGetTickCount();
    const TickType_t timeout_ticks =
        pdMS_TO_TICKS(LASER_CONTROLLER_SERVICE_MODE_WAIT_MS);
    const TickType_t poll_ticks =
        pdMS_TO_TICKS(LASER_CONTROLLER_SERVICE_MODE_POLL_MS);

    if (status == NULL) {
        return false;
    }

    do {
        if (!laser_controller_app_copy_status(status)) {
            return false;
        }

        if (enabled) {
            if (laser_controller_comms_service_mode_active(status)) {
                return true;
            }
        } else if (!laser_controller_comms_service_mode_active(status)) {
            return true;
        }

        vTaskDelay(poll_ticks);
    } while ((xTaskGetTickCount() - start_ticks) < timeout_ticks);

    return laser_controller_app_copy_status(status) &&
           (enabled ? laser_controller_comms_service_mode_active(status) :
                      !laser_controller_comms_service_mode_active(status));
}

static bool laser_controller_comms_wait_for_haptic_enable(
    bool enabled,
    laser_controller_runtime_status_t *status)
{
    const TickType_t start_ticks = xTaskGetTickCount();
    const TickType_t timeout_ticks =
        pdMS_TO_TICKS(LASER_CONTROLLER_HAPTIC_GPIO_WAIT_MS);
    const TickType_t poll_ticks =
        pdMS_TO_TICKS(LASER_CONTROLLER_SERVICE_MODE_POLL_MS);

    if (status == NULL) {
        return false;
    }

    do {
        if (!laser_controller_app_copy_status(status)) {
            return false;
        }

        if (status->inputs.haptic_readback.enable_pin_high == enabled) {
            return true;
        }

        vTaskDelay(poll_ticks);
    } while ((xTaskGetTickCount() - start_ticks) < timeout_ticks);

    return laser_controller_app_copy_status(status) &&
           status->inputs.haptic_readback.enable_pin_high == enabled;
}

static bool laser_controller_comms_wait_for_deployment_mode(
    bool enabled,
    laser_controller_runtime_status_t *status)
{
    const TickType_t start_ticks = xTaskGetTickCount();
    const TickType_t timeout_ticks =
        pdMS_TO_TICKS(LASER_CONTROLLER_SERVICE_MODE_WAIT_MS);
    const TickType_t poll_ticks =
        pdMS_TO_TICKS(LASER_CONTROLLER_SERVICE_MODE_POLL_MS);

    if (status == NULL) {
        return false;
    }

    do {
        if (!laser_controller_app_copy_status(status)) {
            return false;
        }

        if (enabled ? status->deployment.active : !status->deployment.active) {
            return true;
        }

        vTaskDelay(poll_ticks);
    } while ((xTaskGetTickCount() - start_ticks) < timeout_ticks);

    return laser_controller_app_copy_status(status) &&
           (enabled ? status->deployment.active : !status->deployment.active);
}

static bool laser_controller_comms_wait_for_deployment_result(
    laser_controller_runtime_status_t *status)
{
    const TickType_t start_ticks = xTaskGetTickCount();
    const TickType_t timeout_ticks = pdMS_TO_TICKS(60000U);
    const TickType_t poll_ticks =
        pdMS_TO_TICKS(LASER_CONTROLLER_SERVICE_MODE_POLL_MS);

    if (status == NULL) {
        return false;
    }

    do {
        if (!laser_controller_app_copy_status(status)) {
            return false;
        }

        if (status->deployment.active &&
            !status->deployment.running &&
            (status->deployment.ready || status->deployment.failed)) {
            return true;
        }

        vTaskDelay(poll_ticks);
    } while ((xTaskGetTickCount() - start_ticks) < timeout_ticks);

    return laser_controller_app_copy_status(status) &&
           status->deployment.active &&
           !status->deployment.running &&
           (status->deployment.ready || status->deployment.failed);
}

static bool laser_controller_comms_wait_for_supply_state(
    laser_controller_service_supply_t supply,
    bool enabled,
    const laser_controller_runtime_status_t *seed_status,
    laser_controller_runtime_status_t *status)
{
    const TickType_t start_ticks = xTaskGetTickCount();
    const uint32_t timeout_ms =
        enabled ?
            ((seed_status != NULL &&
              seed_status->config.timeouts.rail_good_timeout_ms >
                  LASER_CONTROLLER_SUPPLY_ENABLE_WAIT_MS) ?
                 seed_status->config.timeouts.rail_good_timeout_ms :
                 LASER_CONTROLLER_SUPPLY_ENABLE_WAIT_MS) :
            LASER_CONTROLLER_SUPPLY_DISABLE_WAIT_MS;
    const TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    const TickType_t poll_ticks =
        pdMS_TO_TICKS(LASER_CONTROLLER_SERVICE_MODE_POLL_MS);

    if (status == NULL) {
        return false;
    }

    do {
        bool output_enabled = false;
        bool pgood = false;

        if (!laser_controller_app_copy_status(status)) {
            return false;
        }

        output_enabled =
            supply == LASER_CONTROLLER_SERVICE_SUPPLY_LD ?
                status->outputs.enable_ld_vin :
                status->outputs.enable_tec_vin;
        pgood =
            supply == LASER_CONTROLLER_SERVICE_SUPPLY_LD ?
                status->inputs.ld_rail_pgood :
                status->inputs.tec_rail_pgood;

        if (output_enabled == enabled &&
            (!enabled || pgood == enabled)) {
            return true;
        }

        vTaskDelay(poll_ticks);
    } while ((xTaskGetTickCount() - start_ticks) < timeout_ticks);

    return laser_controller_app_copy_status(status);
}

static bool laser_controller_comms_is_runtime_control_command(const char *command)
{
    /*
     * 2026-04-20: `operate.set_target`, `set_target_temp`, and
     * `set_target_lambda` are NO LONGER in this set. They stage the TEC
     * target and have no laser-fire semantics — the DeployBar in the iOS
     * app (and the host console Operate workspace) issues
     * `operate.set_target` BEFORE `deployment.enter` to lock the
     * operator's wavelength into the deployment snapshot. Blocking it
     * until ready-idle would break that arm gesture. Target staging while
     * `deployment.running` is still guarded below as a separate check
     * because mutating the target mid-checklist would drift the
     * TEC_SETTLE step.
     */
    return command != NULL &&
           (strcmp(command, "set_laser_power") == 0 ||
            strcmp(command, "set_max_current") == 0 ||
            strcmp(command, "operate.set_output") == 0 ||
            strcmp(command, "operate.set_modulation") == 0 ||
            strcmp(command, "laser_output_enable") == 0 ||
            strcmp(command, "laser_output_disable") == 0 ||
            strcmp(command, "configure_modulation") == 0);
}

/*
 * Target-staging commands. These only mutate bench TEC target (temp or
 * wavelength) — no laser fire. They are allowed pre-deployment so the host
 * / iOS app can lock the operator's staged wavelength before arming. They
 * are still rejected while `deployment.running` because mid-checklist
 * target drift would starve the TEC_SETTLE step.
 */
static bool laser_controller_comms_is_target_stage_command(const char *command)
{
    return command != NULL &&
           (strcmp(command, "set_target_temp") == 0 ||
            strcmp(command, "set_target_lambda") == 0 ||
            strcmp(command, "operate.set_target") == 0);
}

/*
 * Green alignment laser is NOW UNGATED at the software level (comms, safety,
 * derive_outputs). User directive 2026-04-14: "Green laser considered safe
 * to activate at ALL TIME, no interlock for it at all." Alignment commands
 * (`operate.set_alignment`, legacy `enable_alignment`/`disable_alignment`)
 * therefore no longer pass through the aux-control deployment gate. Only the
 * LED (GPIO6) retains aux gating because it still has real ownership
 * semantics with the Integrate service path.
 */
static bool laser_controller_comms_is_led_control_command(const char *command)
{
    return command != NULL &&
           strcmp(command, "operate.set_led") == 0;
}

static bool laser_controller_comms_runtime_mode_is_modulated_host(
    const laser_controller_runtime_status_t *status)
{
    return status != NULL &&
           status->bench.runtime_mode ==
               LASER_CONTROLLER_RUNTIME_MODE_MODULATED_HOST;
}

static const char *laser_controller_comms_runtime_mode_switch_block_reason(
    const laser_controller_runtime_status_t *status)
{
    if (status == NULL) {
        return "Controller status is unavailable.";
    }

    if (!status->deployment.active) {
        return "Enter deployment mode before changing runtime mode.";
    }

    if (status->deployment.running) {
        return "Wait for the deployment checklist to stop before changing runtime mode.";
    }

    if (status->fault_latched) {
        return "Clear the active fault before changing runtime mode.";
    }

    if (status->inputs.button.stage1_pressed ||
        status->inputs.button.stage2_pressed) {
        return "Release the trigger inputs before changing runtime mode.";
    }

    if (status->decision.nir_output_enable ||
        status->bench.requested_nir) {
        return "Clear the current NIR request before changing runtime mode.";
    }

    if (status->outputs.enable_alignment_laser ||
        status->bench.requested_alignment) {
        return "Clear the current alignment request before changing runtime mode.";
    }

    if (status->bench.modulation_enabled) {
        return "Disable PCN modulation before changing runtime mode.";
    }

    if (!status->outputs.select_driver_low_current) {
        return "Drive PCN low before changing runtime mode.";
    }

    return NULL;
}

static bool laser_controller_comms_is_service_mutation_command(const char *command)
{
    return command != NULL &&
           (strcmp(command, "clear_faults") == 0 ||
            strcmp(command, "enter_service_mode") == 0 ||
            strcmp(command, "set_runtime_safety") == 0 ||
            strcmp(command, "set_supply_enable") == 0 ||
            strcmp(command, "set_haptic_enable") == 0 ||
            strcmp(command, "tof_illumination_set") == 0 ||
            strcmp(command, "set_gpio_override") == 0 ||
            strcmp(command, "clear_gpio_overrides") == 0 ||
            strcmp(command, "pd_debug_config") == 0 ||
            strcmp(command, "pd_save_firmware_plan") == 0 ||
            strcmp(command, "pd_burn_nvm") == 0 ||
            strcmp(command, "apply_bringup_preset") == 0 ||
            strcmp(command, "set_profile_name") == 0 ||
            strcmp(command, "set_module_state") == 0 ||
            strcmp(command, "save_bringup_profile") == 0 ||
            strcmp(command, "dac_debug_set") == 0 ||
            strcmp(command, "dac_debug_config") == 0 ||
            strcmp(command, "imu_debug_config") == 0 ||
            strcmp(command, "tof_debug_config") == 0 ||
            strcmp(command, "haptic_debug_config") == 0 ||
            strcmp(command, "haptic_external_trigger_pattern") == 0 ||
            strcmp(command, "haptic_debug_fire") == 0 ||
            strcmp(command, "i2c_write") == 0 ||
            strcmp(command, "spi_write") == 0 ||
            strcmp(command, "set_interlocks_disabled") == 0 ||
            strcmp(command, "service.usb_debug_mock_enable") == 0 ||
            strcmp(command, "service.usb_debug_mock_disable") == 0 ||
            strcmp(command, "integrate.rgb_led.set") == 0 ||
            strcmp(command, "integrate.rgb_led.clear") == 0 ||
            strcmp(command, "integrate.tof.set_calibration") == 0);
}

/*
 * "driver_standby_effective" is a BACKWARD-COMPATIBLE bool for legacy status
 * readers. It reports TRUE whenever the driver is NOT in OPERATE mode — i.e.
 * either shutdown (SBDN driven LOW) or standby (Hi-Z). Returns FALSE only
 * when SBDN is explicitly HIGH.
 *
 * New consumers should read `status->outputs.sbdn_state` directly to
 * distinguish OFF vs STANDBY vs ON.
 */
static bool laser_controller_comms_driver_standby_effective(
    const laser_controller_runtime_status_t *status)
{
    const laser_controller_board_gpio_pin_readback_t *pin =
        laser_controller_comms_find_gpio_pin(status, LASER_CONTROLLER_GPIO_LD_SBDN);

    if (pin == NULL) {
        return status != NULL &&
               status->outputs.sbdn_state !=
                   LASER_CONTROLLER_SBDN_STATE_ON;
    }

    if (pin->override_active &&
        pin->override_mode == LASER_CONTROLLER_SERVICE_GPIO_MODE_OUTPUT) {
        return !pin->level_high;
    }

    if (!pin->override_active && pin->output_enabled) {
        return !pin->level_high;
    }

    /*
     * Pin is in Hi-Z or undriven. Treat as "standby effective" — the
     * driver is not in OPERATE because it has no logic-high on SBDN. The
     * enum on the status struct distinguishes STANDBY (intended Hi-Z) from
     * OFF (driven low).
     */
    return status->outputs.sbdn_state != LASER_CONTROLLER_SBDN_STATE_ON;
}

static const laser_controller_board_gpio_pin_readback_t *
laser_controller_comms_find_gpio_pin(
    const laser_controller_runtime_status_t *status,
    uint32_t gpio_num)
{
    if (status == NULL) {
        return NULL;
    }

    for (size_t index = 0U;
         index < LASER_CONTROLLER_BOARD_GPIO_INSPECTOR_PIN_COUNT;
         ++index) {
        const laser_controller_board_gpio_pin_readback_t *pin =
            &status->inputs.gpio_inspector.pins[index];

        if ((uint32_t)pin->gpio_num == gpio_num) {
            return pin;
        }
    }

    return NULL;
}

static bool laser_controller_comms_gpio_override_matches(
    const laser_controller_board_gpio_pin_readback_t *pin,
    laser_controller_service_gpio_mode_t mode,
    bool level_high,
    bool pullup_enabled,
    bool pulldown_enabled)
{
    if (pin == NULL) {
        return false;
    }

    if (mode == LASER_CONTROLLER_SERVICE_GPIO_MODE_FIRMWARE) {
        return !pin->override_active;
    }

    if (!pin->override_active || pin->override_mode != mode) {
        return false;
    }

    if (pin->override_pullup_enabled != pullup_enabled ||
        pin->override_pulldown_enabled != pulldown_enabled) {
        return false;
    }

    if (mode == LASER_CONTROLLER_SERVICE_GPIO_MODE_OUTPUT) {
        return pin->override_level_high == level_high &&
               pin->output_enabled &&
               pin->level_high == level_high;
    }

    return pin->input_enabled &&
           !pin->output_enabled &&
           pin->pullup_enabled == pullup_enabled &&
           pin->pulldown_enabled == pulldown_enabled;
}

static bool laser_controller_comms_wait_for_gpio_override(
    uint32_t gpio_num,
    laser_controller_service_gpio_mode_t mode,
    bool level_high,
    bool pullup_enabled,
    bool pulldown_enabled,
    laser_controller_runtime_status_t *status)
{
    const TickType_t start_ticks = xTaskGetTickCount();
    const TickType_t timeout_ticks =
        pdMS_TO_TICKS(LASER_CONTROLLER_GPIO_OVERRIDE_WAIT_MS);
    const TickType_t poll_ticks =
        pdMS_TO_TICKS(LASER_CONTROLLER_SERVICE_MODE_POLL_MS);

    if (status == NULL) {
        return false;
    }

    do {
        if (!laser_controller_app_copy_status(status)) {
            return false;
        }

        if (laser_controller_comms_gpio_override_matches(
                laser_controller_comms_find_gpio_pin(status, gpio_num),
                mode,
                level_high,
                pullup_enabled,
                pulldown_enabled)) {
            return true;
        }

        vTaskDelay(poll_ticks);
    } while ((xTaskGetTickCount() - start_ticks) < timeout_ticks);

    return laser_controller_app_copy_status(status) &&
           laser_controller_comms_gpio_override_matches(
               laser_controller_comms_find_gpio_pin(status, gpio_num),
               mode,
               level_high,
               pullup_enabled,
               pulldown_enabled);
}

static bool laser_controller_comms_wait_for_gpio_override_clear(
    laser_controller_runtime_status_t *status)
{
    const TickType_t start_ticks = xTaskGetTickCount();
    const TickType_t timeout_ticks =
        pdMS_TO_TICKS(LASER_CONTROLLER_GPIO_OVERRIDE_WAIT_MS);
    const TickType_t poll_ticks =
        pdMS_TO_TICKS(LASER_CONTROLLER_SERVICE_MODE_POLL_MS);

    if (status == NULL) {
        return false;
    }

    do {
        if (!laser_controller_app_copy_status(status)) {
            return false;
        }

        if (!status->inputs.gpio_inspector.any_override_active) {
            return true;
        }

        vTaskDelay(poll_ticks);
    } while ((xTaskGetTickCount() - start_ticks) < timeout_ticks);

    return laser_controller_app_copy_status(status) &&
           !status->inputs.gpio_inspector.any_override_active;
}

static void laser_controller_comms_write_deployment_steps_json(
    laser_controller_comms_buffer_t *buffer,
    const laser_controller_runtime_status_t *status)
{
    if (buffer == NULL || status == NULL) {
        return;
    }

    laser_controller_comms_buffer_append_raw(buffer, "[");
    for (uint32_t index = 1U;
         index < LASER_CONTROLLER_DEPLOYMENT_STEP_COUNT;
         ++index) {
        const laser_controller_deployment_step_t step =
            (laser_controller_deployment_step_t)index;

        if (index > 1U) {
            laser_controller_comms_buffer_append_raw(buffer, ",");
        }

        laser_controller_comms_buffer_append_raw(buffer, "{");
        laser_controller_comms_buffer_append_raw(buffer, "\"key\":");
        laser_controller_comms_write_escaped_string(
            buffer,
            laser_controller_deployment_step_name(step));
        laser_controller_comms_buffer_append_raw(buffer, ",\"label\":");
        laser_controller_comms_write_escaped_string(
            buffer,
            laser_controller_deployment_step_label(step));
        laser_controller_comms_buffer_append_raw(buffer, ",\"status\":");
        laser_controller_comms_write_escaped_string(
            buffer,
            laser_controller_deployment_step_status_name(
                status->deployment.step_status[index]));
        laser_controller_comms_buffer_append_fmt(
            buffer,
            ",\"startedAtMs\":%lu,\"completedAtMs\":%lu",
            (unsigned long)status->deployment.step_started_at_ms[index],
            (unsigned long)status->deployment.step_completed_at_ms[index]);
        laser_controller_comms_buffer_append_raw(buffer, "}");
    }
    laser_controller_comms_buffer_append_raw(buffer, "]");
}

static void laser_controller_comms_write_deployment_secondary_effects_json(
    laser_controller_comms_buffer_t *buffer,
    const laser_controller_runtime_status_t *status)
{
    if (buffer == NULL || status == NULL) {
        return;
    }

    laser_controller_comms_buffer_append_raw(buffer, "[");
    for (uint32_t index = 0U;
         index < status->deployment.secondary_effect_count;
         ++index) {
        const laser_controller_deployment_secondary_effect_t *effect =
            &status->deployment.secondary_effects[index];

        if (index > 0U) {
            laser_controller_comms_buffer_append_raw(buffer, ",");
        }

        laser_controller_comms_buffer_append_raw(buffer, "{");
        laser_controller_comms_buffer_append_raw(buffer, "\"code\":");
        laser_controller_comms_write_escaped_string(
            buffer,
            laser_controller_fault_code_name(effect->code));
        laser_controller_comms_buffer_append_raw(buffer, ",\"reason\":");
        laser_controller_comms_write_escaped_string(buffer, effect->reason);
        laser_controller_comms_buffer_append_fmt(
            buffer,
            ",\"atMs\":%lu}",
            (unsigned long)effect->at_ms);
    }
    laser_controller_comms_buffer_append_raw(buffer, "]");
}

static void laser_controller_comms_write_deployment_json(
    laser_controller_comms_buffer_t *buffer,
    const laser_controller_runtime_status_t *status,
    bool include_steps)
{
    if (buffer == NULL || status == NULL) {
        return;
    }

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"deployment\":{\"active\":%s,\"running\":%s,\"ready\":%s,\"readyIdle\":%s,\"readyQualified\":%s,\"readyInvalidated\":%s,\"failed\":%s,\"phase\":",
        status->deployment.active ? "true" : "false",
        status->deployment.running ? "true" : "false",
        status->deployment.ready ? "true" : "false",
        status->deployment.ready_idle ? "true" : "false",
        status->deployment.ready_qualified ? "true" : "false",
        status->deployment.ready_invalidated ? "true" : "false",
        status->deployment.failed ? "true" : "false");
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_deployment_phase_name(status->deployment.phase));
    laser_controller_comms_buffer_append_fmt(
        buffer,
        ",\"sequenceId\":%lu,\"currentStep\":",
        (unsigned long)status->deployment.sequence_id);
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_deployment_step_name(status->deployment.current_step));
    laser_controller_comms_buffer_append_raw(buffer, ",\"currentStepKey\":");
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_deployment_step_name(status->deployment.current_step));
    laser_controller_comms_buffer_append_fmt(
        buffer,
        ",\"currentStepIndex\":%lu",
        (unsigned long)status->deployment.current_step);
    laser_controller_comms_buffer_append_raw(buffer, ",\"lastCompletedStep\":");
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_deployment_step_name(
            status->deployment.last_completed_step));
    laser_controller_comms_buffer_append_raw(buffer, ",\"lastCompletedStepKey\":");
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_deployment_step_name(
            status->deployment.last_completed_step));
    laser_controller_comms_buffer_append_raw(buffer, ",\"failureCode\":");
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_fault_code_name(status->deployment.failure_code));
    laser_controller_comms_buffer_append_raw(buffer, ",\"failureReason\":");
    laser_controller_comms_write_escaped_string(
        buffer,
        status->deployment.failure_reason);
    laser_controller_comms_buffer_append_raw(buffer, ",\"primaryFailureCode\":");
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_fault_code_name(status->deployment.primary_failure_code));
    laser_controller_comms_buffer_append_raw(buffer, ",\"primaryFailureReason\":");
    laser_controller_comms_write_escaped_string(
        buffer,
        status->deployment.primary_failure_reason);
    laser_controller_comms_buffer_append_raw(buffer, ",\"secondaryEffects\":");
    laser_controller_comms_write_deployment_secondary_effects_json(buffer, status);
    laser_controller_comms_buffer_append_raw(buffer, ",\"targetMode\":");
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_deployment_target_mode_name(
            status->deployment.target.target_mode));
    laser_controller_comms_buffer_append_fmt(
        buffer,
        ",\"targetTempC\":%.2f,\"targetLambdaNm\":%.2f,\"maxLaserCurrentA\":%.3f,\"maxOpticalPowerW\":%.3f",
        status->deployment.target.target_temp_c,
        status->deployment.target.target_lambda_nm,
        status->deployment.max_laser_current_a,
        status->deployment.max_optical_power_w);
    laser_controller_comms_buffer_append_fmt(
        buffer,
        ",\"readyTruth\":{\"tecRailPgoodRaw\":%s,\"tecRailPgoodFiltered\":%s,\"tecTempGood\":%s,\"tecAnalogPlausible\":%s,\"ldRailPgoodRaw\":%s,\"ldRailPgoodFiltered\":%s,\"driverLoopGood\":%s,\"sbdnHigh\":%s,\"pcnLow\":%s,\"idleBiasCurrentA\":%.3f}",
        status->deployment.ready_truth.tec_rail_pgood_raw ? "true" : "false",
        status->deployment.ready_truth.tec_rail_pgood_filtered ? "true" : "false",
        status->deployment.ready_truth.tec_temp_good ? "true" : "false",
        status->deployment.ready_truth.tec_analog_plausible ? "true" : "false",
        status->deployment.ready_truth.ld_rail_pgood_raw ? "true" : "false",
        status->deployment.ready_truth.ld_rail_pgood_filtered ? "true" : "false",
        status->deployment.ready_truth.driver_loop_good ? "true" : "false",
        status->deployment.ready_truth.sbdn_high ? "true" : "false",
        status->deployment.ready_truth.pcn_low ? "true" : "false",
        status->deployment.ready_truth.idle_bias_current_a);
    if (include_steps) {
        laser_controller_comms_buffer_append_raw(buffer, ",\"steps\":");
        laser_controller_comms_write_deployment_steps_json(buffer, status);
    }
    laser_controller_comms_buffer_append_raw(buffer, "},");
}

static void laser_controller_comms_write_snapshot_json(
    laser_controller_comms_buffer_t *buffer,
    const laser_controller_runtime_status_t *status)
{
    const esp_app_desc_t *app_desc = esp_app_get_description();
    const laser_controller_firmware_signature_t *signature =
        laser_controller_signature_get();
    const esp_reset_reason_t reset_reason = esp_reset_reason();
    const float beam_pitch_deg =
        status->inputs.beam_pitch_rad * LASER_CONTROLLER_DEG_PER_RAD;
    const float beam_roll_deg =
        status->inputs.beam_roll_rad * LASER_CONTROLLER_DEG_PER_RAD;
    const float beam_yaw_deg =
        status->inputs.beam_yaw_rad * LASER_CONTROLLER_DEG_PER_RAD;
    const float beam_pitch_limit_deg =
        status->config.thresholds.horizon_threshold_rad * LASER_CONTROLLER_DEG_PER_RAD;
    const float beam_pitch_hysteresis_deg =
        status->config.thresholds.horizon_hysteresis_rad * LASER_CONTROLLER_DEG_PER_RAD;
    const float actual_lambda_nm =
        laser_controller_comms_actual_lambda_nm(status);
    const float lambda_drift_nm =
        laser_controller_comms_lambda_drift_nm(status);
    laser_controller_wireless_status_t wireless = { 0 };

    laser_controller_wireless_copy_status(&wireless);

    laser_controller_comms_buffer_append_raw(buffer, "{");
    laser_controller_comms_buffer_append_raw(buffer, "\"identity\":{");
    laser_controller_comms_buffer_append_raw(buffer, "\"label\":");
    laser_controller_comms_write_escaped_string(
        buffer,
        signature != NULL ? signature->product_name : LASER_CONTROLLER_FIRMWARE_PRODUCT_NAME);
    laser_controller_comms_buffer_append_raw(buffer, ",");
    laser_controller_comms_buffer_append_raw(buffer, "\"firmwareVersion\":");
    laser_controller_comms_write_escaped_string(
        buffer,
        signature != NULL ? signature->firmware_version : app_desc->version);
    laser_controller_comms_buffer_append_fmt(
        buffer,
        ",\"hardwareRevision\":\"rev-%lu\",\"serialNumber\":",
        (unsigned long)status->config.hardware_revision);
    laser_controller_comms_write_escaped_string(buffer, status->config.serial_number);
    laser_controller_comms_buffer_append_raw(buffer, ",\"protocolVersion\":");
    laser_controller_comms_write_escaped_string(
        buffer,
        signature != NULL ? signature->protocol_version :
                            LASER_CONTROLLER_FIRMWARE_PROTOCOL_VERSION);
    laser_controller_comms_buffer_append_raw(buffer, ",\"boardName\":");
    laser_controller_comms_write_escaped_string(
        buffer,
        signature != NULL ? signature->board_name : "unknown");
    laser_controller_comms_buffer_append_raw(buffer, ",\"buildUtc\":");
    laser_controller_comms_write_escaped_string(
        buffer,
        signature != NULL ? signature->build_utc : "unknown");
    laser_controller_comms_buffer_append_raw(buffer, "},");

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"session\":{\"uptimeSeconds\":%lu,\"state\":",
        (unsigned long)(status->uptime_ms / 1000U));
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_state_name(status->state));
    laser_controller_comms_buffer_append_raw(buffer, ",\"powerTier\":");
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_comms_power_tier_name(status->power_tier));
    laser_controller_comms_buffer_append_raw(buffer, ",\"bootReason\":");
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_comms_reset_reason_name(reset_reason));
    laser_controller_comms_buffer_append_raw(
        buffer,
        ",\"connectedAtIso\":\"1970-01-01T00:00:00Z\"},");

    laser_controller_comms_buffer_append_raw(buffer, "\"wireless\":");
    laser_controller_comms_write_wireless_json(buffer, &wireless);
    laser_controller_comms_buffer_append_raw(buffer, ",");

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"pd\":{\"contractValid\":%s,\"negotiatedPowerW\":%.2f,\"sourceVoltageV\":%.2f,\"sourceCurrentA\":%.2f,\"operatingCurrentA\":%.2f,\"contractObjectPosition\":%u,\"sinkProfileCount\":%u,\"sinkProfiles\":[",
        status->inputs.pd_contract_valid ? "true" : "false",
        status->inputs.pd_negotiated_power_w,
        status->inputs.pd_source_voltage_v,
        status->inputs.pd_source_current_a,
        status->inputs.pd_operating_current_a,
        (unsigned int)status->inputs.pd_contract_object_position,
        (unsigned int)status->inputs.pd_sink_profile_count);
    for (size_t index = 0U;
         index < LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT;
         ++index) {
        if (index > 0U) {
            laser_controller_comms_buffer_append_raw(buffer, ",");
        }
        laser_controller_comms_write_pd_profile_json(
            buffer,
            &status->inputs.pd_sink_profiles[index]);
    }
    laser_controller_comms_buffer_append_fmt(
        buffer,
        "],\"sourceIsHostOnly\":%s,\"lastUpdatedMs\":%lu,\"snapshotFresh\":%s,\"source\":",
        status->inputs.pd_source_is_host_only ? "true" : "false",
        (unsigned long)status->inputs.pd_last_updated_ms,
        status->inputs.pd_snapshot_fresh ? "true" : "false");
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_comms_pd_snapshot_source_name(
            status->inputs.pd_source));
    laser_controller_comms_buffer_append_raw(buffer, "},");

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"rails\":{\"ld\":{\"enabled\":%s,\"pgood\":%s},\"tec\":{\"enabled\":%s,\"pgood\":%s}},",
        status->outputs.enable_ld_vin ? "true" : "false",
        status->inputs.ld_rail_pgood ? "true" : "false",
        status->outputs.enable_tec_vin ? "true" : "false",
        status->inputs.tec_rail_pgood ? "true" : "false");

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"imu\":{\"valid\":%s,\"fresh\":%s,\"beamPitchDeg\":%.2f,\"beamRollDeg\":%.2f,\"beamYawDeg\":%.2f,\"beamYawRelative\":true,\"beamPitchLimitDeg\":%.2f},",
        status->inputs.imu_data_valid ? "true" : "false",
        status->inputs.imu_data_fresh ? "true" : "false",
        beam_pitch_deg,
        beam_roll_deg,
        beam_yaw_deg,
        beam_pitch_limit_deg);

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"tof\":{\"valid\":%s,\"fresh\":%s,\"distanceM\":%.3f,\"minRangeM\":%.3f,\"maxRangeM\":%.3f},",
        status->inputs.tof_data_valid ? "true" : "false",
        status->inputs.tof_data_fresh ? "true" : "false",
        status->inputs.tof_distance_m,
        status->config.thresholds.tof_min_range_m,
        status->config.thresholds.tof_max_range_m);

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"laser\":{\"alignmentEnabled\":%s,\"nirEnabled\":%s,\"driverStandby\":%s,\"telemetryValid\":%s,\"commandVoltageV\":%.3f,\"commandedCurrentA\":%.3f,\"currentMonitorVoltageV\":%.3f,\"measuredCurrentA\":%.3f,\"loopGood\":%s,\"driverTempVoltageV\":%.3f,\"driverTempC\":%.2f},",
        status->outputs.enable_alignment_laser ? "true" : "false",
        status->decision.nir_output_enable ? "true" : "false",
        laser_controller_comms_driver_standby_effective(status) ? "true" : "false",
        status->inputs.ld_telemetry_valid ? "true" : "false",
        laser_controller_comms_effective_ld_command_voltage_v(status),
        laser_controller_comms_effective_commanded_current_a(status),
        status->inputs.laser_current_monitor_voltage_v,
        status->inputs.measured_laser_current_a,
        status->inputs.driver_loop_good ? "true" : "false",
        status->inputs.laser_driver_temp_voltage_v,
        status->inputs.laser_driver_temp_c);

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"tec\":{\"targetTempC\":%.2f,\"targetLambdaNm\":%.2f,\"actualLambdaNm\":%.2f,\"telemetryValid\":%s,\"commandVoltageV\":%.3f,\"tempGood\":%s,\"tempC\":%.2f,\"tempAdcVoltageV\":%.3f,\"currentA\":%.2f,\"voltageV\":%.2f,\"settlingSecondsRemaining\":%u},",
        status->bench.target_temp_c,
        status->bench.target_lambda_nm,
        actual_lambda_nm,
        status->inputs.tec_telemetry_valid ? "true" : "false",
        status->inputs.tec_command_voltage_v,
        status->inputs.tec_temp_good ? "true" : "false",
        status->inputs.tec_temp_c,
        status->inputs.tec_temp_adc_voltage_v,
        status->inputs.tec_current_a,
        status->inputs.tec_voltage_v,
        status->inputs.tec_temp_good ? 0U : 1U);

    laser_controller_comms_write_button_state_json(buffer, status);
    laser_controller_comms_write_button_board_json(buffer, status);
    laser_controller_comms_write_peripheral_readback_json(buffer, status);
    laser_controller_comms_write_gpio_inspector_json(buffer, status);

    laser_controller_comms_write_bench_json(buffer, status);
    laser_controller_comms_buffer_append_raw(buffer, ",");

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"safety\":{\"allowAlignment\":%s,\"allowNir\":%s,\"horizonBlocked\":%s,\"distanceBlocked\":%s,\"lambdaDriftBlocked\":%s,\"tecTempAdcBlocked\":%s,\"horizonThresholdDeg\":%.2f,\"horizonHysteresisDeg\":%.2f,\"tofMinRangeM\":%.3f,\"tofMaxRangeM\":%.3f,\"tofHysteresisM\":%.3f,\"imuStaleMs\":%lu,\"tofStaleMs\":%lu,\"railGoodTimeoutMs\":%lu,\"lambdaDriftLimitNm\":%.2f,\"lambdaDriftHysteresisNm\":%.2f,\"lambdaDriftHoldMs\":%lu,\"ldOvertempLimitC\":%.2f,\"tecTempAdcTripV\":%.3f,\"tecTempAdcHysteresisV\":%.3f,\"tecTempAdcHoldMs\":%lu,\"tecMinCommandC\":%.2f,\"tecMaxCommandC\":%.2f,\"tecReadyToleranceC\":%.2f,\"maxLaserCurrentA\":%.2f,\"offCurrentThresholdA\":%.2f,\"maxTofLedDutyCyclePct\":%u,\"lioVoltageOffsetV\":%.4f,\"actualLambdaNm\":%.2f,\"targetLambdaNm\":%.2f,\"lambdaDriftNm\":%.2f,\"tempAdcVoltageV\":%.3f,\"interlocks\":{\"horizonEnabled\":%s,\"distanceEnabled\":%s,\"lambdaDriftEnabled\":%s,\"tecTempAdcEnabled\":%s,\"imuInvalidEnabled\":%s,\"imuStaleEnabled\":%s,\"tofInvalidEnabled\":%s,\"tofStaleEnabled\":%s,\"ldOvertempEnabled\":%s,\"ldLoopBadEnabled\":%s,\"tofLowBoundOnly\":%s},\"autoDeployOnBoot\":%s},",
        status->decision.allow_alignment ? "true" : "false",
        status->decision.allow_nir ? "true" : "false",
        status->decision.horizon_blocked ? "true" : "false",
        status->decision.distance_blocked ? "true" : "false",
        status->decision.lambda_drift_blocked ? "true" : "false",
        status->decision.tec_temp_adc_blocked ? "true" : "false",
        beam_pitch_limit_deg,
        beam_pitch_hysteresis_deg,
        status->config.thresholds.tof_min_range_m,
        status->config.thresholds.tof_max_range_m,
        status->config.thresholds.tof_hysteresis_m,
        (unsigned long)status->config.timeouts.imu_stale_ms,
        (unsigned long)status->config.timeouts.tof_stale_ms,
        (unsigned long)status->config.timeouts.rail_good_timeout_ms,
        status->config.thresholds.lambda_drift_limit_nm,
        status->config.thresholds.lambda_drift_hysteresis_nm,
        (unsigned long)status->config.timeouts.lambda_drift_hold_ms,
        status->config.thresholds.ld_overtemp_limit_c,
        status->config.thresholds.tec_temp_adc_trip_v,
        status->config.thresholds.tec_temp_adc_hysteresis_v,
        (unsigned long)status->config.timeouts.tec_temp_adc_hold_ms,
        status->config.thresholds.tec_min_command_c,
        status->config.thresholds.tec_max_command_c,
        status->config.thresholds.tec_ready_tolerance_c,
        status->config.thresholds.max_laser_current_a,
        status->config.thresholds.off_current_threshold_a,
        (unsigned)status->config.thresholds.max_tof_led_duty_cycle_pct,
        (double)status->config.thresholds.lio_voltage_offset_v,
        actual_lambda_nm,
        status->bench.target_lambda_nm,
        lambda_drift_nm,
        status->inputs.tec_temp_adc_voltage_v,
        status->config.thresholds.interlocks.horizon_enabled ? "true" : "false",
        status->config.thresholds.interlocks.distance_enabled ? "true" : "false",
        status->config.thresholds.interlocks.lambda_drift_enabled ? "true" : "false",
        status->config.thresholds.interlocks.tec_temp_adc_enabled ? "true" : "false",
        status->config.thresholds.interlocks.imu_invalid_enabled ? "true" : "false",
        status->config.thresholds.interlocks.imu_stale_enabled ? "true" : "false",
        status->config.thresholds.interlocks.tof_invalid_enabled ? "true" : "false",
        status->config.thresholds.interlocks.tof_stale_enabled ? "true" : "false",
        status->config.thresholds.interlocks.ld_overtemp_enabled ? "true" : "false",
        status->config.thresholds.interlocks.ld_loop_bad_enabled ? "true" : "false",
        status->config.thresholds.interlocks.tof_low_bound_only ? "true" : "false",
        (status->config.service_flags &
         LASER_CONTROLLER_SERVICE_FLAG_AUTO_DEPLOY_ON_BOOT) != 0U ?
            "true" : "false");

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"bringup\":{\"serviceModeRequested\":%s,\"serviceModeActive\":%s,\"interlocksDisabled\":%s,\"persistenceDirty\":%s,\"persistenceAvailable\":%s,\"lastSaveOk\":%s,\"lastSaveAtMs\":%lu,\"profileRevision\":%lu,\"profileName\":",
        status->bringup.service_mode_requested ? "true" : "false",
        status->state == LASER_CONTROLLER_STATE_SERVICE_MODE ? "true" : "false",
        status->bringup.interlocks_disabled ? "true" : "false",
        status->bringup.persistence_dirty ? "true" : "false",
        status->bringup.persistence_available ? "true" : "false",
        status->bringup.last_save_ok ? "true" : "false",
        (unsigned long)status->bringup.last_save_at_ms,
        (unsigned long)status->bringup.profile_revision);
    laser_controller_comms_write_escaped_string(buffer, status->bringup.profile_name);
    laser_controller_comms_buffer_append_fmt(
        buffer,
        ",\"power\":{\"ldRequested\":%s,\"tecRequested\":%s},\"illumination\":{\"tof\":{\"enabled\":%s,\"dutyCyclePct\":%lu,\"frequencyHz\":%lu}},\"modules\":{",
        status->bringup.ld_rail_debug_enabled ? "true" : "false",
        status->bringup.tec_rail_debug_enabled ? "true" : "false",
        status->bringup.tof_illumination_enabled ? "true" : "false",
        (unsigned long)status->bringup.tof_illumination_duty_cycle_pct,
        (unsigned long)status->bringup.tof_illumination_frequency_hz);
    laser_controller_comms_buffer_append_raw(buffer, "\"imu\":");
    laser_controller_comms_write_module_json(
        buffer,
        &status->bringup.modules[LASER_CONTROLLER_MODULE_IMU]);
    laser_controller_comms_buffer_append_raw(buffer, ",\"dac\":");
    laser_controller_comms_write_module_json(
        buffer,
        &status->bringup.modules[LASER_CONTROLLER_MODULE_DAC]);
    laser_controller_comms_buffer_append_raw(buffer, ",\"haptic\":");
    laser_controller_comms_write_module_json(
        buffer,
        &status->bringup.modules[LASER_CONTROLLER_MODULE_HAPTIC]);
    laser_controller_comms_buffer_append_raw(buffer, ",\"tof\":");
    laser_controller_comms_write_module_json(
        buffer,
        &status->bringup.modules[LASER_CONTROLLER_MODULE_TOF]);
    laser_controller_comms_buffer_append_raw(buffer, ",\"buttons\":");
    laser_controller_comms_write_module_json(
        buffer,
        &status->bringup.modules[LASER_CONTROLLER_MODULE_BUTTONS]);
    laser_controller_comms_buffer_append_raw(buffer, ",\"pd\":");
    laser_controller_comms_write_module_json(
        buffer,
        &status->bringup.modules[LASER_CONTROLLER_MODULE_PD]);
    laser_controller_comms_buffer_append_raw(buffer, ",\"laserDriver\":");
    laser_controller_comms_write_module_json(
        buffer,
        &status->bringup.modules[LASER_CONTROLLER_MODULE_LASER_DRIVER]);
    laser_controller_comms_buffer_append_raw(buffer, ",\"tec\":");
    laser_controller_comms_write_module_json(
        buffer,
        &status->bringup.modules[LASER_CONTROLLER_MODULE_TEC]);
    laser_controller_comms_buffer_append_fmt(
        buffer,
        "},\"tuning\":{\"dacLdChannelV\":%.3f,\"dacTecChannelV\":%.3f,\"dacReferenceMode\":",
        status->bringup.dac_ld_channel_v,
        status->bringup.dac_tec_channel_v);
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_service_dac_reference_name(status->bringup.dac_reference));
    laser_controller_comms_buffer_append_fmt(
        buffer,
        ",\"dacGain2x\":%s,\"dacRefDiv\":%s,\"dacSyncMode\":",
        status->bringup.dac_gain_2x ? "true" : "false",
        status->bringup.dac_ref_div ? "true" : "false");
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_service_dac_sync_mode_name(status->bringup.dac_sync_mode));
    laser_controller_comms_buffer_append_fmt(
        buffer,
        ",\"imuOdrHz\":%lu,\"imuAccelRangeG\":%lu,\"imuGyroRangeDps\":%lu,\"imuGyroEnabled\":%s,\"imuLpf2Enabled\":%s,\"imuTimestampEnabled\":%s,\"imuBduEnabled\":%s,\"imuIfIncEnabled\":%s,\"imuI2cDisabled\":%s,\"tofMinRangeM\":%.3f,\"tofMaxRangeM\":%.3f,\"tofStaleTimeoutMs\":%lu,\"tofCalibration\":{\"distanceMode\":\"%s\",\"timingBudgetMs\":%lu,\"roiWidthSpads\":%u,\"roiHeightSpads\":%u,\"roiCenterSpad\":%u,\"offsetMm\":%ld,\"xtalkCps\":%lu,\"xtalkEnabled\":%s},\"pdProfiles\":[",
        (unsigned long)status->bringup.imu_odr_hz,
        (unsigned long)status->bringup.imu_accel_range_g,
        (unsigned long)status->bringup.imu_gyro_range_dps,
        status->bringup.imu_gyro_enabled ? "true" : "false",
        status->bringup.imu_lpf2_enabled ? "true" : "false",
        status->bringup.imu_timestamp_enabled ? "true" : "false",
        status->bringup.imu_bdu_enabled ? "true" : "false",
        status->bringup.imu_if_inc_enabled ? "true" : "false",
        status->bringup.imu_i2c_disabled ? "true" : "false",
        status->bringup.tof_min_range_m,
        status->bringup.tof_max_range_m,
        (unsigned long)status->bringup.tof_stale_timeout_ms,
        status->bringup.tof_calibration.distance_mode ==
            LASER_CONTROLLER_TOF_DISTANCE_MODE_SHORT ? "short" :
        status->bringup.tof_calibration.distance_mode ==
            LASER_CONTROLLER_TOF_DISTANCE_MODE_MEDIUM ? "medium" : "long",
        (unsigned long)status->bringup.tof_calibration.timing_budget_ms,
        (unsigned)status->bringup.tof_calibration.roi_width_spads,
        (unsigned)status->bringup.tof_calibration.roi_height_spads,
        (unsigned)status->bringup.tof_calibration.roi_center_spad,
        (long)status->bringup.tof_calibration.offset_mm,
        (unsigned long)status->bringup.tof_calibration.xtalk_cps,
        status->bringup.tof_calibration.xtalk_enabled ? "true" : "false");
    for (size_t index = 0U;
         index < LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT;
         ++index) {
        if (index > 0U) {
            laser_controller_comms_buffer_append_raw(buffer, ",");
        }
        laser_controller_comms_write_pd_profile_json(
            buffer,
            &status->bringup.pd_profiles[index]);
    }
    laser_controller_comms_buffer_append_fmt(
        buffer,
        "],\"pdProgrammingOnlyMaxW\":%.2f,\"pdReducedModeMinW\":%.2f,\"pdReducedModeMaxW\":%.2f,\"pdFullModeMinW\":%.2f,\"pdFirmwarePlanEnabled\":%s,\"hapticEffectId\":%lu,\"hapticMode\":",
        status->bringup.pd_programming_only_max_w,
        status->bringup.pd_reduced_mode_min_w,
        status->bringup.pd_reduced_mode_max_w,
        status->bringup.pd_full_mode_min_w,
        status->bringup.pd_firmware_plan_enabled ? "true" : "false",
        (unsigned long)status->bringup.haptic_effect_id);
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_service_haptic_mode_name(status->bringup.haptic_mode));
    laser_controller_comms_buffer_append_fmt(
        buffer,
        ",\"hapticLibrary\":%lu,\"hapticActuator\":",
        (unsigned long)status->bringup.haptic_library);
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_service_haptic_actuator_name(status->bringup.haptic_actuator));
    laser_controller_comms_buffer_append_fmt(
        buffer,
        ",\"hapticRtpLevel\":%lu},\"tools\":{\"lastI2cScan\":",
        (unsigned long)status->bringup.haptic_rtp_level);
    laser_controller_comms_write_escaped_string(buffer, status->bringup.last_i2c_scan);
    laser_controller_comms_buffer_append_raw(buffer, ",\"lastI2cOp\":");
    laser_controller_comms_write_escaped_string(buffer, status->bringup.last_i2c_op);
    laser_controller_comms_buffer_append_raw(buffer, ",\"lastSpiOp\":");
    laser_controller_comms_write_escaped_string(buffer, status->bringup.last_spi_op);
    laser_controller_comms_buffer_append_raw(buffer, ",\"lastAction\":");
    laser_controller_comms_write_escaped_string(buffer, status->bringup.last_action);
    laser_controller_comms_buffer_append_raw(buffer, "}},");

    laser_controller_comms_write_deployment_json(buffer, status, true);

    laser_controller_comms_write_fault_json(buffer, status, true);
    laser_controller_comms_buffer_append_raw(buffer, ",");

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"counters\":{\"commsTimeouts\":0,\"watchdogTrips\":%u,\"brownouts\":%u}}",
        reset_reason == ESP_RST_TASK_WDT ||
                reset_reason == ESP_RST_INT_WDT || reset_reason == ESP_RST_WDT
            ? 1U
            : 0U,
        reset_reason == ESP_RST_BROWNOUT ? 1U : 0U);
}

static void laser_controller_comms_write_command_snapshot_json(
    laser_controller_comms_buffer_t *buffer,
    const laser_controller_runtime_status_t *status)
{
    const esp_reset_reason_t reset_reason = esp_reset_reason();
    const float beam_pitch_deg =
        status->inputs.beam_pitch_rad * LASER_CONTROLLER_DEG_PER_RAD;
    const float beam_roll_deg =
        status->inputs.beam_roll_rad * LASER_CONTROLLER_DEG_PER_RAD;
    const float beam_yaw_deg =
        status->inputs.beam_yaw_rad * LASER_CONTROLLER_DEG_PER_RAD;
    const float beam_pitch_limit_deg =
        status->config.thresholds.horizon_threshold_rad * LASER_CONTROLLER_DEG_PER_RAD;
    const float beam_pitch_hysteresis_deg =
        status->config.thresholds.horizon_hysteresis_rad * LASER_CONTROLLER_DEG_PER_RAD;
    const float actual_lambda_nm =
        laser_controller_comms_actual_lambda_nm(status);
    const float lambda_drift_nm =
        laser_controller_comms_lambda_drift_nm(status);
    laser_controller_wireless_status_t wireless = { 0 };

    laser_controller_wireless_copy_status(&wireless);

    laser_controller_comms_buffer_append_raw(buffer, "{");
    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"session\":{\"uptimeSeconds\":%lu,\"state\":",
        (unsigned long)(status->uptime_ms / 1000U));
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_state_name(status->state));
    laser_controller_comms_buffer_append_raw(buffer, ",\"powerTier\":");
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_comms_power_tier_name(status->power_tier));
    laser_controller_comms_buffer_append_raw(buffer, ",\"bootReason\":");
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_comms_reset_reason_name(reset_reason));
    laser_controller_comms_buffer_append_raw(
        buffer,
        ",\"connectedAtIso\":\"1970-01-01T00:00:00Z\"},");

    laser_controller_comms_buffer_append_raw(buffer, "\"wireless\":");
    laser_controller_comms_write_wireless_json(buffer, &wireless);
    laser_controller_comms_buffer_append_raw(buffer, ",");

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"pd\":{\"contractValid\":%s,\"negotiatedPowerW\":%.2f,\"sourceVoltageV\":%.2f,\"sourceCurrentA\":%.2f,\"operatingCurrentA\":%.2f,\"contractObjectPosition\":%u,\"sinkProfileCount\":%u,\"sinkProfiles\":[",
        status->inputs.pd_contract_valid ? "true" : "false",
        status->inputs.pd_negotiated_power_w,
        status->inputs.pd_source_voltage_v,
        status->inputs.pd_source_current_a,
        status->inputs.pd_operating_current_a,
        (unsigned int)status->inputs.pd_contract_object_position,
        (unsigned int)status->inputs.pd_sink_profile_count);
    for (size_t index = 0U;
         index < LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT;
         ++index) {
        if (index > 0U) {
            laser_controller_comms_buffer_append_raw(buffer, ",");
        }
        laser_controller_comms_write_pd_profile_json(
            buffer,
            &status->inputs.pd_sink_profiles[index]);
    }
    laser_controller_comms_buffer_append_fmt(
        buffer,
        "],\"sourceIsHostOnly\":%s,\"lastUpdatedMs\":%lu,\"snapshotFresh\":%s,\"source\":",
        status->inputs.pd_source_is_host_only ? "true" : "false",
        (unsigned long)status->inputs.pd_last_updated_ms,
        status->inputs.pd_snapshot_fresh ? "true" : "false");
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_comms_pd_snapshot_source_name(
            status->inputs.pd_source));
    laser_controller_comms_buffer_append_raw(buffer, "},");

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"rails\":{\"ld\":{\"enabled\":%s,\"pgood\":%s},\"tec\":{\"enabled\":%s,\"pgood\":%s}},",
        status->outputs.enable_ld_vin ? "true" : "false",
        status->inputs.ld_rail_pgood ? "true" : "false",
        status->outputs.enable_tec_vin ? "true" : "false",
        status->inputs.tec_rail_pgood ? "true" : "false");

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"imu\":{\"valid\":%s,\"fresh\":%s,\"beamPitchDeg\":%.2f,\"beamRollDeg\":%.2f,\"beamYawDeg\":%.2f,\"beamYawRelative\":true,\"beamPitchLimitDeg\":%.2f},",
        status->inputs.imu_data_valid ? "true" : "false",
        status->inputs.imu_data_fresh ? "true" : "false",
        beam_pitch_deg,
        beam_roll_deg,
        beam_yaw_deg,
        beam_pitch_limit_deg);

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"tof\":{\"valid\":%s,\"fresh\":%s,\"distanceM\":%.3f,\"minRangeM\":%.3f,\"maxRangeM\":%.3f},",
        status->inputs.tof_data_valid ? "true" : "false",
        status->inputs.tof_data_fresh ? "true" : "false",
        status->inputs.tof_distance_m,
        status->config.thresholds.tof_min_range_m,
        status->config.thresholds.tof_max_range_m);

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"laser\":{\"alignmentEnabled\":%s,\"nirEnabled\":%s,\"driverStandby\":%s,\"commandVoltageV\":%.3f,\"commandedCurrentA\":%.3f,\"currentMonitorVoltageV\":%.3f,\"measuredCurrentA\":%.3f,\"loopGood\":%s,\"driverTempVoltageV\":%.3f,\"driverTempC\":%.2f},",
        status->outputs.enable_alignment_laser ? "true" : "false",
        status->decision.nir_output_enable ? "true" : "false",
        laser_controller_comms_driver_standby_effective(status) ? "true" : "false",
        laser_controller_comms_effective_ld_command_voltage_v(status),
        laser_controller_comms_effective_commanded_current_a(status),
        status->inputs.laser_current_monitor_voltage_v,
        status->inputs.measured_laser_current_a,
        status->inputs.driver_loop_good ? "true" : "false",
        status->inputs.laser_driver_temp_voltage_v,
        status->inputs.laser_driver_temp_c);

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"tec\":{\"targetTempC\":%.2f,\"targetLambdaNm\":%.2f,\"actualLambdaNm\":%.2f,\"tempGood\":%s,\"tempC\":%.2f,\"tempAdcVoltageV\":%.3f,\"currentA\":%.2f,\"voltageV\":%.2f,\"settlingSecondsRemaining\":%u},",
        status->bench.target_temp_c,
        status->bench.target_lambda_nm,
        actual_lambda_nm,
        status->inputs.tec_temp_good ? "true" : "false",
        status->inputs.tec_temp_c,
        status->inputs.tec_temp_adc_voltage_v,
        status->inputs.tec_current_a,
        status->inputs.tec_voltage_v,
        status->inputs.tec_temp_good ? 0U : 1U);

    laser_controller_comms_write_haptic_readback_json(buffer, status);
    laser_controller_comms_write_gpio_inspector_json(buffer, status);

    laser_controller_comms_buffer_append_raw(buffer, "\"bench\":{");
    laser_controller_comms_buffer_append_raw(buffer, "\"targetMode\":");
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_bench_target_mode_name(status->bench.target_mode));
    laser_controller_comms_buffer_append_fmt(
        buffer,
            ",\"requestedAlignmentEnabled\":%s,\"appliedAlignmentEnabled\":%s,\"requestedNirEnabled\":%s,\"requestedLedEnabled\":%s,\"requestedLedDutyCyclePct\":%lu,\"appliedLedOwner\":",
            status->bench.requested_alignment ? "true" : "false",
            status->outputs.enable_alignment_laser ? "true" : "false",
            status->bench.requested_nir ? "true" : "false",
            status->bench.illumination_enabled ? "true" : "false",
            (unsigned long)status->bench.illumination_duty_cycle_pct);
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_comms_led_owner_name(status));
    laser_controller_comms_buffer_append_fmt(
        buffer,
        ",\"appliedLedPinHigh\":%s,\"illuminationEnabled\":%s,\"illuminationDutyCyclePct\":%lu,\"illuminationFrequencyHz\":%lu,\"modulationEnabled\":%s,\"modulationFrequencyHz\":%lu,\"modulationDutyCyclePct\":%lu,\"lowStateCurrentA\":%.3f},",
        laser_controller_comms_gpio_pin_high(
            status,
            LASER_CONTROLLER_GPIO_TOF_LED_CTRL) ? "true" : "false",
            status->bench.illumination_enabled ? "true" : "false",
            (unsigned long)status->bench.illumination_duty_cycle_pct,
            (unsigned long)status->bench.illumination_frequency_hz,
            status->bench.modulation_enabled ? "true" : "false",
            (unsigned long)status->bench.modulation_frequency_hz,
            (unsigned long)status->bench.modulation_duty_cycle_pct,
            status->bench.low_state_current_a);

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"safety\":{\"allowAlignment\":%s,\"allowNir\":%s,\"horizonBlocked\":%s,\"distanceBlocked\":%s,\"lambdaDriftBlocked\":%s,\"tecTempAdcBlocked\":%s,\"horizonThresholdDeg\":%.2f,\"horizonHysteresisDeg\":%.2f,\"tofMinRangeM\":%.3f,\"tofMaxRangeM\":%.3f,\"tofHysteresisM\":%.3f,\"imuStaleMs\":%lu,\"tofStaleMs\":%lu,\"railGoodTimeoutMs\":%lu,\"lambdaDriftLimitNm\":%.2f,\"lambdaDriftHysteresisNm\":%.2f,\"lambdaDriftHoldMs\":%lu,\"ldOvertempLimitC\":%.2f,\"tecTempAdcTripV\":%.3f,\"tecTempAdcHysteresisV\":%.3f,\"tecTempAdcHoldMs\":%lu,\"tecMinCommandC\":%.2f,\"tecMaxCommandC\":%.2f,\"tecReadyToleranceC\":%.2f,\"maxLaserCurrentA\":%.2f,\"offCurrentThresholdA\":%.2f,\"maxTofLedDutyCyclePct\":%u,\"lioVoltageOffsetV\":%.4f,\"actualLambdaNm\":%.2f,\"targetLambdaNm\":%.2f,\"lambdaDriftNm\":%.2f,\"tempAdcVoltageV\":%.3f,\"interlocks\":{\"horizonEnabled\":%s,\"distanceEnabled\":%s,\"lambdaDriftEnabled\":%s,\"tecTempAdcEnabled\":%s,\"imuInvalidEnabled\":%s,\"imuStaleEnabled\":%s,\"tofInvalidEnabled\":%s,\"tofStaleEnabled\":%s,\"ldOvertempEnabled\":%s,\"ldLoopBadEnabled\":%s,\"tofLowBoundOnly\":%s},\"autoDeployOnBoot\":%s},",
        status->decision.allow_alignment ? "true" : "false",
        status->decision.allow_nir ? "true" : "false",
        status->decision.horizon_blocked ? "true" : "false",
        status->decision.distance_blocked ? "true" : "false",
        status->decision.lambda_drift_blocked ? "true" : "false",
        status->decision.tec_temp_adc_blocked ? "true" : "false",
        beam_pitch_limit_deg,
        beam_pitch_hysteresis_deg,
        status->config.thresholds.tof_min_range_m,
        status->config.thresholds.tof_max_range_m,
        status->config.thresholds.tof_hysteresis_m,
        (unsigned long)status->config.timeouts.imu_stale_ms,
        (unsigned long)status->config.timeouts.tof_stale_ms,
        (unsigned long)status->config.timeouts.rail_good_timeout_ms,
        status->config.thresholds.lambda_drift_limit_nm,
        status->config.thresholds.lambda_drift_hysteresis_nm,
        (unsigned long)status->config.timeouts.lambda_drift_hold_ms,
        status->config.thresholds.ld_overtemp_limit_c,
        status->config.thresholds.tec_temp_adc_trip_v,
        status->config.thresholds.tec_temp_adc_hysteresis_v,
        (unsigned long)status->config.timeouts.tec_temp_adc_hold_ms,
        status->config.thresholds.tec_min_command_c,
        status->config.thresholds.tec_max_command_c,
        status->config.thresholds.tec_ready_tolerance_c,
        status->config.thresholds.max_laser_current_a,
        status->config.thresholds.off_current_threshold_a,
        (unsigned)status->config.thresholds.max_tof_led_duty_cycle_pct,
        (double)status->config.thresholds.lio_voltage_offset_v,
        actual_lambda_nm,
        status->bench.target_lambda_nm,
        lambda_drift_nm,
        status->inputs.tec_temp_adc_voltage_v,
        status->config.thresholds.interlocks.horizon_enabled ? "true" : "false",
        status->config.thresholds.interlocks.distance_enabled ? "true" : "false",
        status->config.thresholds.interlocks.lambda_drift_enabled ? "true" : "false",
        status->config.thresholds.interlocks.tec_temp_adc_enabled ? "true" : "false",
        status->config.thresholds.interlocks.imu_invalid_enabled ? "true" : "false",
        status->config.thresholds.interlocks.imu_stale_enabled ? "true" : "false",
        status->config.thresholds.interlocks.tof_invalid_enabled ? "true" : "false",
        status->config.thresholds.interlocks.tof_stale_enabled ? "true" : "false",
        status->config.thresholds.interlocks.ld_overtemp_enabled ? "true" : "false",
        status->config.thresholds.interlocks.ld_loop_bad_enabled ? "true" : "false",
        status->config.thresholds.interlocks.tof_low_bound_only ? "true" : "false",
        (status->config.service_flags &
         LASER_CONTROLLER_SERVICE_FLAG_AUTO_DEPLOY_ON_BOOT) != 0U ?
            "true" : "false");

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"bringup\":{\"serviceModeRequested\":%s,\"serviceModeActive\":%s,\"interlocksDisabled\":%s,\"persistenceDirty\":%s,\"persistenceAvailable\":%s,\"lastSaveOk\":%s,\"lastSaveAtMs\":%lu,\"profileRevision\":%lu,\"profileName\":",
        status->bringup.service_mode_requested ? "true" : "false",
        status->state == LASER_CONTROLLER_STATE_SERVICE_MODE ? "true" : "false",
        status->bringup.interlocks_disabled ? "true" : "false",
        status->bringup.persistence_dirty ? "true" : "false",
        status->bringup.persistence_available ? "true" : "false",
        status->bringup.last_save_ok ? "true" : "false",
        (unsigned long)status->bringup.last_save_at_ms,
        (unsigned long)status->bringup.profile_revision);
    laser_controller_comms_write_escaped_string(buffer, status->bringup.profile_name);
    laser_controller_comms_buffer_append_fmt(
        buffer,
        ",\"power\":{\"ldRequested\":%s,\"tecRequested\":%s},\"illumination\":{\"tof\":{\"enabled\":%s,\"dutyCyclePct\":%lu,\"frequencyHz\":%lu}},\"modules\":{",
        status->bringup.ld_rail_debug_enabled ? "true" : "false",
        status->bringup.tec_rail_debug_enabled ? "true" : "false",
        status->bringup.tof_illumination_enabled ? "true" : "false",
        (unsigned long)status->bringup.tof_illumination_duty_cycle_pct,
        (unsigned long)status->bringup.tof_illumination_frequency_hz);
    laser_controller_comms_buffer_append_raw(buffer, "\"imu\":");
    laser_controller_comms_write_module_json(
        buffer,
        &status->bringup.modules[LASER_CONTROLLER_MODULE_IMU]);
    laser_controller_comms_buffer_append_raw(buffer, ",\"dac\":");
    laser_controller_comms_write_module_json(
        buffer,
        &status->bringup.modules[LASER_CONTROLLER_MODULE_DAC]);
    laser_controller_comms_buffer_append_raw(buffer, ",\"haptic\":");
    laser_controller_comms_write_module_json(
        buffer,
        &status->bringup.modules[LASER_CONTROLLER_MODULE_HAPTIC]);
    laser_controller_comms_buffer_append_raw(buffer, ",\"tof\":");
    laser_controller_comms_write_module_json(
        buffer,
        &status->bringup.modules[LASER_CONTROLLER_MODULE_TOF]);
    laser_controller_comms_buffer_append_raw(buffer, ",\"buttons\":");
    laser_controller_comms_write_module_json(
        buffer,
        &status->bringup.modules[LASER_CONTROLLER_MODULE_BUTTONS]);
    laser_controller_comms_buffer_append_raw(buffer, ",\"pd\":");
    laser_controller_comms_write_module_json(
        buffer,
        &status->bringup.modules[LASER_CONTROLLER_MODULE_PD]);
    laser_controller_comms_buffer_append_raw(buffer, ",\"laserDriver\":");
    laser_controller_comms_write_module_json(
        buffer,
        &status->bringup.modules[LASER_CONTROLLER_MODULE_LASER_DRIVER]);
    laser_controller_comms_buffer_append_raw(buffer, ",\"tec\":");
    laser_controller_comms_write_module_json(
        buffer,
        &status->bringup.modules[LASER_CONTROLLER_MODULE_TEC]);
    laser_controller_comms_buffer_append_raw(buffer, "},\"tools\":{\"lastI2cScan\":");
    laser_controller_comms_write_escaped_string(buffer, status->bringup.last_i2c_scan);
    laser_controller_comms_buffer_append_raw(buffer, ",\"lastI2cOp\":");
    laser_controller_comms_write_escaped_string(buffer, status->bringup.last_i2c_op);
    laser_controller_comms_buffer_append_raw(buffer, ",\"lastSpiOp\":");
    laser_controller_comms_write_escaped_string(buffer, status->bringup.last_spi_op);
    laser_controller_comms_buffer_append_raw(buffer, ",\"lastAction\":");
    laser_controller_comms_write_escaped_string(buffer, status->bringup.last_action);
    laser_controller_comms_buffer_append_raw(buffer, "}},");

    laser_controller_comms_write_deployment_json(buffer, status, true);

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"fault\":{\"latched\":%s,\"activeCode\":",
        status->fault_latched ? "true" : "false");
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_fault_code_name(status->active_fault_code));
    laser_controller_comms_buffer_append_fmt(
        buffer,
        ",\"activeCount\":%lu,\"tripCounter\":%lu,",
        (unsigned long)status->active_fault_count,
        (unsigned long)status->trip_counter);
    if (status->last_fault_ms == 0U) {
        laser_controller_comms_buffer_append_raw(buffer, "\"lastFaultAtIso\":null},");
    } else {
        laser_controller_comms_buffer_append_raw(
            buffer,
            "\"lastFaultAtIso\":\"1970-01-01T00:00:00Z\"},");
    }

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"counters\":{\"commsTimeouts\":0,\"watchdogTrips\":%u,\"brownouts\":%u}}",
        reset_reason == ESP_RST_TASK_WDT ||
                reset_reason == ESP_RST_INT_WDT || reset_reason == ESP_RST_WDT
            ? 1U
            : 0U,
        reset_reason == ESP_RST_BROWNOUT ? 1U : 0U);
}

static void laser_controller_comms_write_live_telemetry_json(
    laser_controller_comms_buffer_t *buffer,
    const laser_controller_runtime_status_t *status)
{
    const float actual_lambda_nm =
        laser_controller_comms_actual_lambda_nm(status);
    const float lambda_drift_nm =
        laser_controller_comms_lambda_drift_nm(status);

    laser_controller_comms_buffer_append_raw(buffer, "{");
    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"session\":{\"uptimeSeconds\":%lu,\"state\":",
        (unsigned long)(status->uptime_ms / 1000U));
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_state_name(status->state));
    laser_controller_comms_buffer_append_raw(buffer, ",\"powerTier\":");
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_comms_power_tier_name(status->power_tier));
    laser_controller_comms_buffer_append_raw(buffer, "},");

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"pd\":{\"contractValid\":%s,\"negotiatedPowerW\":%.2f,\"sourceVoltageV\":%.2f,\"sourceCurrentA\":%.2f,\"operatingCurrentA\":%.2f,\"sourceIsHostOnly\":%s,\"lastUpdatedMs\":%lu,\"snapshotFresh\":%s,\"source\":",
        status->inputs.pd_contract_valid ? "true" : "false",
        status->inputs.pd_negotiated_power_w,
        status->inputs.pd_source_voltage_v,
        status->inputs.pd_source_current_a,
        status->inputs.pd_operating_current_a,
        status->inputs.pd_source_is_host_only ? "true" : "false",
        (unsigned long)status->inputs.pd_last_updated_ms,
        status->inputs.pd_snapshot_fresh ? "true" : "false");
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_comms_pd_snapshot_source_name(
            status->inputs.pd_source));
    laser_controller_comms_buffer_append_raw(buffer, "},");

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"rails\":{\"ld\":{\"enabled\":%s,\"pgood\":%s},\"tec\":{\"enabled\":%s,\"pgood\":%s}},",
        status->outputs.enable_ld_vin ? "true" : "false",
        status->inputs.ld_rail_pgood ? "true" : "false",
        status->outputs.enable_tec_vin ? "true" : "false",
        status->inputs.tec_rail_pgood ? "true" : "false");

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"laser\":{\"alignmentEnabled\":%s,\"nirEnabled\":%s,\"driverStandby\":%s,\"telemetryValid\":%s,\"commandVoltageV\":%.3f,\"commandedCurrentA\":%.3f,\"currentMonitorVoltageV\":%.3f,\"measuredCurrentA\":%.3f,\"loopGood\":%s,\"driverTempVoltageV\":%.3f,\"driverTempC\":%.2f},",
        status->outputs.enable_alignment_laser ? "true" : "false",
        status->decision.nir_output_enable ? "true" : "false",
        laser_controller_comms_driver_standby_effective(status) ? "true" : "false",
        status->inputs.ld_telemetry_valid ? "true" : "false",
        laser_controller_comms_effective_ld_command_voltage_v(status),
        laser_controller_comms_effective_commanded_current_a(status),
        status->inputs.laser_current_monitor_voltage_v,
        status->inputs.measured_laser_current_a,
        status->inputs.driver_loop_good ? "true" : "false",
        status->inputs.laser_driver_temp_voltage_v,
        status->inputs.laser_driver_temp_c);

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"tec\":{\"targetTempC\":%.2f,\"targetLambdaNm\":%.2f,\"actualLambdaNm\":%.2f,\"telemetryValid\":%s},",
        status->bench.target_temp_c,
        status->bench.target_lambda_nm,
        actual_lambda_nm,
        status->inputs.tec_telemetry_valid ? "true" : "false");

    laser_controller_comms_write_button_state_json(buffer, status);
    laser_controller_comms_write_button_board_json(buffer, status);
    laser_controller_comms_write_bench_json(buffer, status);
    laser_controller_comms_buffer_append_raw(buffer, ",");

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"safety\":{\"allowAlignment\":%s,\"allowNir\":%s,\"horizonBlocked\":%s,\"distanceBlocked\":%s,\"lambdaDriftBlocked\":%s,\"tecTempAdcBlocked\":%s,\"actualLambdaNm\":%.2f,\"targetLambdaNm\":%.2f,\"lambdaDriftNm\":%.2f,\"tempAdcVoltageV\":%.3f,\"interlocks\":{\"horizonEnabled\":%s,\"distanceEnabled\":%s,\"lambdaDriftEnabled\":%s,\"tecTempAdcEnabled\":%s,\"imuInvalidEnabled\":%s,\"imuStaleEnabled\":%s,\"tofInvalidEnabled\":%s,\"tofStaleEnabled\":%s,\"ldOvertempEnabled\":%s,\"ldLoopBadEnabled\":%s,\"tofLowBoundOnly\":%s},\"autoDeployOnBoot\":%s},",
        status->decision.allow_alignment ? "true" : "false",
        status->decision.allow_nir ? "true" : "false",
        status->decision.horizon_blocked ? "true" : "false",
        status->decision.distance_blocked ? "true" : "false",
        status->decision.lambda_drift_blocked ? "true" : "false",
        status->decision.tec_temp_adc_blocked ? "true" : "false",
        actual_lambda_nm,
        status->bench.target_lambda_nm,
        lambda_drift_nm,
        status->inputs.tec_temp_adc_voltage_v,
        status->config.thresholds.interlocks.horizon_enabled ? "true" : "false",
        status->config.thresholds.interlocks.distance_enabled ? "true" : "false",
        status->config.thresholds.interlocks.lambda_drift_enabled ? "true" : "false",
        status->config.thresholds.interlocks.tec_temp_adc_enabled ? "true" : "false",
        status->config.thresholds.interlocks.imu_invalid_enabled ? "true" : "false",
        status->config.thresholds.interlocks.imu_stale_enabled ? "true" : "false",
        status->config.thresholds.interlocks.tof_invalid_enabled ? "true" : "false",
        status->config.thresholds.interlocks.tof_stale_enabled ? "true" : "false",
        status->config.thresholds.interlocks.ld_overtemp_enabled ? "true" : "false",
        status->config.thresholds.interlocks.ld_loop_bad_enabled ? "true" : "false",
        status->config.thresholds.interlocks.tof_low_bound_only ? "true" : "false",
        (status->config.service_flags &
         LASER_CONTROLLER_SERVICE_FLAG_AUTO_DEPLOY_ON_BOOT) != 0U ?
            "true" : "false");

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"bringup\":{\"serviceModeRequested\":%s,\"serviceModeActive\":%s,\"interlocksDisabled\":%s,\"illumination\":{\"tof\":{\"enabled\":%s}}},",
        status->bringup.service_mode_requested ? "true" : "false",
        status->state == LASER_CONTROLLER_STATE_SERVICE_MODE ? "true" : "false",
        status->bringup.interlocks_disabled ? "true" : "false",
        status->bringup.tof_illumination_enabled ? "true" : "false");

    laser_controller_comms_write_deployment_json(buffer, status, true);

    laser_controller_comms_write_fault_json(buffer, status, false);
    laser_controller_comms_buffer_append_raw(buffer, "}");
}

static void laser_controller_comms_write_fast_telemetry_json(
    laser_controller_comms_buffer_t *buffer,
    const laser_controller_runtime_status_t *status)
{
    const uint32_t imu_flags =
        laser_controller_comms_encode_imu_flags(status);
    const uint32_t tof_flags =
        laser_controller_comms_encode_tof_flags(status);
    const uint32_t laser_flags =
        laser_controller_comms_encode_laser_flags(status);
    const uint32_t tec_flags =
        laser_controller_comms_encode_tec_flags(status);
    const uint32_t safety_flags =
        laser_controller_comms_encode_safety_flags(status);
    const uint32_t button_flags =
        laser_controller_comms_encode_button_flags(status);

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "{\"v\":1,\"m\":[%lu,%ld,%ld,%ld,%ld,%lu,%ld,%lu,%ld,%ld,%lu,%ld,%ld,%ld,%ld,%lu,%lu]}",
        (unsigned long)imu_flags,
        (long)laser_controller_comms_encode_centi(
            status->inputs.beam_pitch_rad * LASER_CONTROLLER_DEG_PER_RAD),
        (long)laser_controller_comms_encode_centi(
            status->inputs.beam_roll_rad * LASER_CONTROLLER_DEG_PER_RAD),
        (long)laser_controller_comms_encode_centi(
            status->inputs.beam_yaw_rad * LASER_CONTROLLER_DEG_PER_RAD),
        (long)laser_controller_comms_encode_centi(
            status->config.thresholds.horizon_threshold_rad *
            LASER_CONTROLLER_DEG_PER_RAD),
        (unsigned long)tof_flags,
        (long)lroundf(status->inputs.tof_distance_m * 1000.0f),
        (unsigned long)laser_flags,
        (long)lroundf(status->inputs.measured_laser_current_a * 1000.0f),
        (long)laser_controller_comms_encode_centi(status->inputs.laser_driver_temp_c),
        (unsigned long)tec_flags,
        (long)laser_controller_comms_encode_centi(status->inputs.tec_temp_c),
        (long)lroundf(status->inputs.tec_temp_adc_voltage_v * 1000.0f),
        (long)lroundf(status->inputs.tec_current_a * 100.0f),
        (long)lroundf(status->inputs.tec_voltage_v * 100.0f),
        (unsigned long)safety_flags,
        (unsigned long)button_flags);
}

static void laser_controller_comms_write_live_snapshot_json(
    laser_controller_comms_buffer_t *buffer,
    const laser_controller_runtime_status_t *status)
{
    const esp_app_desc_t *app_desc = esp_app_get_description();
    const laser_controller_firmware_signature_t *signature =
        laser_controller_signature_get();
    const float beam_pitch_deg =
        status->inputs.beam_pitch_rad * LASER_CONTROLLER_DEG_PER_RAD;
    const float beam_roll_deg =
        status->inputs.beam_roll_rad * LASER_CONTROLLER_DEG_PER_RAD;
    const float beam_yaw_deg =
        status->inputs.beam_yaw_rad * LASER_CONTROLLER_DEG_PER_RAD;
    const float beam_pitch_limit_deg =
        status->config.thresholds.horizon_threshold_rad * LASER_CONTROLLER_DEG_PER_RAD;
    const float actual_lambda_nm =
        laser_controller_comms_actual_lambda_nm(status);
    const float lambda_drift_nm =
        laser_controller_comms_lambda_drift_nm(status);

    laser_controller_comms_buffer_append_raw(buffer, "{");
    laser_controller_comms_buffer_append_raw(buffer, "\"identity\":{");
    laser_controller_comms_buffer_append_raw(buffer, "\"label\":");
    laser_controller_comms_write_escaped_string(
        buffer,
        signature != NULL ? signature->product_name : LASER_CONTROLLER_FIRMWARE_PRODUCT_NAME);
    laser_controller_comms_buffer_append_raw(buffer, ",\"firmwareVersion\":");
    laser_controller_comms_write_escaped_string(
        buffer,
        signature != NULL ? signature->firmware_version : app_desc->version);
    laser_controller_comms_buffer_append_fmt(
        buffer,
        ",\"hardwareRevision\":\"rev-%lu\",\"serialNumber\":",
        (unsigned long)status->config.hardware_revision);
    laser_controller_comms_write_escaped_string(buffer, status->config.serial_number);
    laser_controller_comms_buffer_append_raw(buffer, ",\"protocolVersion\":");
    laser_controller_comms_write_escaped_string(
        buffer,
        signature != NULL ? signature->protocol_version :
                            LASER_CONTROLLER_FIRMWARE_PROTOCOL_VERSION);
    laser_controller_comms_buffer_append_raw(buffer, "},");

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"session\":{\"uptimeSeconds\":%lu,\"state\":",
        (unsigned long)(status->uptime_ms / 1000U));
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_state_name(status->state));
    laser_controller_comms_buffer_append_raw(buffer, ",\"powerTier\":");
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_comms_power_tier_name(status->power_tier));
    laser_controller_comms_buffer_append_raw(
        buffer,
        ",\"connectedAtIso\":\"1970-01-01T00:00:00Z\"},");

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"pd\":{\"contractValid\":%s,\"negotiatedPowerW\":%.2f,\"sourceVoltageV\":%.2f,\"sourceCurrentA\":%.2f,\"operatingCurrentA\":%.2f,\"sourceIsHostOnly\":%s,\"lastUpdatedMs\":%lu,\"snapshotFresh\":%s,\"source\":",
        status->inputs.pd_contract_valid ? "true" : "false",
        status->inputs.pd_negotiated_power_w,
        status->inputs.pd_source_voltage_v,
        status->inputs.pd_source_current_a,
        status->inputs.pd_operating_current_a,
        status->inputs.pd_source_is_host_only ? "true" : "false",
        (unsigned long)status->inputs.pd_last_updated_ms,
        status->inputs.pd_snapshot_fresh ? "true" : "false");
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_comms_pd_snapshot_source_name(
            status->inputs.pd_source));
    laser_controller_comms_buffer_append_raw(buffer, "},");

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"rails\":{\"ld\":{\"enabled\":%s,\"pgood\":%s},\"tec\":{\"enabled\":%s,\"pgood\":%s}},",
        status->outputs.enable_ld_vin ? "true" : "false",
        status->inputs.ld_rail_pgood ? "true" : "false",
        status->outputs.enable_tec_vin ? "true" : "false",
        status->inputs.tec_rail_pgood ? "true" : "false");

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"imu\":{\"valid\":%s,\"fresh\":%s,\"beamPitchDeg\":%.2f,\"beamRollDeg\":%.2f,\"beamYawDeg\":%.2f,\"beamYawRelative\":true,\"beamPitchLimitDeg\":%.2f},",
        status->inputs.imu_data_valid ? "true" : "false",
        status->inputs.imu_data_fresh ? "true" : "false",
        beam_pitch_deg,
        beam_roll_deg,
        beam_yaw_deg,
        beam_pitch_limit_deg);

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"tof\":{\"valid\":%s,\"fresh\":%s,\"distanceM\":%.3f,\"minRangeM\":%.3f,\"maxRangeM\":%.3f},",
        status->inputs.tof_data_valid ? "true" : "false",
        status->inputs.tof_data_fresh ? "true" : "false",
        status->inputs.tof_distance_m,
        status->config.thresholds.tof_min_range_m,
        status->config.thresholds.tof_max_range_m);

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"laser\":{\"alignmentEnabled\":%s,\"nirEnabled\":%s,\"driverStandby\":%s,\"telemetryValid\":%s,\"commandVoltageV\":%.3f,\"commandedCurrentA\":%.3f,\"currentMonitorVoltageV\":%.3f,\"measuredCurrentA\":%.3f,\"loopGood\":%s,\"driverTempVoltageV\":%.3f,\"driverTempC\":%.2f},",
        status->outputs.enable_alignment_laser ? "true" : "false",
        status->decision.nir_output_enable ? "true" : "false",
        laser_controller_comms_driver_standby_effective(status) ? "true" : "false",
        status->inputs.ld_telemetry_valid ? "true" : "false",
        laser_controller_comms_effective_ld_command_voltage_v(status),
        laser_controller_comms_effective_commanded_current_a(status),
        status->inputs.laser_current_monitor_voltage_v,
        status->inputs.measured_laser_current_a,
        status->inputs.driver_loop_good ? "true" : "false",
        status->inputs.laser_driver_temp_voltage_v,
        status->inputs.laser_driver_temp_c);

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"tec\":{\"targetTempC\":%.2f,\"targetLambdaNm\":%.2f,\"actualLambdaNm\":%.2f,\"telemetryValid\":%s,\"tempGood\":%s,\"tempC\":%.2f,\"tempAdcVoltageV\":%.3f,\"currentA\":%.2f,\"voltageV\":%.2f,\"settlingSecondsRemaining\":%u},",
        status->bench.target_temp_c,
        status->bench.target_lambda_nm,
        actual_lambda_nm,
        status->inputs.tec_telemetry_valid ? "true" : "false",
        status->inputs.tec_temp_good ? "true" : "false",
        status->inputs.tec_temp_c,
        status->inputs.tec_temp_adc_voltage_v,
        status->inputs.tec_current_a,
        status->inputs.tec_voltage_v,
        status->inputs.tec_temp_good ? 0U : 1U);

    laser_controller_comms_write_haptic_readback_json(buffer, status);
    /*
     * GPIO inspector block unconditionally skipped from the periodic
     * status_snapshot broadcast (2026-04-17). The 40-pin dump is ~8 KB
     * of JSON and can stretch the status_snapshot frame to ~30 KB once
     * bringup + bench + haptic + deployment are included. On Wi-Fi AP
     * that large frame holds the httpd send socket for >1 s; when a
     * slow client (NVS-save-in-flight) lets the TCP window fill, the
     * 1 s send_wait_timeout fires, the send errors out, and
     * httpd_sess_trigger_close tears the session down — presenting to
     * the user as "Wireless controller link dropped" mid-Apply. The
     * inspector remains available on demand via `status.io_get`
     * (emit_io_status_response); the Integrate workspace polls it when
     * the inspector panel is visible. Telemetry clients that need live
     * pin readback should poll, not rely on the 5-s broadcast.
     */
    laser_controller_comms_write_button_state_json(buffer, status);
    laser_controller_comms_write_button_board_json(buffer, status);
    laser_controller_comms_write_bench_json(buffer, status);
    laser_controller_comms_buffer_append_raw(buffer, ",");

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"safety\":{\"allowAlignment\":%s,\"allowNir\":%s,\"horizonBlocked\":%s,\"distanceBlocked\":%s,\"lambdaDriftBlocked\":%s,\"tecTempAdcBlocked\":%s,\"actualLambdaNm\":%.2f,\"targetLambdaNm\":%.2f,\"lambdaDriftNm\":%.2f,\"tempAdcVoltageV\":%.3f,\"interlocks\":{\"horizonEnabled\":%s,\"distanceEnabled\":%s,\"lambdaDriftEnabled\":%s,\"tecTempAdcEnabled\":%s,\"imuInvalidEnabled\":%s,\"imuStaleEnabled\":%s,\"tofInvalidEnabled\":%s,\"tofStaleEnabled\":%s,\"ldOvertempEnabled\":%s,\"ldLoopBadEnabled\":%s,\"tofLowBoundOnly\":%s},\"autoDeployOnBoot\":%s},",
        status->decision.allow_alignment ? "true" : "false",
        status->decision.allow_nir ? "true" : "false",
        status->decision.horizon_blocked ? "true" : "false",
        status->decision.distance_blocked ? "true" : "false",
        status->decision.lambda_drift_blocked ? "true" : "false",
        status->decision.tec_temp_adc_blocked ? "true" : "false",
        actual_lambda_nm,
        status->bench.target_lambda_nm,
        lambda_drift_nm,
        status->inputs.tec_temp_adc_voltage_v,
        status->config.thresholds.interlocks.horizon_enabled ? "true" : "false",
        status->config.thresholds.interlocks.distance_enabled ? "true" : "false",
        status->config.thresholds.interlocks.lambda_drift_enabled ? "true" : "false",
        status->config.thresholds.interlocks.tec_temp_adc_enabled ? "true" : "false",
        status->config.thresholds.interlocks.imu_invalid_enabled ? "true" : "false",
        status->config.thresholds.interlocks.imu_stale_enabled ? "true" : "false",
        status->config.thresholds.interlocks.tof_invalid_enabled ? "true" : "false",
        status->config.thresholds.interlocks.tof_stale_enabled ? "true" : "false",
        status->config.thresholds.interlocks.ld_overtemp_enabled ? "true" : "false",
        status->config.thresholds.interlocks.ld_loop_bad_enabled ? "true" : "false",
        status->config.thresholds.interlocks.tof_low_bound_only ? "true" : "false",
        (status->config.service_flags &
         LASER_CONTROLLER_SERVICE_FLAG_AUTO_DEPLOY_ON_BOOT) != 0U ?
            "true" : "false");

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"bringup\":{\"serviceModeRequested\":%s,\"serviceModeActive\":%s,\"interlocksDisabled\":%s,\"power\":{\"ldRequested\":%s,\"tecRequested\":%s},\"illumination\":{\"tof\":{\"enabled\":%s,\"dutyCyclePct\":%lu,\"frequencyHz\":%lu}}},",
        status->bringup.service_mode_requested ? "true" : "false",
        status->state == LASER_CONTROLLER_STATE_SERVICE_MODE ? "true" : "false",
        status->bringup.interlocks_disabled ? "true" : "false",
        status->bringup.ld_rail_debug_enabled ? "true" : "false",
        status->bringup.tec_rail_debug_enabled ? "true" : "false",
        status->bringup.tof_illumination_enabled ? "true" : "false",
        (unsigned long)status->bringup.tof_illumination_duty_cycle_pct,
        (unsigned long)status->bringup.tof_illumination_frequency_hz);

    laser_controller_comms_write_deployment_json(buffer, status, true);

    laser_controller_comms_write_fault_json(buffer, status, true);
    laser_controller_comms_buffer_append_raw(buffer, "}");
}

static void laser_controller_comms_emit_log_event(
    const laser_controller_log_entry_t *entry)
{
    laser_controller_comms_buffer_t buffer;

    laser_controller_comms_output_lock();
    laser_controller_comms_buffer_reset(
        &buffer,
        s_frame_buffer,
        sizeof(s_frame_buffer));
    laser_controller_comms_buffer_append_fmt(
        &buffer,
        "{\"type\":\"event\",\"event\":\"log\",\"timestamp_ms\":%lu,\"detail\":",
        (unsigned long)entry->timestamp_ms);
    laser_controller_comms_write_escaped_string(&buffer, entry->message);
    laser_controller_comms_buffer_append_raw(&buffer, ",\"payload\":{\"category\":");
    laser_controller_comms_write_escaped_string(&buffer, entry->category);
    laser_controller_comms_buffer_append_raw(&buffer, ",\"message\":");
    laser_controller_comms_write_escaped_string(&buffer, entry->message);
    laser_controller_comms_buffer_append_raw(&buffer, "}}\n");
    laser_controller_comms_emit_buffer_locked(&buffer, "log");
    laser_controller_comms_output_unlock();
}

static void laser_controller_comms_emit_status_event(
    const laser_controller_runtime_status_t *status)
{
    laser_controller_comms_buffer_t buffer;

    laser_controller_comms_output_lock();
    laser_controller_comms_buffer_reset(
        &buffer,
        s_frame_buffer,
        sizeof(s_frame_buffer));
    laser_controller_comms_buffer_append_fmt(
        &buffer,
        "{\"type\":\"event\",\"event\":\"status_snapshot\",\"timestamp_ms\":%lu,\"detail\":\"Periodic controller snapshot.\",\"payload\":",
        (unsigned long)status->uptime_ms);
    laser_controller_comms_write_live_snapshot_json(&buffer, status);
    laser_controller_comms_buffer_append_raw(&buffer, "}\n");
    laser_controller_comms_emit_buffer_locked(&buffer, "status_snapshot");
    laser_controller_comms_output_unlock();
}

static void laser_controller_comms_emit_live_telemetry_event(
    const laser_controller_runtime_status_t *status)
{
    laser_controller_comms_buffer_t buffer;

    laser_controller_comms_output_lock();
    laser_controller_comms_buffer_reset(
        &buffer,
        s_frame_buffer,
        sizeof(s_frame_buffer));
    laser_controller_comms_buffer_append_fmt(
        &buffer,
        "{\"type\":\"event\",\"event\":\"live_telemetry\",\"timestamp_ms\":%lu,\"detail\":\"High-rate controller telemetry.\",\"payload\":",
        (unsigned long)status->uptime_ms);
    laser_controller_comms_write_live_telemetry_json(&buffer, status);
    laser_controller_comms_buffer_append_raw(&buffer, "}\n");
    laser_controller_comms_emit_buffer_locked(&buffer, "live_telemetry");
    laser_controller_comms_output_unlock();
}

static void laser_controller_comms_emit_fast_telemetry_event(
    const laser_controller_runtime_status_t *status)
{
    laser_controller_comms_buffer_t buffer;

    laser_controller_comms_output_lock();
    laser_controller_comms_buffer_reset(
        &buffer,
        s_frame_buffer,
        sizeof(s_frame_buffer));
    laser_controller_comms_buffer_append_fmt(
        &buffer,
        "{\"type\":\"event\",\"event\":\"fast_telemetry\",\"timestamp_ms\":%lu,\"detail\":\"High-cadence runtime telemetry.\",\"payload\":",
        (unsigned long)status->uptime_ms);
    laser_controller_comms_write_fast_telemetry_json(&buffer, status);
    laser_controller_comms_buffer_append_raw(&buffer, "}\n");
    laser_controller_comms_emit_buffer_locked(&buffer, "fast_telemetry");
    laser_controller_comms_output_unlock();
}

static void laser_controller_comms_emit_error_response(
    uint32_t id,
    const char *message)
{
    laser_controller_comms_buffer_t buffer;

    laser_controller_comms_output_lock();
    laser_controller_comms_buffer_reset(
        &buffer,
        s_frame_buffer,
        sizeof(s_frame_buffer));
    laser_controller_comms_buffer_append_fmt(
        &buffer,
        "{\"type\":\"resp\",\"id\":%lu,\"ok\":false,\"error\":",
        (unsigned long)id);
    laser_controller_comms_write_escaped_string(&buffer, message);
    laser_controller_comms_buffer_append_raw(&buffer, "}\n");
    laser_controller_comms_emit_buffer_locked(&buffer, "error_response");
    laser_controller_comms_output_unlock();
}

static void laser_controller_comms_format_dac_block_reason(
    char *buffer,
    size_t buffer_len,
    const laser_controller_runtime_status_t *status,
    bool config_action)
{
    const laser_controller_module_status_t *module = NULL;
    const laser_controller_board_dac_readback_t *dac = NULL;

    if (buffer == NULL || buffer_len == 0U) {
        return;
    }

    if (status == NULL) {
        (void)snprintf(
            buffer,
            buffer_len,
            config_action ?
                "DAC config blocked; read STATUS." :
                "DAC write blocked; read STATUS.");
        return;
    }

    module = &status->bringup.modules[LASER_CONTROLLER_MODULE_DAC];
    dac = &status->inputs.dac;

    if (dac->ref_alarm) {
        (void)snprintf(
            buffer,
            buffer_len,
            "%s REF_ALARM asserted on DAC80502. STATUS=0x%04X GAIN=0x%04X CONFIG=0x%04X. On this 3.3 V board the internal 2.5 V reference requires REF-DIV=1.",
            config_action ? "DAC config blocked;" : "DAC write blocked;",
            (unsigned)dac->status_reg,
            (unsigned)dac->gain_reg,
            (unsigned)dac->config_reg);
        return;
    }

    if (!module->expected_present && !module->debug_enabled) {
        (void)snprintf(
            buffer,
            buffer_len,
            "%s DAC module write gate is closed. expected_present=0 debug_enabled=0.",
            config_action ? "DAC config blocked;" : "DAC write blocked;");
        return;
    }

    (void)snprintf(
        buffer,
        buffer_len,
        "%s DAC module armed but peripheral is not healthy. expected_present=%d debug_enabled=%d detected=%d healthy=%d STATUS=0x%04X GAIN=0x%04X CONFIG=0x%04X.",
        config_action ? "DAC config blocked;" : "DAC write blocked;",
        module->expected_present ? 1 : 0,
        module->debug_enabled ? 1 : 0,
        module->detected ? 1 : 0,
        module->healthy ? 1 : 0,
        (unsigned)dac->status_reg,
        (unsigned)dac->gain_reg,
        (unsigned)dac->config_reg);
}

static void laser_controller_comms_emit_full_status_response(
    uint32_t id,
    const laser_controller_runtime_status_t *status)
{
    laser_controller_comms_buffer_t buffer;

    laser_controller_comms_output_lock();
    laser_controller_comms_buffer_reset(
        &buffer,
        s_frame_buffer,
        sizeof(s_frame_buffer));
    laser_controller_comms_buffer_append_fmt(
        &buffer,
        "{\"type\":\"resp\",\"id\":%lu,\"ok\":true,\"result\":",
        (unsigned long)id);
    laser_controller_comms_write_snapshot_json(&buffer, status);
    laser_controller_comms_buffer_append_raw(&buffer, "}\n");
    laser_controller_comms_emit_buffer_locked(&buffer, "status_response");
    laser_controller_comms_output_unlock();
}

static void laser_controller_comms_emit_status_response(
    uint32_t id,
    const laser_controller_runtime_status_t *status)
{
    laser_controller_comms_buffer_t buffer;

    laser_controller_comms_output_lock();
    laser_controller_comms_buffer_reset(
        &buffer,
        s_frame_buffer,
        sizeof(s_frame_buffer));
    laser_controller_comms_buffer_append_fmt(
        &buffer,
        "{\"type\":\"resp\",\"id\":%lu,\"ok\":true,\"result\":",
        (unsigned long)id);
    laser_controller_comms_write_command_snapshot_json(&buffer, status);
    laser_controller_comms_buffer_append_raw(&buffer, "}\n");
    laser_controller_comms_emit_buffer_locked(&buffer, "status_response");
    laser_controller_comms_output_unlock();
}

static void laser_controller_comms_emit_ping_response(
    uint32_t id,
    const laser_controller_runtime_status_t *status)
{
    laser_controller_comms_buffer_t buffer;

    laser_controller_comms_output_lock();
    laser_controller_comms_buffer_reset(
        &buffer,
        s_frame_buffer,
        sizeof(s_frame_buffer));
    laser_controller_comms_buffer_append_fmt(
        &buffer,
        "{\"type\":\"resp\",\"id\":%lu,\"ok\":true,\"result\":{\"alive\":true,\"uptimeSeconds\":%lu,\"state\":",
        (unsigned long)id,
        (unsigned long)(status->uptime_ms / 1000U));
    laser_controller_comms_write_escaped_string(
        &buffer,
        laser_controller_state_name(status->state));
    laser_controller_comms_buffer_append_raw(&buffer, "}}\n");
    laser_controller_comms_emit_buffer_locked(&buffer, "ping_response");
    laser_controller_comms_output_unlock();
}

static void laser_controller_comms_emit_faults_response(
    uint32_t id,
    const laser_controller_runtime_status_t *status)
{
    laser_controller_comms_buffer_t buffer;

    laser_controller_comms_output_lock();
    laser_controller_comms_buffer_reset(
        &buffer,
        s_frame_buffer,
        sizeof(s_frame_buffer));
    laser_controller_comms_buffer_append_fmt(
        &buffer,
        "{\"type\":\"resp\",\"id\":%lu,\"ok\":true,\"result\":{\"latched\":%s,\"activeCode\":",
        (unsigned long)id,
        status->fault_latched ? "true" : "false");
    laser_controller_comms_write_escaped_string(
        &buffer,
        laser_controller_fault_code_name(status->active_fault_code));
    /*
     * Reason strings (added 2026-04-16) — see write_fault_json for the
     * canonical comment. Tolerant parser on the host treats absent
     * fields as empty.
     */
    laser_controller_comms_buffer_append_raw(&buffer, ",\"activeReason\":");
    laser_controller_comms_write_escaped_string(
        &buffer,
        status->active_fault_reason);
    laser_controller_comms_buffer_append_raw(&buffer, ",\"latchedReason\":");
    laser_controller_comms_write_escaped_string(
        &buffer,
        status->latched_fault_reason);
    laser_controller_comms_buffer_append_fmt(
        &buffer,
        ",\"activeCount\":%lu,\"tripCounter\":%lu}}\n",
        (unsigned long)status->active_fault_count,
        (unsigned long)status->trip_counter);
    laser_controller_comms_emit_buffer_locked(&buffer, "faults_response");
    laser_controller_comms_output_unlock();
}

static void laser_controller_comms_emit_io_status_response(
    uint32_t id,
    const laser_controller_runtime_status_t *status,
    bool include_rails,
    bool include_haptic,
    bool include_gpio,
    bool include_power,
    bool include_illumination)
{
    laser_controller_comms_buffer_t buffer;
    bool bringup_open = false;

    laser_controller_comms_output_lock();
    laser_controller_comms_buffer_reset(
        &buffer,
        s_frame_buffer,
        sizeof(s_frame_buffer));
    laser_controller_comms_buffer_append_fmt(
        &buffer,
        "{\"type\":\"resp\",\"id\":%lu,\"ok\":true,\"result\":{",
        (unsigned long)id);
    laser_controller_comms_buffer_append_fmt(
        &buffer,
        "\"session\":{\"uptimeSeconds\":%lu,\"state\":",
        (unsigned long)(status->uptime_ms / 1000U));
    laser_controller_comms_write_escaped_string(
        &buffer,
        laser_controller_state_name(status->state));
    laser_controller_comms_buffer_append_raw(&buffer, ",\"powerTier\":");
    laser_controller_comms_write_escaped_string(
        &buffer,
        laser_controller_comms_power_tier_name(status->power_tier));
    laser_controller_comms_buffer_append_raw(&buffer, "}");

    if (include_rails) {
        laser_controller_comms_buffer_append_fmt(
            &buffer,
            ",\"rails\":{\"ld\":{\"enabled\":%s,\"pgood\":%s},\"tec\":{\"enabled\":%s,\"pgood\":%s}}",
            status->outputs.enable_ld_vin ? "true" : "false",
            status->inputs.ld_rail_pgood ? "true" : "false",
            status->outputs.enable_tec_vin ? "true" : "false",
            status->inputs.tec_rail_pgood ? "true" : "false");
    }

    if (include_haptic) {
        laser_controller_comms_buffer_append_raw(&buffer, ",");
        laser_controller_comms_write_peripheral_subset_json(
            &buffer,
            status,
            LASER_CONTROLLER_COMMS_PERIPHERAL_HAPTIC);
    }

    if (include_gpio) {
        laser_controller_comms_buffer_append_raw(&buffer, ",");
        laser_controller_comms_write_gpio_inspector_json(&buffer, status);
        if (buffer.length > 0U && buffer.data[buffer.length - 1U] == ',') {
            buffer.data[--buffer.length] = '\0';
        }
    }

    if (include_power || include_illumination || status->bringup.service_mode_requested ||
        status->state == LASER_CONTROLLER_STATE_SERVICE_MODE) {
        laser_controller_comms_buffer_append_raw(
            &buffer,
            ",\"bringup\":{");
        bringup_open = true;
        laser_controller_comms_buffer_append_fmt(
            &buffer,
            "\"serviceModeRequested\":%s,\"serviceModeActive\":%s,\"interlocksDisabled\":%s",
            status->bringup.service_mode_requested ? "true" : "false",
            status->state == LASER_CONTROLLER_STATE_SERVICE_MODE ? "true" : "false",
            status->bringup.interlocks_disabled ? "true" : "false");
        if (include_power) {
            laser_controller_comms_buffer_append_fmt(
                &buffer,
                ",\"power\":{\"ldRequested\":%s,\"tecRequested\":%s}",
                status->bringup.ld_rail_debug_enabled ? "true" : "false",
                status->bringup.tec_rail_debug_enabled ? "true" : "false");
        }
        if (include_illumination) {
            laser_controller_comms_buffer_append_fmt(
                &buffer,
                ",\"illumination\":{\"tof\":{\"enabled\":%s,\"dutyCyclePct\":%lu,\"frequencyHz\":%lu}}",
                status->bringup.tof_illumination_enabled ? "true" : "false",
                (unsigned long)status->bringup.tof_illumination_duty_cycle_pct,
                (unsigned long)status->bringup.tof_illumination_frequency_hz);
        }
    }

    if (bringup_open) {
        laser_controller_comms_buffer_append_raw(&buffer, "}");
    }

    laser_controller_comms_buffer_append_fmt(
        &buffer,
        ",\"fault\":{\"latched\":%s,\"activeCode\":",
        status->fault_latched ? "true" : "false");
    laser_controller_comms_write_escaped_string(
        &buffer,
        laser_controller_fault_code_name(status->active_fault_code));
    laser_controller_comms_buffer_append_fmt(
        &buffer,
        ",\"activeCount\":%lu,\"tripCounter\":%lu}}}\n",
        (unsigned long)status->active_fault_count,
        (unsigned long)status->trip_counter);
    laser_controller_comms_emit_buffer_locked(&buffer, "io_status_response");
    laser_controller_comms_output_unlock();
}

static void laser_controller_comms_emit_tool_status_response(
    uint32_t id,
    const laser_controller_runtime_status_t *status,
    uint32_t peripheral_mask,
    bool include_pd,
    bool include_modules,
    bool include_tools)
{
    laser_controller_comms_buffer_t buffer;
    bool bringup_open = false;

    laser_controller_comms_output_lock();
    laser_controller_comms_buffer_reset(
        &buffer,
        s_frame_buffer,
        sizeof(s_frame_buffer));
    laser_controller_comms_buffer_append_fmt(
        &buffer,
        "{\"type\":\"resp\",\"id\":%lu,\"ok\":true,\"result\":{",
        (unsigned long)id);
    laser_controller_comms_buffer_append_fmt(
        &buffer,
        "\"session\":{\"uptimeSeconds\":%lu,\"state\":",
        (unsigned long)(status->uptime_ms / 1000U));
    laser_controller_comms_write_escaped_string(
        &buffer,
        laser_controller_state_name(status->state));
    laser_controller_comms_buffer_append_raw(&buffer, ",\"powerTier\":");
    laser_controller_comms_write_escaped_string(
        &buffer,
        laser_controller_comms_power_tier_name(status->power_tier));
    laser_controller_comms_buffer_append_raw(&buffer, "}");

    if (include_pd) {
        laser_controller_comms_buffer_append_fmt(
            &buffer,
            ",\"pd\":{\"contractValid\":%s,\"negotiatedPowerW\":%.2f,\"sourceVoltageV\":%.2f,\"sourceCurrentA\":%.2f,\"operatingCurrentA\":%.2f,\"sourceIsHostOnly\":%s,\"lastUpdatedMs\":%lu,\"snapshotFresh\":%s,\"source\":",
            status->inputs.pd_contract_valid ? "true" : "false",
            status->inputs.pd_negotiated_power_w,
            status->inputs.pd_source_voltage_v,
            status->inputs.pd_source_current_a,
            status->inputs.pd_operating_current_a,
            status->inputs.pd_source_is_host_only ? "true" : "false",
            (unsigned long)status->inputs.pd_last_updated_ms,
            status->inputs.pd_snapshot_fresh ? "true" : "false");
        laser_controller_comms_write_escaped_string(
            &buffer,
            laser_controller_comms_pd_snapshot_source_name(
                status->inputs.pd_source));
        laser_controller_comms_buffer_append_raw(&buffer, "}");
    }

    if (peripheral_mask != LASER_CONTROLLER_COMMS_PERIPHERAL_NONE) {
        laser_controller_comms_buffer_append_raw(&buffer, ",");
        laser_controller_comms_write_peripheral_subset_json(
            &buffer,
            status,
            peripheral_mask);
    }

    if (include_modules || include_tools ||
        status->bringup.service_mode_requested ||
        status->state == LASER_CONTROLLER_STATE_SERVICE_MODE) {
        laser_controller_comms_buffer_append_raw(&buffer, ",\"bringup\":{");
        bringup_open = true;
        laser_controller_comms_buffer_append_fmt(
            &buffer,
            "\"serviceModeRequested\":%s,\"serviceModeActive\":%s,\"interlocksDisabled\":%s",
            status->bringup.service_mode_requested ? "true" : "false",
            status->state == LASER_CONTROLLER_STATE_SERVICE_MODE ? "true" : "false",
            status->bringup.interlocks_disabled ? "true" : "false");
        if (include_modules) {
            laser_controller_comms_buffer_append_raw(&buffer, ",");
            laser_controller_comms_write_modules_only_json(&buffer, status);
        }
        if (include_tools) {
            laser_controller_comms_buffer_append_raw(&buffer, ",");
            laser_controller_comms_write_tools_only_json(&buffer, status);
        }
    }

    if (bringup_open) {
        laser_controller_comms_buffer_append_raw(&buffer, "}");
    }

    laser_controller_comms_buffer_append_fmt(
        &buffer,
        ",\"fault\":{\"latched\":%s,\"activeCode\":",
        status->fault_latched ? "true" : "false");
    laser_controller_comms_write_escaped_string(
        &buffer,
        laser_controller_fault_code_name(status->active_fault_code));
    laser_controller_comms_buffer_append_fmt(
        &buffer,
        ",\"activeCount\":%lu,\"tripCounter\":%lu}}}\n",
        (unsigned long)status->active_fault_count,
        (unsigned long)status->trip_counter);
    laser_controller_comms_emit_buffer_locked(&buffer, "tool_status_response");
    laser_controller_comms_output_unlock();
}

static void laser_controller_comms_emit_profile_status_response(
    uint32_t id,
    const laser_controller_runtime_status_t *status)
{
    laser_controller_comms_buffer_t buffer;

    laser_controller_comms_output_lock();
    laser_controller_comms_buffer_reset(
        &buffer,
        s_frame_buffer,
        sizeof(s_frame_buffer));
    laser_controller_comms_buffer_append_fmt(
        &buffer,
        "{\"type\":\"resp\",\"id\":%lu,\"ok\":true,\"result\":{",
        (unsigned long)id);
    laser_controller_comms_buffer_append_fmt(
        &buffer,
        "\"session\":{\"uptimeSeconds\":%lu,\"state\":",
        (unsigned long)(status->uptime_ms / 1000U));
    laser_controller_comms_write_escaped_string(
        &buffer,
        laser_controller_state_name(status->state));
    laser_controller_comms_buffer_append_raw(&buffer, ",\"powerTier\":");
    laser_controller_comms_write_escaped_string(
        &buffer,
        laser_controller_comms_power_tier_name(status->power_tier));
    laser_controller_comms_buffer_append_fmt(
        &buffer,
        "},\"bringup\":{\"serviceModeRequested\":%s,\"serviceModeActive\":%s,\"interlocksDisabled\":%s,\"persistenceDirty\":%s,\"persistenceAvailable\":%s,\"lastSaveOk\":%s,\"lastSaveAtMs\":%lu,\"profileRevision\":%lu,\"profileName\":",
        status->bringup.service_mode_requested ? "true" : "false",
        status->state == LASER_CONTROLLER_STATE_SERVICE_MODE ? "true" : "false",
        status->bringup.interlocks_disabled ? "true" : "false",
        status->bringup.persistence_dirty ? "true" : "false",
        status->bringup.persistence_available ? "true" : "false",
        status->bringup.last_save_ok ? "true" : "false",
        (unsigned long)status->bringup.last_save_at_ms,
        (unsigned long)status->bringup.profile_revision);
    laser_controller_comms_write_escaped_string(
        &buffer,
        status->bringup.profile_name);
    laser_controller_comms_buffer_append_raw(&buffer, "},");
    laser_controller_comms_write_fault_json(&buffer, status, true);
    laser_controller_comms_buffer_append_raw(&buffer, "}}\n");
    laser_controller_comms_emit_buffer_locked(&buffer, "profile_status_response");
    laser_controller_comms_output_unlock();
}

static void laser_controller_comms_emit_bench_status_response(
    uint32_t id,
    const laser_controller_runtime_status_t *status,
    bool include_tec,
    bool include_bench,
    bool include_safety)
{
    laser_controller_comms_buffer_t buffer;
    const float beam_pitch_limit_deg =
        status->config.thresholds.horizon_threshold_rad * LASER_CONTROLLER_DEG_PER_RAD;
    const float beam_pitch_hysteresis_deg =
        status->config.thresholds.horizon_hysteresis_rad * LASER_CONTROLLER_DEG_PER_RAD;
    const float actual_lambda_nm =
        laser_controller_comms_actual_lambda_nm(status);
    const float lambda_drift_nm =
        laser_controller_comms_lambda_drift_nm(status);

    laser_controller_comms_output_lock();
    laser_controller_comms_buffer_reset(
        &buffer,
        s_frame_buffer,
        sizeof(s_frame_buffer));
    laser_controller_comms_buffer_append_fmt(
        &buffer,
        "{\"type\":\"resp\",\"id\":%lu,\"ok\":true,\"result\":{",
        (unsigned long)id);
    laser_controller_comms_buffer_append_fmt(
        &buffer,
        "\"session\":{\"uptimeSeconds\":%lu,\"state\":",
        (unsigned long)(status->uptime_ms / 1000U));
    laser_controller_comms_write_escaped_string(
        &buffer,
        laser_controller_state_name(status->state));
    laser_controller_comms_buffer_append_raw(&buffer, ",\"powerTier\":");
    laser_controller_comms_write_escaped_string(
        &buffer,
        laser_controller_comms_power_tier_name(status->power_tier));
    laser_controller_comms_buffer_append_fmt(
        &buffer,
        "},\"rails\":{\"ld\":{\"enabled\":%s,\"pgood\":%s},\"tec\":{\"enabled\":%s,\"pgood\":%s}},"
        "\"laser\":{\"alignmentEnabled\":%s,\"nirEnabled\":%s,\"driverStandby\":%s,\"telemetryValid\":%s,\"measuredCurrentA\":%.3f,\"commandedCurrentA\":%.3f,\"loopGood\":%s,\"driverTempC\":%.2f}",
        status->outputs.enable_ld_vin ? "true" : "false",
        status->inputs.ld_rail_pgood ? "true" : "false",
        status->outputs.enable_tec_vin ? "true" : "false",
        status->inputs.tec_rail_pgood ? "true" : "false",
        status->outputs.enable_alignment_laser ? "true" : "false",
        status->decision.nir_output_enable ? "true" : "false",
        laser_controller_comms_driver_standby_effective(status) ? "true" : "false",
        status->inputs.ld_telemetry_valid ? "true" : "false",
        status->inputs.measured_laser_current_a,
        laser_controller_comms_effective_commanded_current_a(status),
        status->inputs.driver_loop_good ? "true" : "false",
        status->inputs.laser_driver_temp_c);

    if (include_tec) {
        laser_controller_comms_buffer_append_fmt(
            &buffer,
            ",\"tec\":{\"targetTempC\":%.2f,\"targetLambdaNm\":%.2f,\"actualLambdaNm\":%.2f,\"telemetryValid\":%s,\"tempGood\":%s,\"tempC\":%.2f,\"tempAdcVoltageV\":%.3f,\"currentA\":%.2f,\"voltageV\":%.2f,\"settlingSecondsRemaining\":%u}",
            status->bench.target_temp_c,
            status->bench.target_lambda_nm,
            actual_lambda_nm,
            status->inputs.tec_telemetry_valid ? "true" : "false",
            status->inputs.tec_temp_good ? "true" : "false",
            status->inputs.tec_temp_c,
            status->inputs.tec_temp_adc_voltage_v,
            status->inputs.tec_current_a,
            status->inputs.tec_voltage_v,
            status->inputs.tec_temp_good ? 0U : 1U);
    }

    if (include_bench) {
        laser_controller_comms_buffer_append_raw(&buffer, ",");
        laser_controller_comms_write_bench_json(&buffer, status);
    }

    if (include_safety) {
        laser_controller_comms_buffer_append_fmt(
            &buffer,
            ",\"safety\":{\"allowAlignment\":%s,\"allowNir\":%s,\"horizonBlocked\":%s,\"distanceBlocked\":%s,\"lambdaDriftBlocked\":%s,\"tecTempAdcBlocked\":%s,\"horizonThresholdDeg\":%.2f,\"horizonHysteresisDeg\":%.2f,\"tofMinRangeM\":%.3f,\"tofMaxRangeM\":%.3f,\"tofHysteresisM\":%.3f,\"imuStaleMs\":%lu,\"tofStaleMs\":%lu,\"railGoodTimeoutMs\":%lu,\"lambdaDriftLimitNm\":%.2f,\"lambdaDriftHysteresisNm\":%.2f,\"lambdaDriftHoldMs\":%lu,\"ldOvertempLimitC\":%.2f,\"tecTempAdcTripV\":%.3f,\"tecTempAdcHysteresisV\":%.3f,\"tecTempAdcHoldMs\":%lu,\"tecMinCommandC\":%.2f,\"tecMaxCommandC\":%.2f,\"tecReadyToleranceC\":%.2f,\"maxLaserCurrentA\":%.2f,\"offCurrentThresholdA\":%.2f,\"maxTofLedDutyCyclePct\":%u,\"lioVoltageOffsetV\":%.4f,\"actualLambdaNm\":%.2f,\"targetLambdaNm\":%.2f,\"lambdaDriftNm\":%.2f,\"tempAdcVoltageV\":%.3f,\"interlocks\":{\"horizonEnabled\":%s,\"distanceEnabled\":%s,\"lambdaDriftEnabled\":%s,\"tecTempAdcEnabled\":%s,\"imuInvalidEnabled\":%s,\"imuStaleEnabled\":%s,\"tofInvalidEnabled\":%s,\"tofStaleEnabled\":%s,\"ldOvertempEnabled\":%s,\"ldLoopBadEnabled\":%s,\"tofLowBoundOnly\":%s},\"autoDeployOnBoot\":%s}",
            status->decision.allow_alignment ? "true" : "false",
            status->decision.allow_nir ? "true" : "false",
            status->decision.horizon_blocked ? "true" : "false",
            status->decision.distance_blocked ? "true" : "false",
            status->decision.lambda_drift_blocked ? "true" : "false",
            status->decision.tec_temp_adc_blocked ? "true" : "false",
            beam_pitch_limit_deg,
            beam_pitch_hysteresis_deg,
            status->config.thresholds.tof_min_range_m,
            status->config.thresholds.tof_max_range_m,
            status->config.thresholds.tof_hysteresis_m,
            (unsigned long)status->config.timeouts.imu_stale_ms,
            (unsigned long)status->config.timeouts.tof_stale_ms,
            (unsigned long)status->config.timeouts.rail_good_timeout_ms,
            status->config.thresholds.lambda_drift_limit_nm,
            status->config.thresholds.lambda_drift_hysteresis_nm,
            (unsigned long)status->config.timeouts.lambda_drift_hold_ms,
            status->config.thresholds.ld_overtemp_limit_c,
            status->config.thresholds.tec_temp_adc_trip_v,
            status->config.thresholds.tec_temp_adc_hysteresis_v,
            (unsigned long)status->config.timeouts.tec_temp_adc_hold_ms,
            status->config.thresholds.tec_min_command_c,
            status->config.thresholds.tec_max_command_c,
            status->config.thresholds.tec_ready_tolerance_c,
            status->config.thresholds.max_laser_current_a,
            status->config.thresholds.off_current_threshold_a,
            (unsigned)status->config.thresholds.max_tof_led_duty_cycle_pct,
            (double)status->config.thresholds.lio_voltage_offset_v,
            actual_lambda_nm,
            status->bench.target_lambda_nm,
            lambda_drift_nm,
            status->inputs.tec_temp_adc_voltage_v,
            status->config.thresholds.interlocks.horizon_enabled ? "true" : "false",
            status->config.thresholds.interlocks.distance_enabled ? "true" : "false",
            status->config.thresholds.interlocks.lambda_drift_enabled ? "true" : "false",
            status->config.thresholds.interlocks.tec_temp_adc_enabled ? "true" : "false",
            status->config.thresholds.interlocks.imu_invalid_enabled ? "true" : "false",
            status->config.thresholds.interlocks.imu_stale_enabled ? "true" : "false",
            status->config.thresholds.interlocks.tof_invalid_enabled ? "true" : "false",
            status->config.thresholds.interlocks.tof_stale_enabled ? "true" : "false",
            status->config.thresholds.interlocks.ld_overtemp_enabled ? "true" : "false",
            status->config.thresholds.interlocks.ld_loop_bad_enabled ? "true" : "false",
            status->config.thresholds.interlocks.tof_low_bound_only ? "true" : "false",
            (status->config.service_flags &
             LASER_CONTROLLER_SERVICE_FLAG_AUTO_DEPLOY_ON_BOOT) != 0U ?
                "true" : "false");
    }

    laser_controller_comms_buffer_append_fmt(
        &buffer,
        ",\"bringup\":{\"serviceModeRequested\":%s,\"serviceModeActive\":%s,\"interlocksDisabled\":%s},\"fault\":{\"latched\":%s,\"activeCode\":",
        status->bringup.service_mode_requested ? "true" : "false",
        status->state == LASER_CONTROLLER_STATE_SERVICE_MODE ? "true" : "false",
        status->bringup.interlocks_disabled ? "true" : "false",
        status->fault_latched ? "true" : "false");
    laser_controller_comms_write_escaped_string(
        &buffer,
        laser_controller_fault_code_name(status->active_fault_code));
    laser_controller_comms_buffer_append_fmt(
        &buffer,
        ",\"activeCount\":%lu,\"tripCounter\":%lu}}}\n",
        (unsigned long)status->active_fault_count,
        (unsigned long)status->trip_counter);
    laser_controller_comms_emit_buffer_locked(&buffer, "bench_status_response");
    laser_controller_comms_output_unlock();
}

static const char *laser_controller_comms_find_value_start(
    const char *line,
    const char *key,
    bool quoted)
{
    char bare_key[64];
    size_t bare_len = 0U;
    const char *match = NULL;

    if (line == NULL || key == NULL) {
        return NULL;
    }

    match = strstr(line, key);
    if (match != NULL) {
        return match + strlen(key);
    }

    while (key[bare_len] != '\0' &&
           key[bare_len] != ':' &&
           bare_len < (sizeof(bare_key) - 1U)) {
        bare_key[bare_len] = key[bare_len];
        ++bare_len;
    }
    bare_key[bare_len] = '\0';

    if (bare_len == 0U) {
        return NULL;
    }

    match = line;
    while ((match = strstr(match, bare_key)) != NULL) {
        const char *cursor = match + bare_len;

        while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
            ++cursor;
        }
        if (*cursor != ':') {
            ++match;
            continue;
        }

        ++cursor;
        while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
            ++cursor;
        }

        if (quoted) {
            if (*cursor != '\"') {
                ++match;
                continue;
            }
            ++cursor;
        }

        return cursor;
    }

    return NULL;
}

static size_t laser_controller_comms_extract_bare_key(
    const char *key,
    char *bare_key,
    size_t bare_key_len)
{
    size_t length = 0U;
    const char *cursor = key;

    if (key == NULL || bare_key == NULL || bare_key_len == 0U) {
        return 0U;
    }

    if (*cursor == '\"') {
        ++cursor;
    }

    while (*cursor != '\0' &&
           *cursor != '\"' &&
           *cursor != ':' &&
           length < (bare_key_len - 1U)) {
        bare_key[length++] = *cursor++;
    }

    bare_key[length] = '\0';
    return length;
}

static const char *laser_controller_comms_find_root_value_start(
    const char *line,
    const char *key,
    bool quoted)
{
    char bare_key[64];
    size_t bare_len = 0U;
    const char *cursor = line;
    int brace_depth = 0;

    bare_len = laser_controller_comms_extract_bare_key(
        key,
        bare_key,
        sizeof(bare_key));
    if (line == NULL || bare_len == 0U) {
        return NULL;
    }

    while (*cursor != '\0') {
        if (*cursor == '{') {
            ++brace_depth;
            ++cursor;
            continue;
        }

        if (*cursor == '}') {
            if (brace_depth > 0) {
                --brace_depth;
            }
            ++cursor;
            continue;
        }

        if (*cursor != '\"') {
            ++cursor;
            continue;
        }

        const char *token_start = cursor + 1;
        const char *token_end = token_start;
        bool escaped = false;

        while (*token_end != '\0') {
            if (escaped) {
                escaped = false;
            } else if (*token_end == '\\') {
                escaped = true;
            } else if (*token_end == '\"') {
                break;
            }
            ++token_end;
        }

        if (*token_end == '\0') {
            return NULL;
        }

        if (brace_depth == 1 &&
            (size_t)(token_end - token_start) == bare_len &&
            strncmp(token_start, bare_key, bare_len) == 0) {
            const char *value_start = token_end + 1;

            while (*value_start != '\0' && isspace((unsigned char)*value_start)) {
                ++value_start;
            }
            if (*value_start != ':') {
                cursor = token_end + 1;
                continue;
            }

            ++value_start;
            while (*value_start != '\0' && isspace((unsigned char)*value_start)) {
                ++value_start;
            }
            if (quoted) {
                if (*value_start != '\"') {
                    cursor = token_end + 1;
                    continue;
                }
                ++value_start;
            }
            return value_start;
        }

        cursor = token_end + 1;
    }

    return NULL;
}

static bool laser_controller_comms_extract_uint(
    const char *line,
    const char *key,
    uint32_t *value)
{
    const char *match = laser_controller_comms_find_value_start(line, key, false);
    char *end_ptr = NULL;
    unsigned long parsed;

    if (match == NULL || value == NULL) {
        return false;
    }

    if (*match == '\"') {
        ++match;
    }
    parsed = strtoul(match, &end_ptr, 0);
    if (end_ptr == match) {
        return false;
    }

    *value = (uint32_t)parsed;
    return true;
}

static bool laser_controller_comms_extract_root_uint(
    const char *line,
    const char *key,
    uint32_t *value)
{
    const char *match = laser_controller_comms_find_root_value_start(line, key, false);
    char *end_ptr = NULL;
    unsigned long parsed;

    if (match == NULL || value == NULL) {
        return false;
    }

    if (*match == '\"') {
        ++match;
    }
    parsed = strtoul(match, &end_ptr, 0);
    if (end_ptr == match) {
        return false;
    }

    *value = (uint32_t)parsed;
    return true;
}

static bool laser_controller_comms_extract_float(
    const char *line,
    const char *key,
    float *value)
{
    const char *match = laser_controller_comms_find_value_start(line, key, false);
    char *end_ptr = NULL;
    float parsed;

    if (match == NULL || value == NULL) {
        return false;
    }

    if (*match == '\"') {
        ++match;
    }
    parsed = strtof(match, &end_ptr);
    if (end_ptr == match) {
        return false;
    }

    *value = parsed;
    return true;
}

static bool laser_controller_comms_extract_bool(
    const char *line,
    const char *key,
    bool *value)
{
    const char *match = laser_controller_comms_find_value_start(line, key, false);

    if (match == NULL || value == NULL) {
        return false;
    }

    /*
     * 2026-04-20: skip any whitespace between `:` and the value. The
     * fast-path `strstr(line, key)` in `find_value_start` returns a pointer
     * right after the literal key (including the colon), so pretty-printed
     * JSON like `"key": true` would land `match` on a leading space and
     * the strncmp below would silently fail — the bool field would
     * appear to not be present. `extract_float` / `extract_uint` use
     * `strtof`/`strtoul` which skip whitespace natively, so only the bool
     * path needed the fix.
     */
    while (*match != '\0' &&
           (*match == ' ' || *match == '\t' ||
            *match == '\r' || *match == '\n')) {
        ++match;
    }

    if (*match == '\"') {
        ++match;
    }

    if (strncmp(match, "true", 4) == 0) {
        *value = true;
        return true;
    }

    if (strncmp(match, "false", 5) == 0) {
        *value = false;
        return true;
    }

    return false;
}

static void laser_controller_comms_refresh_status_after_mutation(
    laser_controller_runtime_status_t *status,
    uint32_t wait_ms)
{
    if (status == NULL) {
        return;
    }

    if (wait_ms > 0U) {
        vTaskDelay(pdMS_TO_TICKS(wait_ms));
    }

    (void)laser_controller_app_copy_status(status);
}

static bool laser_controller_comms_extract_string(
    const char *line,
    const char *key,
    char *value,
    size_t value_len)
{
    const char *match = laser_controller_comms_find_value_start(line, key, true);
    const char *end = NULL;
    size_t length;

    if (match == NULL || value == NULL || value_len == 0U) {
        return false;
    }
    end = strchr(match, '\"');
    if (end == NULL) {
        return false;
    }

    length = (size_t)(end - match);
    if (length >= value_len) {
        length = value_len - 1U;
    }

    memcpy(value, match, length);
    value[length] = '\0';
    return true;
}

static bool laser_controller_comms_extract_root_string(
    const char *line,
    const char *key,
    char *value,
    size_t value_len)
{
    const char *match = laser_controller_comms_find_root_value_start(line, key, true);
    const char *end = NULL;
    size_t length;

    if (match == NULL || value == NULL || value_len == 0U) {
        return false;
    }

    end = strchr(match, '\"');
    if (end == NULL) {
        return false;
    }

    length = (size_t)(end - match);
    if (length >= value_len) {
        length = value_len - 1U;
    }

    memcpy(value, match, length);
    value[length] = '\0';
    return true;
}

static void laser_controller_comms_handle_command_line(const char *line)
{
    char command[48];
    char envelope_type[16];
    char text_arg[48];
    uint32_t id = 0U;
    laser_controller_runtime_status_t status;
    const laser_controller_time_ms_t now_ms = laser_controller_board_uptime_ms();

    if (line == NULL) {
        return;
    }

    if (laser_controller_comms_extract_root_string(
            line,
            "\"type\":\"",
            envelope_type,
            sizeof(envelope_type)) &&
        strcmp(envelope_type, "cmd") != 0 &&
        strcmp(envelope_type, "command") != 0) {
        return;
    }

    if (!laser_controller_comms_extract_root_uint(line, "\"id\":", &id) ||
        (!laser_controller_comms_extract_root_string(
             line,
             "\"cmd\":\"",
             command,
             sizeof(command)) &&
         !laser_controller_comms_extract_root_string(
             line,
             "\"command\":\"",
             command,
             sizeof(command)))) {
        laser_controller_comms_emit_error_response(id, "Malformed command envelope.");
        return;
    }

    if (!laser_controller_app_copy_status(&status)) {
        laser_controller_comms_emit_error_response(id, "Controller status unavailable.");
        return;
    }

    if (strcmp(command, "ping") == 0) {
        laser_controller_comms_emit_ping_response(id, &status);
        return;
    }

    if (strcmp(command, "get_status") == 0 ||
        strcmp(command, "status.get") == 0) {
        laser_controller_comms_emit_full_status_response(id, &status);
        return;
    }

    if (strcmp(command, "get_faults") == 0) {
        laser_controller_comms_emit_faults_response(id, &status);
        return;
    }

    if (strcmp(command, "enter_deployment_mode") == 0 ||
        strcmp(command, "deployment.enter") == 0) {
        if (status.fault_latched) {
            laser_controller_comms_emit_error_response(
                id,
                "Clear the blocking latched fault before entering deployment mode.");
            return;
        }
        /*
         * 2026-04-17 (audit round 2, S2): pre-check the
         * already-active case so the operator sees a specific
         * error message instead of the generic "could not be
         * entered from the current controller state" string.
         */
        if (status.deployment.active) {
            laser_controller_comms_emit_error_response(
                id,
                "Deployment mode is already active. Use 'Exit deployment' first if you want to restart it.");
            return;
        }
        if (laser_controller_app_enter_deployment_mode() != ESP_OK) {
            laser_controller_comms_emit_error_response(
                id,
                "Deployment mode could not be entered from the current controller state.");
            return;
        }
        if (!laser_controller_comms_wait_for_deployment_mode(true, &status)) {
            laser_controller_comms_emit_error_response(
                id,
                "Deployment mode did not become active before the command timed out.");
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(60U));
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_full_status_response(id, &status);
        return;
    }

    if (strcmp(command, "exit_deployment_mode") == 0 ||
        strcmp(command, "deployment.exit") == 0) {
        if (laser_controller_app_exit_deployment_mode() != ESP_OK) {
            laser_controller_comms_emit_error_response(
                id,
                "Deployment mode could not be exited while the deployment sequence is still running.");
            return;
        }
        if (!laser_controller_comms_wait_for_deployment_mode(false, &status)) {
            laser_controller_comms_emit_error_response(
                id,
                "Deployment mode did not clear before the command timed out.");
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(60U));
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_full_status_response(id, &status);
        return;
    }

    if (strcmp(command, "run_deployment_sequence") == 0 ||
        strcmp(command, "deployment.run") == 0) {
        if (status.fault_latched) {
            laser_controller_comms_emit_error_response(
                id,
                "Clear the blocking latched fault before running the deployment checklist.");
            return;
        }
        if (laser_controller_app_run_deployment_sequence() != ESP_OK) {
            laser_controller_comms_emit_error_response(
                id,
                "Deployment sequence could not start from the current controller state.");
            return;
        }
        if (strcmp(command, "deployment.run") == 0) {
            laser_controller_comms_refresh_status_after_mutation(&status, 25U);
            laser_controller_comms_emit_status_response(id, &status);
            return;
        }
        if (!laser_controller_comms_wait_for_deployment_result(&status)) {
            laser_controller_comms_emit_error_response(
                id,
                "Deployment sequence did not finish before the command timed out.");
            return;
        }
        laser_controller_comms_emit_status_response(id, &status);
        return;
    }

    if (strcmp(command, "set_deployment_target") == 0 ||
        strcmp(command, "deployment.set_target") == 0) {
        laser_controller_deployment_target_t target = {
            .target_mode = LASER_CONTROLLER_DEPLOYMENT_TARGET_MODE_TEMP,
            .target_temp_c = status.deployment.target.target_temp_c,
            .target_lambda_nm = status.deployment.target.target_lambda_nm,
        };

        if (status.deployment.running) {
            laser_controller_comms_emit_error_response(
                id,
                "Deployment target edits are blocked while the deployment checklist is running.");
            return;
        }
        if (status.deployment.ready) {
            laser_controller_comms_emit_error_response(
                id,
                "Deployment target edits are locked once deployment is ready. Use the Control page runtime target instead.");
            return;
        }

        if (laser_controller_comms_extract_string(
                line,
                "\"target_mode\":\"",
                text_arg,
                sizeof(text_arg))) {
            if (strcmp(text_arg, "lambda") == 0) {
                target.target_mode =
                    LASER_CONTROLLER_DEPLOYMENT_TARGET_MODE_LAMBDA;
            } else {
                target.target_mode =
                    LASER_CONTROLLER_DEPLOYMENT_TARGET_MODE_TEMP;
            }
        }

        if (laser_controller_comms_extract_float(line, "\"temp_c\":", &target.target_temp_c)) {
            target.target_mode = LASER_CONTROLLER_DEPLOYMENT_TARGET_MODE_TEMP;
        }
        if (laser_controller_comms_extract_float(
                line,
                "\"lambda_nm\":",
                &target.target_lambda_nm)) {
            target.target_mode = LASER_CONTROLLER_DEPLOYMENT_TARGET_MODE_LAMBDA;
        }

        if (laser_controller_app_set_deployment_target(&target) != ESP_OK) {
            laser_controller_comms_emit_error_response(
                id,
                "Deployment target could not be applied because deployment mode is not active.");
            return;
        }

        laser_controller_comms_refresh_status_after_mutation(&status, 25U);
        laser_controller_comms_emit_status_response(id, &status);
        return;
    }

    if (strcmp(command, "clear_faults") == 0) {
        /*
         * Loosened 2026-04-16: clear_faults previously required both
         * !deployment.active AND service_mode_active, forcing operators
         * through a 5-step exit-deploy / enter-service / clear /
         * exit-service / re-enter-deploy dance for a single fault clear.
         * Clearing the latch is idempotent — if the underlying condition
         * persists, the safety evaluator re-latches on the next 5 ms tick.
         * Only mid-emission clearing is hazardous (would reset the gating
         * mid-fire), so refuse only in NIR_ACTIVE / ALIGNMENT_ACTIVE.
         */
        if (status.state == LASER_CONTROLLER_STATE_NIR_ACTIVE ||
            status.state == LASER_CONTROLLER_STATE_ALIGNMENT_ACTIVE) {
            laser_controller_comms_emit_error_response(
                id,
                "Cannot clear faults while NIR or alignment is emitting. Stop the output first.");
            return;
        }

        if (laser_controller_app_clear_fault_latch() != ESP_OK) {
            laser_controller_comms_emit_error_response(
                id,
                "Fault latch clear rejected because recovery criteria are not met.");
            return;
        }

        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_io_status_response(
            id,
            &status,
            true,
            false,
            false,
            true,
            true);
        return;
    }

    if (strcmp(command, "enter_service_mode") == 0) {
        if (status.deployment.active) {
            laser_controller_comms_emit_error_response(
                id,
                "Deployment mode is active. Exit deployment mode before opening service writes.");
            return;
        }
        laser_controller_service_set_mode_requested(true, now_ms);
        laser_controller_logger_log(now_ms, "service", "service mode requested");
        if (!laser_controller_comms_wait_for_service_mode(true, &status)) {
            laser_controller_comms_emit_error_response(
                id,
                "Service mode request timed out before the controller entered write-safe state.");
            return;
        }
        laser_controller_comms_emit_io_status_response(
            id,
            &status,
            true,
            false,
            false,
            true,
            true);
        return;
    }

    if (strcmp(command, "exit_service_mode") == 0) {
        laser_controller_service_set_mode_requested(false, now_ms);
        laser_controller_logger_log(now_ms, "service", "service mode exited");
        if (!laser_controller_comms_wait_for_service_mode(false, &status)) {
            laser_controller_comms_emit_error_response(
                id,
                "Service mode exit timed out before the controller returned to normal supervision.");
            return;
        }
        laser_controller_comms_emit_io_status_response(
            id,
            &status,
            true,
            false,
            false,
            true,
            true);
        return;
    }

    /*
     * USB-Debug Mock — explicit opt-in only. Hard-isolated per AGENT.md.
     * The mock substitutes synthesized rail PGOOD and TEC/LD telemetry
     * inside read_inputs so the GUI / state machine / protocol paths can
     * be exercised end-to-end from a USB-only session where TEC/LD rails
     * physically cannot come up. The mock NEVER drives any GPIO and
     * auto-disables the moment real PD power is detected.
     */
    if (strcmp(command, "service.usb_debug_mock_enable") == 0) {
        if (!laser_controller_comms_service_mode_active(&status)) {
            laser_controller_comms_emit_error_response(
                id,
                "Enter service mode before enabling the USB debug mock.");
            return;
        }
        if (status.power_tier != LASER_CONTROLLER_POWER_TIER_PROGRAMMING_ONLY) {
            laser_controller_comms_emit_error_response(
                id,
                "USB debug mock only engages when power_tier is programming_only. "
                "Real bench power is currently negotiated.");
            return;
        }
        if (status.fault_latched) {
            laser_controller_comms_emit_error_response(
                id,
                "Clear the latched fault before enabling the USB debug mock.");
            return;
        }
        const esp_err_t err = laser_controller_usb_debug_mock_request_enable(
            true,
            status.power_tier,
            now_ms);
        if (err != ESP_OK) {
            laser_controller_comms_emit_error_response(
                id,
                "USB debug mock enable rejected (guard re-check failed).");
            return;
        }
        laser_controller_comms_refresh_status_after_mutation(&status, 25U);
        laser_controller_comms_emit_io_status_response(
            id,
            &status,
            true,
            false,
            false,
            true,
            true);
        return;
    }

    if (strcmp(command, "service.usb_debug_mock_disable") == 0) {
        /*
         * Disable is always accepted. No service-mode requirement so the
         * operator can bail out of mock mode even if service-mode exit
         * already happened (which itself auto-disables, but belt-and-
         * suspenders).
         */
        laser_controller_usb_debug_mock_request_disable(
            "operator request",
            now_ms);
        laser_controller_comms_refresh_status_after_mutation(&status, 25U);
        laser_controller_comms_emit_io_status_response(
            id,
            &status,
            true,
            false,
            false,
            true,
            true);
        return;
    }

    /*
     * Integrate-test override of the button-board RGB status LED. Service
     * mode + no deployment + no fault latch. Watchdog-bounded — firmware
     * reverts to its computed state after the hold window expires.
     * Four-place sync target: types.ts, mock-transport.ts, protocol-spec.md.
     */
    if (strcmp(command, "integrate.rgb_led.set") == 0) {
        if (!laser_controller_comms_service_mode_active(&status)) {
            laser_controller_comms_emit_error_response(
                id,
                "Enter service mode before driving the RGB LED test.");
            return;
        }
        if (status.deployment.active) {
            laser_controller_comms_emit_error_response(
                id,
                "Exit deployment mode before driving the RGB LED test.");
            return;
        }
        if (status.fault_latched) {
            laser_controller_comms_emit_error_response(
                id,
                "Clear the latched fault before driving the RGB LED test.");
            return;
        }
        uint32_t r = 0U;
        uint32_t g = 0U;
        uint32_t b = 0U;
        bool blink = false;
        uint32_t hold_ms = 5000U;
        if (!laser_controller_comms_extract_uint(line, "\"r\":", &r) ||
            !laser_controller_comms_extract_uint(line, "\"g\":", &g) ||
            !laser_controller_comms_extract_uint(line, "\"b\":", &b)) {
            laser_controller_comms_emit_error_response(
                id,
                "Missing r/g/b RGB arguments (0..255).");
            return;
        }
        (void)laser_controller_comms_extract_bool(line, "\"blink\":", &blink);
        (void)laser_controller_comms_extract_uint(line, "\"hold_ms\":", &hold_ms);
        if (r > 255U || g > 255U || b > 255U) {
            laser_controller_comms_emit_error_response(
                id,
                "RGB component out of range (0..255).");
            return;
        }
        if (hold_ms == 0U) {
            hold_ms = 5000U;
        } else if (hold_ms > 30000U) {
            hold_ms = 30000U;
        }
        const esp_err_t err = laser_controller_app_set_rgb_test(
            (uint8_t)r, (uint8_t)g, (uint8_t)b, blink, hold_ms);
        if (err != ESP_OK) {
            laser_controller_comms_emit_error_response(
                id,
                "RGB LED test rejected (guard re-check failed).");
            return;
        }
        laser_controller_comms_refresh_status_after_mutation(&status, 25U);
        laser_controller_comms_emit_io_status_response(
            id,
            &status,
            true,
            false,
            false,
            true,
            true);
        return;
    }

    if (strcmp(command, "integrate.rgb_led.clear") == 0) {
        const esp_err_t err = laser_controller_app_clear_rgb_test();
        if (err != ESP_OK) {
            laser_controller_comms_emit_error_response(
                id,
                "RGB LED test clear rejected.");
            return;
        }
        laser_controller_comms_refresh_status_after_mutation(&status, 25U);
        laser_controller_comms_emit_io_status_response(
            id,
            &status,
            true,
            false,
            false,
            true,
            true);
        return;
    }

    /*
     * ToF VL53L1X calibration + ROI. Service-mode-only, no deployment,
     * no fault latched. All fields are optional; unspecified fields
     * retain their current value. Writes persist to the dedicated
     * "tof_cal" NVS blob AND are applied to the VL53L1X immediately.
     * Fields:
     *   distance_mode : "short" | "medium" | "long"
     *   timing_budget_ms : 20 | 33 | 50 | 100 | 200
     *   roi_width_spads, roi_height_spads : 4..16
     *   roi_center_spad : 0..255 (default 199)
     *   offset_mm : signed distance offset correction
     *   xtalk_cps : unsigned crosstalk compensation counts/sec
     *   xtalk_enabled : bool — apply/ignore xtalk
     *
     * See docs/protocol-spec.md "ToF calibration" section.
     */
    if (strcmp(command, "integrate.tof.set_calibration") == 0) {
        if (!laser_controller_comms_service_mode_active(&status)) {
            laser_controller_comms_emit_error_response(
                id,
                "Enter service mode before updating ToF calibration.");
            return;
        }
        if (status.deployment.active) {
            laser_controller_comms_emit_error_response(
                id,
                "Exit deployment mode before updating ToF calibration.");
            return;
        }
        /*
         * fault_latched gate removed 2026-04-16: ToF calibration is
         * config-only (writes NVS + in-memory cal). No hardware
         * actuation. Refusing under fault left operators unable to
         * adjust ToF cal in service mode after a transient fault.
         */

        laser_controller_tof_calibration_t cal;
        laser_controller_service_get_tof_calibration(&cal);

        if (laser_controller_comms_extract_string(
                line,
                "\"distance_mode\":\"",
                text_arg,
                sizeof(text_arg))) {
            if (strcmp(text_arg, "short") == 0) {
                cal.distance_mode = LASER_CONTROLLER_TOF_DISTANCE_MODE_SHORT;
            } else if (strcmp(text_arg, "medium") == 0) {
                cal.distance_mode = LASER_CONTROLLER_TOF_DISTANCE_MODE_MEDIUM;
            } else if (strcmp(text_arg, "long") == 0) {
                cal.distance_mode = LASER_CONTROLLER_TOF_DISTANCE_MODE_LONG;
            } else {
                laser_controller_comms_emit_error_response(
                    id,
                    "Unsupported distance_mode. Use short | medium | long.");
                return;
            }
        }

        uint32_t u_arg = 0U;
        if (laser_controller_comms_extract_uint(
                line, "\"timing_budget_ms\":", &u_arg)) {
            cal.timing_budget_ms = u_arg;
        }
        if (laser_controller_comms_extract_uint(
                line, "\"roi_width_spads\":", &u_arg)) {
            cal.roi_width_spads = (uint8_t)u_arg;
        }
        if (laser_controller_comms_extract_uint(
                line, "\"roi_height_spads\":", &u_arg)) {
            cal.roi_height_spads = (uint8_t)u_arg;
        }
        if (laser_controller_comms_extract_uint(
                line, "\"roi_center_spad\":", &u_arg)) {
            cal.roi_center_spad = (uint8_t)u_arg;
        }
        /*
         * offset_mm is signed; comms.c only has extract_uint / extract_float,
         * so read as float and cast. Range is clamped in board.c.
         */
        float offset_f = (float)cal.offset_mm;
        if (laser_controller_comms_extract_float(
                line, "\"offset_mm\":", &offset_f)) {
            if (offset_f > 2000.0f) offset_f = 2000.0f;
            if (offset_f < -2000.0f) offset_f = -2000.0f;
            cal.offset_mm = (int32_t)offset_f;
        }
        if (laser_controller_comms_extract_uint(
                line, "\"xtalk_cps\":", &u_arg)) {
            cal.xtalk_cps = u_arg;
        }
        bool xtalk_enabled = cal.xtalk_enabled;
        if (laser_controller_comms_extract_bool(
                line, "\"xtalk_enabled\":", &xtalk_enabled)) {
            cal.xtalk_enabled = xtalk_enabled;
        }

        const esp_err_t persist_err = laser_controller_service_set_tof_calibration(
            &cal, now_ms);
        if (persist_err != ESP_OK) {
            laser_controller_comms_emit_error_response(
                id,
                "ToF calibration values failed validation (range / mode).");
            return;
        }

        const esp_err_t apply_err =
            laser_controller_board_tof_apply_calibration(&cal);
        if (apply_err != ESP_OK) {
            laser_controller_logger_logf(
                now_ms,
                "tof",
                "calibration persisted but hardware apply failed: %s",
                esp_err_to_name(apply_err));
            /*
             * Persistence succeeded — firmware will reapply on next ToF
             * init. Return a soft success with an informative note so
             * the operator knows to re-probe.
             */
        }

        laser_controller_logger_log(
            now_ms,
            "tof",
            "calibration updated (persisted to tof_cal NVS blob).");
        laser_controller_comms_refresh_status_after_mutation(&status, 25U);
        laser_controller_comms_emit_io_status_response(
            id,
            &status,
            true,
            false,
            false,
            true,
            true);
        return;
    }

    if (strcmp(command, "get_bringup_profile") == 0) {
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_full_status_response(id, &status);
        return;
    }

    if (strcmp(command, "refresh_pd_status") == 0) {
        if (status.deployment.active) {
            laser_controller_comms_emit_error_response(
                id,
                "PD refresh is locked while deployment mode is active. Only boot-time firmware PDO reconcile or explicit integrate-page PD actions may touch the STUSB4500.");
            return;
        }
        laser_controller_board_force_pd_refresh_with_source(
            LASER_CONTROLLER_PD_SNAPSHOT_SOURCE_INTEGRATE_REFRESH);
        vTaskDelay(pdMS_TO_TICKS(LASER_CONTROLLER_PD_REFRESH_WAIT_MS));
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_tool_status_response(
            id,
            &status,
            LASER_CONTROLLER_COMMS_PERIPHERAL_PD,
            true,
            true,
            false);
        return;
    }

    if (strcmp(command, "scan_wireless_networks") == 0) {
        const esp_err_t wireless_err =
            laser_controller_wireless_scan_networks();
        if (wireless_err != ESP_OK) {
            laser_controller_comms_emit_error_response(
                id,
                "Controller Wi-Fi scan failed before the network list could be refreshed.");
            return;
        }

        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_full_status_response(id, &status);
        return;
    }

    if (strcmp(command, "configure_wireless") == 0) {
        char mode_name[16] = { 0 };
        char station_ssid[33] = { 0 };
        char station_password[65] = { 0 };
        laser_controller_wireless_mode_t mode =
            LASER_CONTROLLER_WIRELESS_MODE_SOFTAP;
        const bool has_station_ssid = laser_controller_comms_extract_string(
            line,
            "\"ssid\":\"",
            station_ssid,
            sizeof(station_ssid));
        const bool has_station_password = laser_controller_comms_extract_string(
            line,
            "\"password\":\"",
            station_password,
            sizeof(station_password));
        esp_err_t wireless_err = ESP_OK;

        if (!laser_controller_comms_extract_string(
                line,
                "\"mode\":\"",
                mode_name,
                sizeof(mode_name))) {
            laser_controller_comms_emit_error_response(id, "Missing wireless mode.");
            return;
        }

        if (strcmp(mode_name, "softap") == 0 ||
            strcmp(mode_name, "ap") == 0) {
            mode = LASER_CONTROLLER_WIRELESS_MODE_SOFTAP;
        } else if (strcmp(mode_name, "station") == 0) {
            mode = LASER_CONTROLLER_WIRELESS_MODE_STATION;
        } else {
            laser_controller_comms_emit_error_response(
                id,
                "Wireless mode must be softap or station.");
            return;
        }

        wireless_err = laser_controller_wireless_configure(
            mode,
            has_station_ssid ? station_ssid : NULL,
            has_station_password ? station_password : NULL);
        if (wireless_err != ESP_OK) {
            laser_controller_comms_emit_error_response(
                id,
                wireless_err == ESP_ERR_INVALID_ARG ?
                    "Station mode needs a saved or provided SSID before the controller can join existing Wi-Fi." :
                    "Wireless reconfiguration failed before the controller could apply the requested network mode.");
            return;
        }

        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_status_response(id, &status);
        return;
    }

    if (status.deployment.active &&
        laser_controller_comms_is_service_mutation_command(command)) {
        laser_controller_comms_emit_error_response(
            id,
            "Deployment mode is active. Bring-up, GPIO, and service writes are locked until deployment mode is exited.");
        return;
    }

    if (!laser_controller_comms_service_mode_active(&status) &&
        laser_controller_comms_is_service_mutation_command(command)) {
        laser_controller_comms_emit_error_response(
            id,
            "Enter service mode before issuing mutating bring-up or bench commands.");
        return;
    }

    /*
     * Green alignment commands are intentionally NOT gated here. Per user
     * directive, the green laser has no software interlock; only hardware
     * rail availability gates it.
     *
     * LED (GPIO6) still requires deployment active + not-running because
     * the service-vs-runtime ownership story needs deployment to be the
     * stable context where runtime owns the sideband.
     */
    if (laser_controller_comms_is_led_control_command(command) &&
        !status.deployment.active) {
        laser_controller_comms_emit_error_response(
            id,
            "Enter deployment mode before changing the GPIO6 LED request.");
        return;
    }

    if (laser_controller_comms_is_led_control_command(command) &&
        status.deployment.running) {
        laser_controller_comms_emit_error_response(
            id,
            "Wait for the deployment checklist to stop before changing the GPIO6 LED request.");
        return;
    }

    if (laser_controller_comms_is_runtime_control_command(command) &&
        (!status.deployment.active || !status.deployment.ready)) {
        laser_controller_comms_emit_error_response(
            id,
            "Complete the deployment checklist successfully before using runtime control commands.");
        return;
    }

    /*
     * 2026-04-20: target-staging commands are allowed pre-deployment so
     * the host can stage the operator's wavelength before arming. Still
     * rejected while the checklist is actively running to protect
     * TEC_SETTLE from a mid-tick target change.
     */
    if (laser_controller_comms_is_target_stage_command(command) &&
        status.deployment.running) {
        laser_controller_comms_emit_error_response(
            id,
            "Target staging is blocked while the deployment checklist is running.");
        return;
    }

    if (strcmp(command, "set_runtime_mode") == 0 ||
        strcmp(command, "operate.set_mode") == 0) {
        laser_controller_runtime_mode_t runtime_mode =
            status.bench.runtime_mode;
        const char *mode_error =
            laser_controller_comms_runtime_mode_switch_block_reason(&status);

        if (!laser_controller_comms_extract_string(
                line,
                "\"mode\":\"",
                text_arg,
                sizeof(text_arg))) {
            laser_controller_comms_emit_error_response(
                id,
                "Missing runtime mode.");
            return;
        }

        if (strcmp(text_arg, "binary_trigger") == 0) {
            /*
             * Binary-trigger mode requires the button board MCP23017 to
             * be reachable on the shared I2C bus — otherwise the trigger
             * input is unobservable and the safety decision cannot read
             * stage state. AGENT.md previously blocked this mode entirely;
             * as of 2026-04-15 the gate is the live MCP23017 reachability
             * status from the board layer.
             */
            if (!status.inputs.button.board_reachable) {
                laser_controller_comms_emit_error_response(
                    id,
                    "Button board (MCP23017 @ 0x20) is not reachable. Confirm the J2 connector and re-check before selecting binary_trigger.");
                return;
            }
            runtime_mode = LASER_CONTROLLER_RUNTIME_MODE_BINARY_TRIGGER;
        } else if (strcmp(text_arg, "modulated_host") == 0) {
            runtime_mode = LASER_CONTROLLER_RUNTIME_MODE_MODULATED_HOST;
        } else {
            laser_controller_comms_emit_error_response(
                id,
                "Unsupported runtime mode.");
            return;
        }

        if (runtime_mode != status.bench.runtime_mode &&
            mode_error != NULL) {
            laser_controller_comms_emit_error_response(id, mode_error);
            return;
        }

        laser_controller_bench_set_runtime_mode(runtime_mode, now_ms);
        laser_controller_logger_logf(
            now_ms,
            "bench",
            "runtime mode -> %s",
            laser_controller_runtime_mode_name(runtime_mode));
        laser_controller_comms_refresh_status_after_mutation(&status, 25U);
        laser_controller_comms_emit_bench_status_response(
            id,
            &status,
            true,
            true,
            true);
        return;
    }

    /*
     * 2026-04-16: only the legacy direct-enable commands remain mode-gated.
     * The value setters (`operate.set_output`, `operate.set_modulation`,
     * `set_laser_power`, `set_max_current`) just stage stored values that
     * the buttons (binary_trigger mode) and the host (modulated_host mode)
     * both use. The actual hardware drive is gated downstream:
     *   - `host_request_nir` in safety.c requires runtime_mode == MODULATED_HOST
     *     before bench.requested_nir can fire NIR.
     *   - Buttons fire NIR via the binary-trigger path only when buttons are
     *     effective (deployment_ready_idle && board_reachable).
     * Allowing stored-value commits in any mode lets the operator pre-stage
     * the current setpoint that the buttons will fire at, without flipping
     * to host mode just to move a slider.
     */
    if ((strcmp(command, "laser_output_enable") == 0 ||
         strcmp(command, "laser_output_disable") == 0 ||
         strcmp(command, "configure_modulation") == 0) &&
        !laser_controller_comms_runtime_mode_is_modulated_host(&status)) {
        laser_controller_comms_emit_error_response(
            id,
            "Host runtime output control is only available in modulated_host mode.");
        return;
    }

    if (strcmp(command, "set_laser_power") == 0 ||
        strcmp(command, "set_max_current") == 0) {
        float current_a = 0.0f;

        if (!laser_controller_comms_extract_float(line, "\"current_a\":", &current_a)) {
            laser_controller_comms_emit_error_response(id, "Missing current_a.");
            return;
        }

        laser_controller_bench_set_laser_current_a(&status.config, current_a, now_ms);
        laser_controller_logger_logf(
            now_ms,
            "bench",
            "bench laser current staged -> %.3f A",
            current_a);
        laser_controller_comms_refresh_status_after_mutation(&status, 25U);
        laser_controller_comms_emit_bench_status_response(
            id,
            &status,
            false,
            true,
            true);
        return;
    }

    if (strcmp(command, "operate.set_alignment") == 0) {
        bool enabled = false;

        if (!laser_controller_comms_extract_bool(line, "\"enabled\":", &enabled)) {
            laser_controller_comms_emit_error_response(id, "Missing enabled.");
            return;
        }

        laser_controller_bench_set_alignment_requested(enabled, now_ms);
        laser_controller_logger_logf(
            now_ms,
            "bench",
            "operate alignment -> enabled=%d",
            enabled);
        laser_controller_comms_refresh_status_after_mutation(&status, 25U);
        laser_controller_comms_emit_bench_status_response(
            id,
            &status,
            false,
            false,
            true);
        return;
    }

    if (strcmp(command, "operate.set_led") == 0) {
        bool enabled = false;
        uint32_t duty_cycle_pct = 0U;
        uint32_t frequency_hz = 20000U;

        (void)laser_controller_comms_extract_bool(line, "\"enabled\":", &enabled);
        (void)laser_controller_comms_extract_uint(
            line,
            "\"duty_cycle_pct\":",
            &duty_cycle_pct);
        (void)laser_controller_comms_extract_uint(
            line,
            "\"frequency_hz\":",
            &frequency_hz);

        /*
         * 2026-04-17 (audit round 2, S2): block LED enable while the
         * deployment checklist is running. The checklist forces the
         * front LED off on entry (app.c enter_deployment_mode), but
         * without this gate any GUI slider drag during the
         * rail-sequence steps turns the LED back on for 180 ms until
         * the next control-task tick overrides it back — giving the
         * operator visible but transient feedback that the front LED
         * is respecting their click, when in fact deployment owns it.
         * Reject cleanly during checklist; allow during ready_idle
         * and during idle (non-deployment) states.
         */
        if (status.deployment.active && status.deployment.running) {
            laser_controller_comms_emit_error_response(
                id,
                "LED control is blocked while the deployment checklist is running. It will re-arm once the checklist reaches the ready posture.");
            return;
        }

        laser_controller_bench_set_illumination(
            enabled,
            duty_cycle_pct,
            frequency_hz,
            now_ms);
        /*
         * Mirror the brightness into the deployment-armed LED source of
         * truth so the GUI slider and side buttons converge on a single
         * value (Bug 1 fix 2026-04-17). The bench storage above is kept
         * for its `enabled` toggle and for non-deployment ownership.
         */
        (void)laser_controller_app_set_button_led_brightness(duty_cycle_pct);
        laser_controller_logger_logf(
            now_ms,
            "bench",
            "operate led -> enabled=%d duty=%lu freq=%lu",
            enabled,
            (unsigned long)duty_cycle_pct,
            (unsigned long)frequency_hz);
        laser_controller_comms_refresh_status_after_mutation(&status, 25U);
        laser_controller_comms_emit_bench_status_response(
            id,
            &status,
            false,
            false,
            true);
        return;
    }

    if (strcmp(command, "operate.set_output") == 0) {
        bool enabled = status.bench.requested_nir;
        float current_a = status.bench.high_state_current_a;

        (void)laser_controller_comms_extract_bool(line, "\"enabled\":", &enabled);
        (void)laser_controller_comms_extract_float(line, "\"current_a\":", &current_a);

        laser_controller_bench_set_laser_current_a(&status.config, current_a, now_ms);
        laser_controller_bench_set_nir_requested(enabled, now_ms);
        laser_controller_logger_logf(
            now_ms,
            "bench",
            "operate output -> enabled=%d current=%.3f A",
            enabled,
            current_a);
        laser_controller_comms_refresh_status_after_mutation(&status, 25U);
        laser_controller_comms_emit_bench_status_response(
            id,
            &status,
            false,
            true,
            true);
        return;
    }

    /*
     * 2026-04-16 user feature: explicit "save current bench settings as
     * deployment defaults" — the operator clicks a button on the Operate
     * page and we persist the current NIR current + TEC temp/lambda to
     * NVS. On the next power-up these become the values the headless
     * 5-second auto-deploy will run with.
     *
     * Persists three things:
     *   - bench.high_state_current_a → NVS u32 (mA)
     *   - bench.target_temp_c / target_lambda_nm → service profile slot
     *     via service_set_runtime_target (already auto-saved on slider
     *     commit; we re-save here as a defensive flush)
     *   - service profile blob (force flush)
     */
    if (strcmp(command, "operate.save_deployment_defaults") == 0) {
        /*
         * 2026-04-17 audit fix: refuse while the checklist is running.
         * Mid-checklist the bench fields may reflect values the
         * operator is in the middle of adjusting, not a deliberate
         * deployment default. The operator can save defaults either
         * before running the checklist (from ready_idle of the prior
         * cycle) or after it passes.
         */
        if (status.deployment.active && status.deployment.running) {
            laser_controller_comms_emit_error_response(
                id,
                "Cannot save deployment defaults while the checklist is running.");
            return;
        }
        const esp_err_t cur_err = laser_controller_service_save_deployment_current_a(
            status.bench.high_state_current_a);
        if (cur_err != ESP_OK) {
            laser_controller_comms_emit_error_response(
                id,
                "Could not save NIR current to NVS — controller storage error.");
            return;
        }
        laser_controller_service_set_runtime_target(
            status.bench.target_mode,
            status.bench.target_temp_c,
            status.bench.target_lambda_nm,
            now_ms);
        laser_controller_service_save_profile(now_ms);
        laser_controller_logger_logf(
            now_ms,
            "service",
            "deployment defaults saved: current=%.3f A target=%s temp=%.2f C lambda=%.2f nm",
            status.bench.high_state_current_a,
            status.bench.target_mode == LASER_CONTROLLER_BENCH_TARGET_MODE_LAMBDA ?
                "lambda" : "temp",
            status.bench.target_temp_c,
            status.bench.target_lambda_nm);
        laser_controller_comms_refresh_status_after_mutation(&status, 25U);
        laser_controller_comms_emit_bench_status_response(
            id,
            &status,
            false,
            true,
            true);
        return;
    }

    if (strcmp(command, "pd_debug_config") == 0) {
        laser_controller_power_policy_t power_policy = status.config.power;
        laser_controller_service_pd_profile_t profiles[
            LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT] =
                { 0 };
        esp_err_t pd_err = ESP_OK;

        laser_controller_logger_log(
            now_ms,
            "service",
            "pd debug config command received");
        memcpy(
            profiles,
            status.bringup.pd_profiles,
            sizeof(profiles));
        laser_controller_comms_extract_pd_config(
            line,
            &power_policy,
            profiles,
            LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT,
            NULL);

        if (laser_controller_app_set_runtime_power_policy(&power_policy) != ESP_OK) {
            laser_controller_comms_emit_error_response(
                id,
                "USB-PD planning update rejected because one or more values are invalid.");
            return;
        }
        laser_controller_logger_log(
            now_ms,
            "service",
            "pd debug power policy accepted");

        /*
         * 2026-04-16 HARD RULE: open the operator-authorization window
         * immediately before the PD write. The board layer rejects
         * writes that arrive without an open window. 2 s is generous
         * — the I2C transaction completes in < 50 ms.
         */
        laser_controller_board_authorize_pd_write_window(2000U);
        pd_err = laser_controller_service_set_pd_config(
            &power_policy,
            profiles,
            LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT,
            now_ms);
        if (pd_err != ESP_OK) {
            laser_controller_comms_emit_error_response(
                id,
                pd_err == ESP_ERR_INVALID_ARG ?
                    "USB-PD planning update rejected because one or more values are invalid." :
                pd_err == ESP_ERR_TIMEOUT ?
                    "STUSB4500 PDO update timed out before register write or renegotiation completed." :
                    "STUSB4500 PDO update failed before verified readback and renegotiation completed.");
            return;
        }

        laser_controller_logger_logf(
            now_ms,
            "service",
            "pd debug config full>=%.1fW reduced=%.1f..%.1fW pdo1=%.1fV@%.1fA pdo2=%.1fV@%.1fA pdo3=%.1fV@%.1fA",
            power_policy.full_mode_min_w,
            power_policy.reduced_mode_min_w,
            power_policy.reduced_mode_max_w,
            profiles[0].voltage_v,
            profiles[0].current_a,
            profiles[1].voltage_v,
            profiles[1].current_a,
            profiles[2].voltage_v,
            profiles[2].current_a);
        laser_controller_board_force_pd_refresh_with_source(
            LASER_CONTROLLER_PD_SNAPSHOT_SOURCE_INTEGRATE_REFRESH);
        vTaskDelay(pdMS_TO_TICKS(LASER_CONTROLLER_PD_RENEGOTIATE_WAIT_MS));
        (void)laser_controller_app_copy_status(&status);
        if (!laser_controller_comms_pd_profiles_match(
                status.bringup.pd_profiles,
                status.inputs.pd_sink_profiles,
                status.inputs.pd_sink_profile_count)) {
            laser_controller_comms_emit_error_response(
                id,
                "STUSB4500 runtime PDO readback did not match the requested plan after renegotiation.");
            return;
        }
        laser_controller_comms_emit_status_response(id, &status);
        return;
    }

    if (strcmp(command, "pd_save_firmware_plan") == 0) {
        laser_controller_power_policy_t power_policy = status.config.power;
        laser_controller_service_pd_profile_t profiles[
            LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT] =
                { 0 };
        bool firmware_plan_enabled = true;
        esp_err_t pd_err = ESP_OK;

        memcpy(
            profiles,
            status.bringup.pd_profiles,
            sizeof(profiles));
        laser_controller_comms_extract_pd_config(
            line,
            &power_policy,
            profiles,
            LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT,
            &firmware_plan_enabled);

        if (laser_controller_app_set_runtime_power_policy(&power_policy) != ESP_OK) {
            laser_controller_comms_emit_error_response(
                id,
                "Firmware PDO plan rejected because one or more power thresholds are invalid.");
            return;
        }

        /*
         * 2026-04-16 HARD RULE: open authorization window before PD write.
         */
        laser_controller_board_authorize_pd_write_window(2000U);
        pd_err = laser_controller_service_set_pd_config(
            &power_policy,
            profiles,
            LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT,
            now_ms);
        if (pd_err != ESP_OK) {
            laser_controller_comms_emit_error_response(
                id,
                pd_err == ESP_ERR_INVALID_ARG ?
                    "Firmware PDO plan rejected because one or more PDO values are invalid." :
                pd_err == ESP_ERR_TIMEOUT ?
                    "Firmware PDO plan timed out before runtime PDO write or renegotiation completed." :
                    "Firmware PDO plan failed before verified runtime PDO readback completed.");
            return;
        }

        laser_controller_board_force_pd_refresh_with_source(
            LASER_CONTROLLER_PD_SNAPSHOT_SOURCE_INTEGRATE_REFRESH);
        vTaskDelay(pdMS_TO_TICKS(LASER_CONTROLLER_PD_RENEGOTIATE_WAIT_MS));
        (void)laser_controller_app_copy_status(&status);
        if (!laser_controller_comms_pd_profiles_match(
                status.bringup.pd_profiles,
                status.inputs.pd_sink_profiles,
                status.inputs.pd_sink_profile_count)) {
            laser_controller_comms_emit_error_response(
                id,
                "Firmware PDO plan was not saved because STUSB4500 readback did not match the requested runtime plan.");
            return;
        }

        laser_controller_service_set_pd_firmware_plan_enabled(
            firmware_plan_enabled,
            now_ms);
        laser_controller_service_save_profile(now_ms);
        laser_controller_logger_logf(
            now_ms,
            "service",
            firmware_plan_enabled ?
                "firmware PDO plan saved and auto-reconcile enabled" :
                "firmware PDO plan saved with auto-reconcile disabled");
        (void)laser_controller_app_copy_status(&status);
        if (!status.bringup.last_save_ok) {
            laser_controller_comms_emit_error_response(
                id,
                "Runtime PDOs verified, but the firmware PDO plan could not be saved to controller NVS.");
            return;
        }
        laser_controller_comms_emit_status_response(id, &status);
        return;
    }

    if (strcmp(command, "pd_burn_nvm") == 0) {
        laser_controller_power_policy_t power_policy = status.config.power;
        laser_controller_service_pd_profile_t profiles[
            LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT] = { 0 };
        esp_err_t pd_err = ESP_OK;

        memcpy(
            profiles,
            status.bringup.pd_profiles,
            sizeof(profiles));
        laser_controller_comms_extract_pd_config(
            line,
            &power_policy,
            profiles,
            LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT,
            NULL);

        laser_controller_logger_log(
            now_ms,
            "service",
            "pd NVM burn requested");

        if (laser_controller_app_set_runtime_power_policy(&power_policy) != ESP_OK) {
            laser_controller_comms_emit_error_response(
                id,
                "STUSB4500 NVM burn rejected because one or more power thresholds are invalid.");
            return;
        }

        /*
         * 2026-04-16 HARD RULE: NVM burn flow performs TWO PD writes
         * back-to-back (validate runtime PDOs via apply, then burn NVM).
         * Each write consumes the authorization window; arm fresh before
         * each call.
         */
        laser_controller_board_authorize_pd_write_window(2000U);
        pd_err = laser_controller_service_set_pd_config(
            &power_policy,
            profiles,
            LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT,
            now_ms);
        if (pd_err != ESP_OK) {
            laser_controller_comms_emit_error_response(
                id,
                pd_err == ESP_ERR_INVALID_ARG ?
                    "STUSB4500 NVM burn rejected because one or more PDO values are invalid." :
                pd_err == ESP_ERR_TIMEOUT ?
                    "STUSB4500 runtime PDO verification timed out before the NVM burn could begin." :
                    "STUSB4500 runtime PDO verification failed before the NVM burn could begin.");
            return;
        }

        laser_controller_board_force_pd_refresh_with_source(
            LASER_CONTROLLER_PD_SNAPSHOT_SOURCE_INTEGRATE_REFRESH);
        vTaskDelay(pdMS_TO_TICKS(LASER_CONTROLLER_PD_RENEGOTIATE_WAIT_MS));
        (void)laser_controller_app_copy_status(&status);
        if (!laser_controller_comms_pd_profiles_match(
                status.bringup.pd_profiles,
                status.inputs.pd_sink_profiles,
                status.inputs.pd_sink_profile_count)) {
            laser_controller_comms_emit_error_response(
                id,
                "STUSB4500 runtime PDO readback did not match the requested plan, so the NVM burn was aborted.");
            return;
        }

        /* HARD RULE: re-arm window for the NVM burn. NVM burn takes
         * longer than runtime PDO write — give it 15 s headroom. */
        laser_controller_board_authorize_pd_write_window(15000U);
        pd_err = laser_controller_service_burn_pd_nvm(
            profiles,
            LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT,
            now_ms);
        if (pd_err != ESP_OK) {
            laser_controller_comms_emit_error_response(
                id,
                pd_err == ESP_ERR_INVALID_ARG ?
                    "STUSB4500 NVM burn rejected because one or more PDO values cannot be encoded into the NVM map." :
                pd_err == ESP_ERR_TIMEOUT ?
                    "STUSB4500 NVM burn timed out before raw NVM readback verification completed." :
                pd_err == ESP_ERR_INVALID_RESPONSE ?
                    "STUSB4500 NVM write finished, but raw NVM readback did not match the requested PDO bytes." :
                    "STUSB4500 NVM burn failed before raw NVM readback verification completed.");
            return;
        }

        laser_controller_logger_log(
            now_ms,
            "service",
            "pd NVM burn verified against raw NVM readback");
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_status_response(id, &status);
        return;
    }

    if (strcmp(command, "set_runtime_safety") == 0 ||
        strcmp(command, "set_deployment_safety") == 0 ||
        strcmp(command, "integrate.set_safety") == 0) {
        laser_controller_runtime_safety_policy_t policy = {
            .thresholds = status.config.thresholds,
            .timeouts = status.config.timeouts,
        };
        float value_f = 0.0f;
        uint32_t value_u = 0U;

        if (strcmp(command, "set_deployment_safety") == 0 &&
            status.deployment.running) {
            laser_controller_comms_emit_error_response(
                id,
                "Deployment safety edits are blocked while the deployment checklist is running.");
            return;
        }

        /*
         * 2026-04-17 (audit round 2, S1 safety): block any safety-policy
         * edit while the beam is ACTIVELY emitting. The thresholds
         * consumed by safety_evaluate (max_laser_current_a,
         * tec_temp_adc_trip_v, ld_overtemp_limit_c, tof_min_range_m,
         * lambda_drift_limit_nm) are sampled fresh every control-task
         * tick, so widening them mid-emission would retroactively
         * un-trip an active safety guard. Block on nir-or-alignment
         * output-enable from the latest safety decision so the
         * operator always closes the beam path before mutating
         * policy. This is the literal definition of the
         * "safety policy cannot widen while live" rule.
         */
        if (status.decision.nir_output_enable ||
            status.decision.alignment_output_enable) {
            laser_controller_comms_emit_error_response(
                id,
                "Runtime safety edits are blocked while the laser or alignment output is active. Turn the output off first.");
            return;
        }

        if (laser_controller_comms_extract_float(line, "\"horizon_threshold_deg\":", &value_f)) {
            policy.thresholds.horizon_threshold_rad = value_f * LASER_CONTROLLER_RAD_PER_DEG;
        }
        if (laser_controller_comms_extract_float(line, "\"horizon_hysteresis_deg\":", &value_f)) {
            policy.thresholds.horizon_hysteresis_rad = value_f * LASER_CONTROLLER_RAD_PER_DEG;
        }
        if (laser_controller_comms_extract_float(line, "\"tof_min_range_m\":", &value_f)) {
            policy.thresholds.tof_min_range_m = value_f;
        }
        if (laser_controller_comms_extract_float(line, "\"tof_max_range_m\":", &value_f)) {
            policy.thresholds.tof_max_range_m = value_f;
        }
        if (laser_controller_comms_extract_float(line, "\"tof_hysteresis_m\":", &value_f)) {
            policy.thresholds.tof_hysteresis_m = value_f;
        }
        if (laser_controller_comms_extract_uint(line, "\"imu_stale_ms\":", &value_u)) {
            policy.timeouts.imu_stale_ms = value_u;
        }
        if (laser_controller_comms_extract_uint(line, "\"tof_stale_ms\":", &value_u)) {
            policy.timeouts.tof_stale_ms = value_u;
        }
        if (laser_controller_comms_extract_uint(line, "\"rail_good_timeout_ms\":", &value_u)) {
            policy.timeouts.rail_good_timeout_ms = value_u;
        }
        if (laser_controller_comms_extract_float(line, "\"lambda_drift_limit_nm\":", &value_f)) {
            policy.thresholds.lambda_drift_limit_nm = value_f;
        }
        if (laser_controller_comms_extract_float(line, "\"lambda_drift_hysteresis_nm\":", &value_f)) {
            policy.thresholds.lambda_drift_hysteresis_nm = value_f;
        }
        if (laser_controller_comms_extract_uint(line, "\"lambda_drift_hold_ms\":", &value_u)) {
            policy.timeouts.lambda_drift_hold_ms = value_u;
        }
        if (laser_controller_comms_extract_float(line, "\"ld_overtemp_limit_c\":", &value_f)) {
            policy.thresholds.ld_overtemp_limit_c = value_f;
        }
        if (laser_controller_comms_extract_float(line, "\"tec_temp_adc_trip_v\":", &value_f)) {
            policy.thresholds.tec_temp_adc_trip_v = value_f;
        }
        if (laser_controller_comms_extract_float(line, "\"tec_temp_adc_hysteresis_v\":", &value_f)) {
            policy.thresholds.tec_temp_adc_hysteresis_v = value_f;
        }
        if (laser_controller_comms_extract_uint(line, "\"tec_temp_adc_hold_ms\":", &value_u)) {
            policy.timeouts.tec_temp_adc_hold_ms = value_u;
        }
        if (laser_controller_comms_extract_float(line, "\"tec_min_command_c\":", &value_f)) {
            policy.thresholds.tec_min_command_c = value_f;
        }
        if (laser_controller_comms_extract_float(line, "\"tec_max_command_c\":", &value_f)) {
            policy.thresholds.tec_max_command_c = value_f;
        }
        if (laser_controller_comms_extract_float(line, "\"tec_ready_tolerance_c\":", &value_f)) {
            policy.thresholds.tec_ready_tolerance_c = value_f;
        }
        if (laser_controller_comms_extract_float(line, "\"max_laser_current_a\":", &value_f)) {
            policy.thresholds.max_laser_current_a = value_f;
        }
        if (laser_controller_comms_extract_float(line, "\"off_current_threshold_a\":", &value_f)) {
            policy.thresholds.off_current_threshold_a = value_f;
        }
        {
            uint32_t max_led_duty = 0U;
            if (laser_controller_comms_extract_uint(
                    line, "\"max_tof_led_duty_cycle_pct\":", &max_led_duty)) {
                if (max_led_duty > 100U) {
                    max_led_duty = 100U;
                }
                policy.thresholds.max_tof_led_duty_cycle_pct = max_led_duty;
            }
        }
        if (laser_controller_comms_extract_float(
                line, "\"lio_voltage_offset_v\":", &value_f)) {
            policy.thresholds.lio_voltage_offset_v = value_f;
        }

        /*
         * Auto-deploy-on-boot service flag (2026-04-20 user directive).
         * Stored in `config.service_flags` bitmap so the NVS blob stays
         * layout-stable. Absent field leaves current setting; an explicit
         * false clears the bit.
         */
        {
            bool value_b = false;
            if (laser_controller_comms_extract_bool(
                    line, "\"auto_deploy_on_boot\":", &value_b)) {
                /*
                 * `policy` carries a snapshot of thresholds+timeouts only;
                 * service_flags is applied via a separate setter path after
                 * the main apply succeeds.
                 */
                if (value_b) {
                    status.config.service_flags |=
                        LASER_CONTROLLER_SERVICE_FLAG_AUTO_DEPLOY_ON_BOOT;
                } else {
                    status.config.service_flags &=
                        ~LASER_CONTROLLER_SERVICE_FLAG_AUTO_DEPLOY_ON_BOOT;
                }
                laser_controller_app_set_service_flags(status.config.service_flags);
            }
        }

        /*
         * Per-interlock enable mask + ToF low-bound-only (2026-04-17
         * user directive). Accepts either a JSON bool or an int. Fields
         * absent in the payload leave the existing policy unchanged so
         * partial updates are non-destructive.
         */
        {
            bool value_b = false;
            if (laser_controller_comms_extract_bool(
                    line, "\"interlock_horizon_enabled\":", &value_b)) {
                policy.thresholds.interlocks.horizon_enabled = value_b;
            }
            if (laser_controller_comms_extract_bool(
                    line, "\"interlock_distance_enabled\":", &value_b)) {
                policy.thresholds.interlocks.distance_enabled = value_b;
            }
            if (laser_controller_comms_extract_bool(
                    line, "\"interlock_lambda_drift_enabled\":", &value_b)) {
                policy.thresholds.interlocks.lambda_drift_enabled = value_b;
            }
            if (laser_controller_comms_extract_bool(
                    line, "\"interlock_tec_temp_adc_enabled\":", &value_b)) {
                policy.thresholds.interlocks.tec_temp_adc_enabled = value_b;
            }
            if (laser_controller_comms_extract_bool(
                    line, "\"interlock_imu_invalid_enabled\":", &value_b)) {
                policy.thresholds.interlocks.imu_invalid_enabled = value_b;
            }
            if (laser_controller_comms_extract_bool(
                    line, "\"interlock_imu_stale_enabled\":", &value_b)) {
                policy.thresholds.interlocks.imu_stale_enabled = value_b;
            }
            if (laser_controller_comms_extract_bool(
                    line, "\"interlock_tof_invalid_enabled\":", &value_b)) {
                policy.thresholds.interlocks.tof_invalid_enabled = value_b;
            }
            if (laser_controller_comms_extract_bool(
                    line, "\"interlock_tof_stale_enabled\":", &value_b)) {
                policy.thresholds.interlocks.tof_stale_enabled = value_b;
            }
            if (laser_controller_comms_extract_bool(
                    line, "\"interlock_ld_overtemp_enabled\":", &value_b)) {
                policy.thresholds.interlocks.ld_overtemp_enabled = value_b;
            }
            if (laser_controller_comms_extract_bool(
                    line, "\"interlock_ld_loop_bad_enabled\":", &value_b)) {
                policy.thresholds.interlocks.ld_loop_bad_enabled = value_b;
            }
            if (laser_controller_comms_extract_bool(
                    line, "\"tof_low_bound_only\":", &value_b)) {
                policy.thresholds.interlocks.tof_low_bound_only = value_b;
            }
        }

        if (laser_controller_app_set_runtime_safety_policy(&policy) != ESP_OK) {
            laser_controller_comms_emit_error_response(
                id,
                "Runtime safety update rejected because one or more values are invalid.");
            return;
        }

        laser_controller_logger_log(
            now_ms,
            strcmp(command, "set_deployment_safety") == 0 ? "deploy" : "config",
            strcmp(command, "set_deployment_safety") == 0 ?
                "deployment safety policy updated from host" :
                "runtime safety policy updated from host");
        if (strcmp(command, "integrate.set_safety") == 0) {
            laser_controller_service_save_profile(now_ms);
        }
        laser_controller_comms_refresh_status_after_mutation(&status, 25U);
        /*
         * 2026-04-17 (late, Apply-crash hotfix): unified all safety-set
         * responses on the smaller `emit_bench_status_response` so the
         * response frame stays well under the httpd SEND_WAIT_TIMEOUT
         * cap. Previously `integrate.set_safety` emitted the full
         * ~30 KB `emit_status_response` (includes gpioInspector.pins
         * and the bringup detail block), which on a Wi-Fi AP link with
         * lightly congested TCP queue would exceed the 1 s send timeout
         * right after the 1–2 s NVS flash commit, trigger
         * `httpd_sess_trigger_close` on the issuing socket, and surface
         * as "Wireless controller link dropped" mid-Apply. The host
         * reconciles safety state from the next periodic
         * `status_snapshot` (now interlocks-aware and free of the GPIO
         * inspector tail), so nothing is lost by switching to the
         * bench-sized response.
         */
        laser_controller_comms_emit_bench_status_response(
            id,
            &status,
            true,
            false,
            true);
        return;
    }

    if (strcmp(command, "set_target_temp") == 0 ||
        strcmp(command, "operate.set_target") == 0) {
        float target_temp_c = 0.0f;
        bool use_temp = strcmp(command, "set_target_temp") == 0;

        if (strcmp(command, "operate.set_target") == 0) {
            if (laser_controller_comms_extract_string(
                    line,
                    "\"target_mode\":\"",
                    text_arg,
                    sizeof(text_arg))) {
                use_temp = strcmp(text_arg, "lambda") != 0;
            }
        }

        if (!use_temp) {
            goto handle_operate_target_lambda;
        }

        if (!laser_controller_comms_extract_float(line, "\"temp_c\":", &target_temp_c)) {
            laser_controller_comms_emit_error_response(id, "Missing temp_c.");
            return;
        }

        laser_controller_bench_set_target_temp_c(&status.config, target_temp_c, now_ms);
        laser_controller_service_set_runtime_target(
            LASER_CONTROLLER_BENCH_TARGET_MODE_TEMP,
            target_temp_c,
            laser_controller_comms_lambda_from_temp(&status.config, target_temp_c),
            now_ms);
        if (status.deployment.active) {
            (void)laser_controller_app_set_deployment_target(
                &(laser_controller_deployment_target_t){
                    .target_mode = LASER_CONTROLLER_DEPLOYMENT_TARGET_MODE_TEMP,
                    .target_temp_c = target_temp_c,
                    .target_lambda_nm = status.deployment.target.target_lambda_nm,
                });
        }
        laser_controller_logger_logf(
            now_ms,
            "bench",
            "bench tec target staged -> %.2f C",
            target_temp_c);
        laser_controller_comms_refresh_status_after_mutation(&status, 25U);
        laser_controller_comms_emit_bench_status_response(
            id,
            &status,
            true,
            true,
            true);
        return;
    }

handle_operate_target_lambda:
    if (strcmp(command, "set_target_lambda") == 0 ||
        (strcmp(command, "operate.set_target") == 0 &&
         laser_controller_comms_find_value_start(line, "\"lambda_nm\":", false) != NULL) ||
        (strcmp(command, "operate.set_target") == 0 &&
         laser_controller_comms_find_value_start(line, "\"target_mode\":\"", true) != NULL &&
         strstr(line, "\"target_mode\":\"lambda\"") != NULL)) {
        float target_lambda_nm = 0.0f;
        laser_controller_bench_status_t bench_status_after = status.bench;

        if (!laser_controller_comms_extract_float(line, "\"lambda_nm\":", &target_lambda_nm)) {
            laser_controller_comms_emit_error_response(id, "Missing lambda_nm.");
            return;
        }

        laser_controller_bench_set_target_lambda_nm(&status.config, target_lambda_nm, now_ms);
        laser_controller_bench_copy_status(&bench_status_after);
        laser_controller_service_set_runtime_target(
            LASER_CONTROLLER_BENCH_TARGET_MODE_LAMBDA,
            bench_status_after.target_temp_c,
            bench_status_after.target_lambda_nm,
            now_ms);
        if (status.deployment.active) {
            (void)laser_controller_app_set_deployment_target(
                &(laser_controller_deployment_target_t){
                    .target_mode = LASER_CONTROLLER_DEPLOYMENT_TARGET_MODE_LAMBDA,
                    .target_temp_c = status.deployment.target.target_temp_c,
                    .target_lambda_nm = target_lambda_nm,
                });
        }
        laser_controller_logger_logf(
            now_ms,
            "bench",
            "bench wavelength target staged -> %.2f nm",
            target_lambda_nm);
        laser_controller_comms_refresh_status_after_mutation(&status, 25U);
        laser_controller_comms_emit_bench_status_response(
            id,
            &status,
            true,
            true,
            true);
        return;
    }

    if (strcmp(command, "enable_alignment") == 0) {
        laser_controller_bench_set_alignment_requested(true, now_ms);
        laser_controller_logger_log(now_ms, "bench", "alignment request staged");
        laser_controller_comms_refresh_status_after_mutation(&status, 25U);
        laser_controller_comms_emit_bench_status_response(
            id,
            &status,
            false,
            false,
            true);
        return;
    }

    if (strcmp(command, "disable_alignment") == 0) {
        laser_controller_bench_set_alignment_requested(false, now_ms);
        laser_controller_logger_log(now_ms, "bench", "alignment request cleared");
        laser_controller_comms_refresh_status_after_mutation(&status, 25U);
        laser_controller_comms_emit_bench_status_response(
            id,
            &status,
            false,
            false,
            true);
        return;
    }

    if (strcmp(command, "laser_output_enable") == 0) {
        laser_controller_bench_set_nir_requested(true, now_ms);
        laser_controller_logger_log(now_ms, "bench", "nir request staged");
        laser_controller_comms_refresh_status_after_mutation(&status, 25U);
        laser_controller_comms_emit_bench_status_response(
            id,
            &status,
            false,
            true,
            true);
        return;
    }

    if (strcmp(command, "laser_output_disable") == 0) {
        laser_controller_bench_set_nir_requested(false, now_ms);
        laser_controller_logger_log(now_ms, "bench", "nir request cleared");
        laser_controller_comms_refresh_status_after_mutation(&status, 25U);
        laser_controller_comms_emit_bench_status_response(
            id,
            &status,
            false,
            true,
            true);
        return;
    }

    if (strcmp(command, "configure_modulation") == 0 ||
        strcmp(command, "operate.set_modulation") == 0) {
        bool enabled = false;
        uint32_t frequency_hz = 0U;
        uint32_t duty_cycle_pct = 0U;

        if (!laser_controller_comms_extract_bool(line, "\"enabled\":", &enabled) ||
            !laser_controller_comms_extract_uint(line, "\"frequency_hz\":", &frequency_hz) ||
            !laser_controller_comms_extract_uint(line, "\"duty_cycle_pct\":", &duty_cycle_pct)) {
            laser_controller_comms_emit_error_response(id, "Missing modulation arguments.");
            return;
        }

        laser_controller_bench_set_modulation(
            enabled,
            frequency_hz,
            duty_cycle_pct,
            now_ms);
        laser_controller_logger_logf(
            now_ms,
            "bench",
            "pcn modulation staged enabled=%d freq=%lu duty=%lu",
            enabled,
            (unsigned long)frequency_hz,
            (unsigned long)duty_cycle_pct);
        laser_controller_comms_refresh_status_after_mutation(&status, 25U);
        laser_controller_comms_emit_bench_status_response(
            id,
            &status,
            false,
            true,
            true);
        return;
    }

    if (strcmp(command, "apply_bringup_preset") == 0) {
        if (!laser_controller_comms_extract_string(
                line,
                "\"preset\":\"",
                text_arg,
                sizeof(text_arg))) {
            laser_controller_comms_emit_error_response(id, "Missing preset.");
            return;
        }
        if (!laser_controller_service_apply_preset(text_arg, now_ms)) {
            laser_controller_comms_emit_error_response(id, "Unknown bring-up preset.");
            return;
        }
        laser_controller_logger_logf(now_ms, "service", "bring-up preset applied: %s", text_arg);
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_status_response(id, &status);
        return;
    }

    if (strcmp(command, "set_profile_name") == 0) {
        if (!laser_controller_comms_extract_string(
                line,
                "\"name\":\"",
                text_arg,
                sizeof(text_arg))) {
            laser_controller_comms_emit_error_response(id, "Missing profile name.");
            return;
        }
        if (!laser_controller_service_set_profile_name(text_arg, now_ms)) {
            laser_controller_comms_emit_error_response(id, "Invalid profile name.");
            return;
        }
        laser_controller_logger_logf(now_ms, "service", "bring-up profile renamed: %s", text_arg);
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_profile_status_response(id, &status);
        return;
    }

    if (strcmp(command, "set_module_state") == 0) {
        laser_controller_module_t module;
        bool expected_present;
        bool debug_enabled;

        if (!laser_controller_comms_extract_string(
                line,
                "\"module\":\"",
                text_arg,
                sizeof(text_arg)) ||
            !laser_controller_service_parse_module(text_arg, &module)) {
            laser_controller_comms_emit_error_response(id, "Unknown module.");
            return;
        }

        if (!laser_controller_comms_extract_bool(
                line,
                "\"expected_present\":",
                &expected_present) ||
            !laser_controller_comms_extract_bool(
                line,
                "\"debug_enabled\":",
                &debug_enabled)) {
            laser_controller_comms_emit_error_response(id, "Missing module state arguments.");
            return;
        }

        (void)laser_controller_service_set_module_state(
            module,
            expected_present,
            debug_enabled,
            now_ms);
        laser_controller_logger_logf(
            now_ms,
            "service",
            "module %s -> expected=%d debug=%d",
            laser_controller_service_module_name(module),
            expected_present,
            debug_enabled);
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_tool_status_response(
            id,
            &status,
            LASER_CONTROLLER_COMMS_PERIPHERAL_NONE,
            false,
            true,
            false);
        return;
    }

    if (strcmp(command, "set_supply_enable") == 0) {
        laser_controller_service_supply_t supply;
        bool enabled = false;

        if (!laser_controller_comms_extract_string(
                line,
                "\"rail\":\"",
                text_arg,
                sizeof(text_arg)) ||
            !laser_controller_service_parse_supply(text_arg, &supply)) {
            laser_controller_comms_emit_error_response(id, "Unknown supply rail.");
            return;
        }

        if (!laser_controller_comms_extract_bool(
                line,
                "\"enabled\":",
                &enabled)) {
            laser_controller_comms_emit_error_response(id, "Missing rail enable state.");
            return;
        }

        if (!laser_controller_service_set_supply_enable(supply, enabled, now_ms)) {
            laser_controller_comms_emit_error_response(id, "Supply rail request rejected.");
            return;
        }

        laser_controller_logger_logf(
            now_ms,
            "service",
            "service rail %s -> %s",
            laser_controller_service_supply_name(supply),
            enabled ? "enabled" : "disabled");
        (void)laser_controller_comms_wait_for_supply_state(
            supply,
            enabled,
            &status,
            &status);
        laser_controller_comms_emit_io_status_response(
            id,
            &status,
            true,
            false,
            false,
            true,
            false);
        return;
    }

    if (strcmp(command, "set_interlocks_disabled") == 0) {
        bool enabled = false;

        if (!laser_controller_comms_extract_bool(line, "\"enabled\":", &enabled)) {
            laser_controller_comms_emit_error_response(
                id,
                "Missing interlock override state.");
            return;
        }

        if (enabled && !laser_controller_comms_service_mode_active(&status)) {
            laser_controller_comms_emit_error_response(
                id,
                "Enter service mode before disabling controller interlocks.");
            return;
        }

        laser_controller_service_set_interlocks_disabled(enabled, now_ms);
        laser_controller_logger_logf(
            now_ms,
            "service",
            "bench interlock override -> %s",
            enabled ? "disabled" : "restored");
        vTaskDelay(pdMS_TO_TICKS(80U));
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_status_response(id, &status);
        return;
    }

    if (strcmp(command, "set_haptic_enable") == 0) {
        bool enabled = false;

        if (!laser_controller_comms_extract_bool(line, "\"enabled\":", &enabled)) {
            laser_controller_comms_emit_error_response(
                id,
                "Missing ERM enable state.");
            return;
        }

        laser_controller_service_set_haptic_driver_enable(enabled, now_ms);
        laser_controller_logger_logf(
            now_ms,
            "service",
            "ERM driver enable GPIO48 -> %s",
            enabled ? "high" : "low");
        (void)laser_controller_comms_wait_for_haptic_enable(enabled, &status);
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_io_status_response(
            id,
            &status,
            false,
            true,
            false,
            false,
            false);
        return;
    }

    if (strcmp(command, "set_gpio_override") == 0) {
        laser_controller_service_gpio_mode_t mode;
        uint32_t gpio_num = 0U;
        bool level_high = false;
        bool pullup_enabled = false;
        bool pulldown_enabled = false;

        if (!laser_controller_comms_extract_uint(line, "\"gpio\":", &gpio_num)) {
            laser_controller_comms_emit_error_response(id, "Missing GPIO number.");
            return;
        }

        if (!laser_controller_comms_extract_string(
                line,
                "\"mode\":\"",
                text_arg,
                sizeof(text_arg)) ||
            !laser_controller_service_parse_gpio_mode(text_arg, &mode)) {
            laser_controller_comms_emit_error_response(id, "Unknown GPIO override mode.");
            return;
        }

        (void)laser_controller_comms_extract_bool(
            line,
            "\"level_high\":",
            &level_high);
        (void)laser_controller_comms_extract_bool(
            line,
            "\"pullup_enabled\":",
            &pullup_enabled);
        (void)laser_controller_comms_extract_bool(
            line,
            "\"pulldown_enabled\":",
            &pulldown_enabled);

        if (!laser_controller_service_set_gpio_override(
                gpio_num,
                mode,
                level_high,
                pullup_enabled,
                pulldown_enabled,
                now_ms)) {
            laser_controller_comms_emit_error_response(id, "GPIO override rejected.");
            return;
        }

        laser_controller_logger_logf(
            now_ms,
            "service",
            "gpio %lu override -> %s",
            (unsigned long)gpio_num,
            laser_controller_service_gpio_mode_name(mode));
        (void)laser_controller_comms_wait_for_gpio_override(
            gpio_num,
            mode,
            level_high,
            pullup_enabled,
            pulldown_enabled,
            &status);
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_io_status_response(
            id,
            &status,
            false,
            false,
            true,
            false,
            false);
        return;
    }

    if (strcmp(command, "clear_gpio_overrides") == 0) {
        laser_controller_service_clear_gpio_overrides(now_ms);
        laser_controller_logger_log(
            now_ms,
            "service",
            "all gpio overrides cleared");
        (void)laser_controller_comms_wait_for_gpio_override_clear(&status);
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_io_status_response(
            id,
            &status,
            false,
            false,
            true,
            false,
            false);
        return;
    }

    if (strcmp(command, "save_bringup_profile") == 0 ||
        strcmp(command, "integrate.save_profile") == 0) {
        laser_controller_service_save_profile(now_ms);
        laser_controller_logger_log(
            now_ms,
            "service",
            "device-side bring-up profile save requested");
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_profile_status_response(id, &status);
        return;
    }

    if (strcmp(command, "dac_debug_set") == 0) {
        float voltage_v = 0.0f;
        bool tec_channel = false;
        esp_err_t err;

        if (!laser_controller_comms_extract_string(
                line,
                "\"channel\":\"",
                text_arg,
                sizeof(text_arg)) ||
            !laser_controller_comms_extract_float(
                line,
                "\"voltage_v\":",
                &voltage_v)) {
            laser_controller_comms_emit_error_response(id, "Missing DAC debug arguments.");
            return;
        }

        tec_channel = strcmp(text_arg, "tec") == 0;
        err = laser_controller_service_set_dac_shadow(tec_channel, voltage_v, now_ms);
        if (err != ESP_OK) {
            if (err == ESP_ERR_INVALID_STATE) {
                (void)laser_controller_app_copy_status(&status);
                laser_controller_comms_format_dac_block_reason(
                    text_arg,
                    sizeof(text_arg),
                    &status,
                    false);
            } else {
                snprintf(
                    text_arg,
                    sizeof(text_arg),
                    "DAC write failed: %s.",
                    esp_err_to_name(err));
            }
            laser_controller_comms_emit_error_response(id, text_arg);
            return;
        }
        laser_controller_logger_logf(
            now_ms,
            "service",
            "dac %s shadow -> %.3f V",
            tec_channel ? "tec" : "ld",
            voltage_v);
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_status_response(id, &status);
        return;
    }

    if (strcmp(command, "dac_debug_config") == 0) {
        laser_controller_service_dac_reference_t reference;
        laser_controller_service_dac_sync_t sync_mode;
        bool gain_2x = false;
        bool ref_div = false;
        esp_err_t err;

        if (!laser_controller_comms_extract_string(
                line,
                "\"reference_mode\":\"",
                text_arg,
                sizeof(text_arg)) ||
            !laser_controller_service_parse_dac_reference(text_arg, &reference) ||
            !laser_controller_comms_extract_bool(line, "\"gain_2x\":", &gain_2x) ||
            !laser_controller_comms_extract_bool(line, "\"ref_div\":", &ref_div) ||
            !laser_controller_comms_extract_string(
                line,
                "\"sync_mode\":\"",
                text_arg,
                sizeof(text_arg)) ||
            !laser_controller_service_parse_dac_sync_mode(text_arg, &sync_mode)) {
            laser_controller_comms_emit_error_response(id, "Missing DAC configuration arguments.");
            return;
        }

        err = laser_controller_service_set_dac_config(
            reference,
            gain_2x,
            ref_div,
            sync_mode,
            now_ms);
        if (err != ESP_OK) {
            if (err == ESP_ERR_INVALID_STATE) {
                (void)laser_controller_app_copy_status(&status);
                laser_controller_comms_format_dac_block_reason(
                    text_arg,
                    sizeof(text_arg),
                    &status,
                    true);
            } else if (err == ESP_ERR_INVALID_ARG) {
                (void)snprintf(
                    text_arg,
                    sizeof(text_arg),
                    "DAC rejected; internal ref needs REF-DIV=1.");
            } else {
                snprintf(
                    text_arg,
                    sizeof(text_arg),
                    "DAC config failed: %s.",
                    esp_err_to_name(err));
            }
            laser_controller_comms_emit_error_response(id, text_arg);
            return;
        }
        laser_controller_logger_logf(
            now_ms,
            "service",
            "dac debug config ref=%s gain2x=%d refdiv=%d sync=%s",
            laser_controller_service_dac_reference_name(reference),
            gain_2x,
            ref_div,
            laser_controller_service_dac_sync_mode_name(sync_mode));
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_status_response(id, &status);
        return;
    }

    if (strcmp(command, "imu_debug_config") == 0) {
        uint32_t odr_hz = 0U;
        uint32_t accel_range_g = 0U;
        uint32_t gyro_range_dps = 0U;
        bool gyro_enabled = false;
        bool lpf2_enabled = false;
        bool timestamp_enabled = false;
        bool bdu_enabled = false;
        bool if_inc_enabled = false;
        bool i2c_disabled = false;

        if (!laser_controller_comms_extract_uint(line, "\"odr_hz\":", &odr_hz) ||
            !laser_controller_comms_extract_uint(
                line,
                "\"accel_range_g\":",
                &accel_range_g) ||
            !laser_controller_comms_extract_uint(
                line,
                "\"gyro_range_dps\":",
                &gyro_range_dps) ||
            !laser_controller_comms_extract_bool(
                line,
                "\"gyro_enabled\":",
                &gyro_enabled) ||
            !laser_controller_comms_extract_bool(
                line,
                "\"lpf2_enabled\":",
                &lpf2_enabled) ||
            !laser_controller_comms_extract_bool(
                line,
                "\"timestamp_enabled\":",
                &timestamp_enabled) ||
            !laser_controller_comms_extract_bool(
                line,
                "\"bdu_enabled\":",
                &bdu_enabled) ||
            !laser_controller_comms_extract_bool(
                line,
                "\"if_inc_enabled\":",
                &if_inc_enabled) ||
            !laser_controller_comms_extract_bool(
                line,
                "\"i2c_disabled\":",
                &i2c_disabled)) {
            laser_controller_comms_emit_error_response(id, "Missing IMU debug arguments.");
            return;
        }

        laser_controller_service_set_imu_config(
            odr_hz,
            accel_range_g,
            gyro_range_dps,
            gyro_enabled,
            lpf2_enabled,
            timestamp_enabled,
            bdu_enabled,
            if_inc_enabled,
            i2c_disabled,
            now_ms);
        laser_controller_logger_logf(
            now_ms,
            "service",
            "imu debug config odr=%lu accel=%lu gyro=%lu enabled=%d lpf2=%d ts=%d",
            (unsigned long)odr_hz,
            (unsigned long)accel_range_g,
            (unsigned long)gyro_range_dps,
            gyro_enabled,
            lpf2_enabled,
            timestamp_enabled);
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_status_response(id, &status);
        return;
    }

    if (strcmp(command, "tof_debug_config") == 0) {
        float min_range_m = 0.0f;
        float max_range_m = 0.0f;
        uint32_t stale_timeout_ms = 0U;

        if (!laser_controller_comms_extract_float(
                line,
                "\"min_range_m\":",
                &min_range_m) ||
            !laser_controller_comms_extract_float(
                line,
                "\"max_range_m\":",
                &max_range_m) ||
            !laser_controller_comms_extract_uint(
                line,
                "\"stale_timeout_ms\":",
                &stale_timeout_ms)) {
            laser_controller_comms_emit_error_response(id, "Missing ToF debug arguments.");
            return;
        }

        laser_controller_service_set_tof_config(
            min_range_m,
            max_range_m,
            stale_timeout_ms,
            now_ms);
        laser_controller_logger_logf(
            now_ms,
            "service",
            "tof debug window %.3f-%.3f m timeout=%lu ms",
            min_range_m,
            max_range_m,
            (unsigned long)stale_timeout_ms);
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_status_response(id, &status);
        return;
    }

    if (strcmp(command, "tof_illumination_set") == 0) {
        bool enabled = false;
        uint32_t duty_cycle_pct = 0U;
        uint32_t frequency_hz = 0U;
        esp_err_t err;

        if (!laser_controller_comms_extract_bool(line, "\"enabled\":", &enabled) ||
            !laser_controller_comms_extract_uint(
                line,
                "\"duty_cycle_pct\":",
                &duty_cycle_pct) ||
            !laser_controller_comms_extract_uint(
                line,
                "\"frequency_hz\":",
                &frequency_hz)) {
            laser_controller_comms_emit_error_response(
                id,
                "Missing ToF illumination arguments.");
            return;
        }

        err = laser_controller_service_set_tof_illumination(
            enabled,
            duty_cycle_pct,
            frequency_hz,
            now_ms);
        if (err == ESP_ERR_INVALID_STATE) {
            laser_controller_comms_emit_error_response(
                id,
                "ToF illumination write gate is closed. Mark the ToF module expected or arm debug.");
            return;
        }
        if (err != ESP_OK) {
            laser_controller_comms_emit_error_response(
                id,
                "ToF illumination update failed.");
            return;
        }

        laser_controller_logger_logf(
            now_ms,
            "service",
            "tof illumination enabled=%d duty=%lu freq=%lu",
            enabled ? 1 : 0,
            (unsigned long)duty_cycle_pct,
            (unsigned long)frequency_hz);
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_io_status_response(
            id,
            &status,
            false,
            false,
            false,
            false,
            true);
        return;
    }

    if (strcmp(command, "haptic_debug_config") == 0) {
        uint32_t effect_id = 0U;
        uint32_t library = 0U;
        uint32_t rtp_level = 0U;
        laser_controller_service_haptic_mode_t mode;
        laser_controller_service_haptic_actuator_t actuator;

        if (!laser_controller_comms_extract_uint(line, "\"effect_id\":", &effect_id) ||
            !laser_controller_comms_extract_string(
                line,
                "\"mode\":\"",
                text_arg,
                sizeof(text_arg)) ||
            !laser_controller_service_parse_haptic_mode(text_arg, &mode) ||
            !laser_controller_comms_extract_uint(line, "\"library\":", &library) ||
            !laser_controller_comms_extract_string(
                line,
                "\"actuator\":\"",
                text_arg,
                sizeof(text_arg)) ||
            !laser_controller_service_parse_haptic_actuator(text_arg, &actuator) ||
            !laser_controller_comms_extract_uint(line, "\"rtp_level\":", &rtp_level)) {
            laser_controller_comms_emit_error_response(id, "Missing haptic effect id.");
            return;
        }

        laser_controller_service_set_haptic_config(
            effect_id,
            mode,
            library,
            actuator,
            rtp_level,
            now_ms);
        laser_controller_logger_logf(
            now_ms,
            "service",
            "haptic effect=%lu mode=%s library=%lu actuator=%s rtp=%lu",
            (unsigned long)effect_id,
            laser_controller_service_haptic_mode_name(mode),
            (unsigned long)library,
            laser_controller_service_haptic_actuator_name(actuator),
            (unsigned long)rtp_level);
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_status_response(id, &status);
        return;
    }

    if (strcmp(command, "haptic_debug_fire") == 0) {
        laser_controller_service_fire_haptic_test(now_ms);
        laser_controller_logger_log(now_ms, "service", "haptic test fired");
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_io_status_response(
            id,
            &status,
            false,
            true,
            false,
            false,
            false);
        return;
    }

    if (strcmp(command, "haptic_external_trigger_pattern") == 0) {
        uint32_t pulse_count = 0U;
        uint32_t high_ms = 0U;
        uint32_t low_ms = 0U;
        bool release_after = true;
        esp_err_t err;

        if (!laser_controller_comms_extract_uint(line, "\"pulse_count\":", &pulse_count) ||
            !laser_controller_comms_extract_uint(line, "\"high_ms\":", &high_ms) ||
            !laser_controller_comms_extract_uint(line, "\"low_ms\":", &low_ms)) {
            laser_controller_comms_emit_error_response(
                id,
                "Missing external ERM trigger pattern arguments.");
            return;
        }

        (void)laser_controller_comms_extract_bool(
            line,
            "\"release_after\":",
            &release_after);

        err = laser_controller_service_fire_haptic_trigger_pattern(
            pulse_count,
            high_ms,
            low_ms,
            release_after,
            now_ms);
        if (err != ESP_OK) {
            laser_controller_comms_emit_error_response(
                id,
                err == ESP_ERR_INVALID_ARG ?
                    "External ERM trigger burst rejected because pulse count or timing is out of range." :
                    "External ERM trigger burst requires the haptic module armed, ERM EN high on GPIO48, and DRV2605 external trigger mode.");
            return;
        }

        laser_controller_logger_logf(
            now_ms,
            "service",
            "hazardous IO37 external ERM burst pulses=%lu high=%lu low=%lu release=%d",
            (unsigned long)pulse_count,
            (unsigned long)high_ms,
            (unsigned long)low_ms,
            release_after ? 1 : 0);
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_io_status_response(
            id,
            &status,
            false,
            true,
            false,
            false,
            false);
        return;
    }

    if (strcmp(command, "reboot") == 0) {
        laser_controller_bench_clear_requests(now_ms);
        laser_controller_service_set_mode_requested(false, now_ms);
        laser_controller_logger_log(now_ms, "service", "controlled reboot requested");
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_status_response(id, &status);
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(20));
        esp_restart();
        return;
    }

    if (strcmp(command, "i2c_scan") == 0) {
        laser_controller_service_i2c_scan(now_ms);
        laser_controller_logger_log(now_ms, "service", "i2c scan requested");
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_tool_status_response(
            id,
            &status,
            LASER_CONTROLLER_COMMS_PERIPHERAL_NONE,
            false,
            true,
            true);
        return;
    }

    if (strcmp(command, "i2c_read") == 0) {
        uint32_t address = 0U;
        uint32_t reg = 0U;

        if (!laser_controller_comms_extract_uint(line, "\"address\":", &address) ||
            !laser_controller_comms_extract_uint(line, "\"reg\":", &reg)) {
            laser_controller_comms_emit_error_response(id, "Missing I2C read arguments.");
            return;
        }

        laser_controller_service_i2c_read(address, reg, now_ms);
        laser_controller_logger_logf(
            now_ms,
            "service",
            "i2c read 0x%02lX reg 0x%02lX",
            (unsigned long)address,
            (unsigned long)reg);
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_tool_status_response(
            id,
            &status,
            address == LASER_CONTROLLER_DAC80502_ADDR ?
                LASER_CONTROLLER_COMMS_PERIPHERAL_DAC :
            address == LASER_CONTROLLER_STUSB4500_ADDR ?
                LASER_CONTROLLER_COMMS_PERIPHERAL_PD :
            address == LASER_CONTROLLER_DRV2605_ADDR ?
                LASER_CONTROLLER_COMMS_PERIPHERAL_HAPTIC :
                LASER_CONTROLLER_COMMS_PERIPHERAL_NONE,
            false,
            true,
            true);
        return;
    }

    if (strcmp(command, "i2c_write") == 0) {
        uint32_t address = 0U;
        uint32_t reg = 0U;
        uint32_t value = 0U;

        if (!laser_controller_comms_extract_uint(line, "\"address\":", &address) ||
            !laser_controller_comms_extract_uint(line, "\"reg\":", &reg) ||
            !laser_controller_comms_extract_uint(line, "\"value\":", &value)) {
            laser_controller_comms_emit_error_response(id, "Missing I2C write arguments.");
            return;
        }

        laser_controller_service_i2c_write(address, reg, value, now_ms);
        laser_controller_logger_logf(
            now_ms,
            "service",
            "i2c write 0x%02lX reg 0x%02lX <- 0x%02lX",
            (unsigned long)address,
            (unsigned long)reg,
            (unsigned long)(value & 0xFFU));
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_tool_status_response(
            id,
            &status,
            address == LASER_CONTROLLER_DAC80502_ADDR ?
                LASER_CONTROLLER_COMMS_PERIPHERAL_DAC :
            address == LASER_CONTROLLER_STUSB4500_ADDR ?
                LASER_CONTROLLER_COMMS_PERIPHERAL_PD :
            address == LASER_CONTROLLER_DRV2605_ADDR ?
                LASER_CONTROLLER_COMMS_PERIPHERAL_HAPTIC :
                LASER_CONTROLLER_COMMS_PERIPHERAL_NONE,
            false,
            true,
            true);
        return;
    }

    if (strcmp(command, "spi_read") == 0) {
        uint32_t reg = 0U;

        if (!laser_controller_comms_extract_string(
                line,
                "\"device\":\"",
                text_arg,
                sizeof(text_arg)) ||
            !laser_controller_comms_extract_uint(line, "\"reg\":", &reg)) {
            laser_controller_comms_emit_error_response(id, "Missing SPI read arguments.");
            return;
        }

        laser_controller_service_spi_read(text_arg, reg, now_ms);
        laser_controller_logger_logf(
            now_ms,
            "service",
            "spi read %s reg 0x%02lX",
            text_arg,
            (unsigned long)reg);
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_tool_status_response(
            id,
            &status,
            LASER_CONTROLLER_COMMS_PERIPHERAL_IMU,
            false,
            true,
            true);
        return;
    }

    if (strcmp(command, "spi_write") == 0) {
        uint32_t reg = 0U;
        uint32_t value = 0U;

        if (!laser_controller_comms_extract_string(
                line,
                "\"device\":\"",
                text_arg,
                sizeof(text_arg)) ||
            !laser_controller_comms_extract_uint(line, "\"reg\":", &reg) ||
            !laser_controller_comms_extract_uint(line, "\"value\":", &value)) {
            laser_controller_comms_emit_error_response(id, "Missing SPI write arguments.");
            return;
        }

        laser_controller_service_spi_write(text_arg, reg, value, now_ms);
        laser_controller_logger_logf(
            now_ms,
            "service",
            "spi write %s reg 0x%02lX <- 0x%02lX",
            text_arg,
            (unsigned long)reg,
            (unsigned long)(value & 0xFFU));
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_tool_status_response(
            id,
            &status,
            LASER_CONTROLLER_COMMS_PERIPHERAL_IMU,
            false,
            true,
            true);
        return;
    }

    laser_controller_comms_emit_error_response(
        id,
        "Command not implemented in bring-up firmware.");
}

void laser_controller_comms_receive_line(const char *line)
{
    laser_controller_comms_line_t item = { 0 };
    char command[32] = { 0 };
    uint32_t id = 0U;
    bool is_background_probe = false;

    if (line == NULL || line[0] == '\0' || s_command_queue == NULL) {
        return;
    }

    /*
     * 2026-04-16 user feature: stamp host-activity at the *very* top so
     * the headless auto-deploy state machine in app.c run_fast_cycle is
     * suppressed the moment any inbound command arrives — including
     * background probes (get_status, ping). Suppression is sticky for
     * the whole boot session.
     */
    laser_controller_app_note_host_activity();

    if (laser_controller_comms_extract_root_uint(line, "\"id\":", &id) &&
        id == 0U &&
        (laser_controller_comms_extract_root_string(
             line,
             "\"cmd\":\"",
             command,
             sizeof(command)) ||
         laser_controller_comms_extract_root_string(
             line,
             "\"command\":\"",
             command,
             sizeof(command)))) {
        is_background_probe =
            strcmp(command, "ping") == 0 ||
            strcmp(command, "get_status") == 0;
    }

    if (is_background_probe &&
        (s_command_in_flight ||
         uxQueueMessagesWaiting(s_command_queue) != 0U)) {
        return;
    }

    (void)strlcpy(item.line, line, sizeof(item.line));

    if (xQueueSend(s_command_queue, &item, 0U) == pdPASS) {
        return;
    }

    if (is_background_probe) {
        return;
    }

    if (id != 0U || laser_controller_comms_extract_root_uint(line, "\"id\":", &id)) {
        laser_controller_comms_emit_error_response(
            id,
            "Controller command queue is full. Wait for the current action to finish and retry.");
    }
}

static void laser_controller_comms_command_task(void *argument)
{
    laser_controller_comms_line_t item = { 0 };

    (void)argument;

    for (;;) {
        if (xQueueReceive(s_command_queue, &item, portMAX_DELAY) == pdTRUE) {
            s_command_in_flight = true;
            laser_controller_comms_handle_command_line(item.line);
            s_command_in_flight = false;
            s_wireless_telemetry_quiet_until_ms =
                laser_controller_board_uptime_ms() +
                LASER_CONTROLLER_COMMS_WIRELESS_POST_COMMAND_QUIET_MS;
        }
    }
}

static void laser_controller_comms_tx_task(void *argument)
{
    size_t last_log_count = 0U;
    laser_controller_time_ms_t last_fast_telemetry_ms = 0U;
    laser_controller_time_ms_t last_live_telemetry_ms = 0U;
    laser_controller_time_ms_t last_status_ms = 0U;

    (void)argument;

    for (;;) {
        laser_controller_runtime_status_t status;
        const size_t total_log_count = laser_controller_logger_total_count();

        if (laser_controller_app_copy_status(&status)) {
            const laser_controller_time_ms_t now_ms = status.uptime_ms;
            const bool wireless_clients_active =
                laser_controller_wireless_has_clients();
            const bool pause_wireless_telemetry =
                laser_controller_comms_should_pause_wireless_telemetry(now_ms);
            /*
             * 2026-04-17: if a new WS client just upgraded, emit a
             * full status_snapshot now — bypassing the pause/quiet
             * window. Without this, a page refresh that landed inside
             * a command-in-flight or post-command quiet window could
             * leave the GUI blocked at "Waiting for controller
             * firmware handshake…" for up to ~400 ms, and some
             * browsers timed out their initial handshake before that.
             * The consume call is read-then-clear, so the snapshot is
             * emitted once per new client.
             */
            if (laser_controller_wireless_consume_new_client_pending()) {
                laser_controller_comms_emit_status_event(&status);
                last_status_ms = now_ms;
            }
            const laser_controller_time_ms_t fast_telemetry_period_ms =
                wireless_clients_active ?
                    LASER_CONTROLLER_COMMS_WIRELESS_FAST_TELEMETRY_PERIOD_MS :
                    LASER_CONTROLLER_COMMS_FAST_TELEMETRY_PERIOD_MS;
            /*
             * fast_telemetry is NOT gated by `pause_wireless_telemetry`
             * (2026-04-17 user directive "I need at least 5 refreshes
             * per second over AP"). On a Wi-Fi session where the host
             * sends even periodic status probes, the 400-ms quiet
             * window starved fast telemetry. Stage 2 async broadcast
             * serializes every WS frame on the httpd task, so command
             * responses and fast frames cannot interleave mid-JSON; the
             * quiet window's original purpose is therefore covered by
             * the transport layer. Live + status broadcasts still honor
             * the pause so they don't pile up behind command traffic.
             */
            const bool emit_fast_telemetry =
                (last_fast_telemetry_ms == 0U ||
                (now_ms - last_fast_telemetry_ms) >=
                    fast_telemetry_period_ms);
            const bool emit_live_telemetry =
                !pause_wireless_telemetry &&
                (last_live_telemetry_ms == 0U ||
                (now_ms - last_live_telemetry_ms) >=
                    LASER_CONTROLLER_COMMS_LIVE_TELEMETRY_PERIOD_MS);
            const bool emit_status =
                !pause_wireless_telemetry &&
                (last_status_ms == 0U ||
                (now_ms - last_status_ms) >=
                    LASER_CONTROLLER_COMMS_STATUS_PERIOD_MS);

            if (emit_fast_telemetry) {
                laser_controller_comms_emit_fast_telemetry_event(&status);
                last_fast_telemetry_ms = now_ms;
            }

            if (emit_live_telemetry) {
                laser_controller_comms_emit_live_telemetry_event(&status);
                last_live_telemetry_ms = now_ms;
            }

            if (emit_status) {
                laser_controller_comms_emit_status_event(&status);
                last_status_ms = now_ms;
            }
        }

        if (total_log_count != last_log_count) {
            const size_t backlog =
                total_log_count > last_log_count ?
                    (total_log_count - last_log_count) :
                    0U;
            const size_t requested =
                backlog > LASER_CONTROLLER_LOG_ENTRY_COUNT ?
                    LASER_CONTROLLER_LOG_ENTRY_COUNT :
                    backlog;
            const size_t copied =
                laser_controller_logger_copy_recent(s_tx_log_entries, requested);
            const size_t first_index =
                total_log_count > copied ? (total_log_count - copied) : 0U;
            const size_t skip =
                last_log_count > first_index ? (last_log_count - first_index) : 0U;

            for (size_t index = skip; index < copied; ++index) {
                laser_controller_comms_emit_log_event(&s_tx_log_entries[index]);
            }

            last_log_count = total_log_count;
        }

        vTaskDelay(pdMS_TO_TICKS(LASER_CONTROLLER_COMMS_TX_PERIOD_MS));
    }
}

#if !CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
static void laser_controller_comms_rx_task(void *argument)
{
    char line[LASER_CONTROLLER_COMMS_MAX_LINE_LEN];

    (void)argument;

    for (;;) {
        if (fgets(line, sizeof(line), stdin) == NULL) {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        laser_controller_comms_receive_line(line);
    }
}
#endif

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED
static void laser_controller_comms_usb_rx_task(void *argument)
{
    char line[LASER_CONTROLLER_COMMS_MAX_LINE_LEN];
    size_t length = 0U;

    (void)argument;

    for (;;) {
        uint8_t ch = 0U;
        const int read_count =
            usb_serial_jtag_read_bytes(&ch, 1U, pdMS_TO_TICKS(20));

        if (read_count <= 0) {
            continue;
        }

        if (ch == '\r') {
            continue;
        }

        if (ch == '\n') {
            if (length == 0U) {
                continue;
            }
            line[length] = '\0';
            laser_controller_comms_receive_line(line);
            length = 0U;
            continue;
        }

        if (length < (sizeof(line) - 1U)) {
            line[length++] = (char)ch;
        } else {
            length = 0U;
        }
    }
}
#endif

void laser_controller_comms_start(void)
{
    usb_serial_jtag_driver_config_t usb_config =
        USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    TaskHandle_t tx_handle = NULL;
    TaskHandle_t cmd_handle = NULL;
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED
    TaskHandle_t usb_rx_handle = NULL;
#endif
#if !CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    TaskHandle_t rx_handle = NULL;
#endif

    if (s_comms_started) {
        return;
    }

    if (s_output_lock == NULL) {
        s_output_lock = xSemaphoreCreateMutexStatic(&s_output_lock_buffer);
    }
    if (s_command_queue == NULL) {
        s_command_queue = xQueueCreateStatic(
            LASER_CONTROLLER_COMMS_QUEUE_DEPTH,
            sizeof(laser_controller_comms_line_t),
            s_command_queue_storage,
            &s_command_queue_buffer);
    }

    (void)setvbuf(stdout, NULL, _IOLBF, 0);

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED
    {
        const esp_err_t usb_err = usb_serial_jtag_driver_install(&usb_config);
        if (usb_err != ESP_OK && usb_err != ESP_ERR_INVALID_STATE) {
            laser_controller_logger_logf(
                laser_controller_board_uptime_ms(),
                "comms",
                "usb serial/jtag rx install failed: %s",
                esp_err_to_name(usb_err));
        }
    }
#endif

    tx_handle = xTaskCreateStaticPinnedToCore(
        laser_controller_comms_tx_task,
        "laser_tx",
        LASER_CONTROLLER_COMMS_TX_STACK_BYTES,
        NULL,
        LASER_CONTROLLER_COMMS_TX_PRIORITY,
        s_tx_task_stack,
        &s_tx_task_tcb,
        tskNO_AFFINITY);
    cmd_handle = xTaskCreateStaticPinnedToCore(
        laser_controller_comms_command_task,
        "laser_cmd",
        LASER_CONTROLLER_COMMS_RX_STACK_BYTES,
        NULL,
        LASER_CONTROLLER_COMMS_CMD_PRIORITY,
        s_cmd_task_stack,
        &s_cmd_task_tcb,
        tskNO_AFFINITY);

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED
    usb_rx_handle = xTaskCreateStaticPinnedToCore(
        laser_controller_comms_usb_rx_task,
        "laser_usb_rx",
        LASER_CONTROLLER_COMMS_RX_STACK_BYTES,
        NULL,
        LASER_CONTROLLER_COMMS_RX_PRIORITY,
        s_usb_rx_task_stack,
        &s_usb_rx_task_tcb,
        tskNO_AFFINITY);
#endif
#if !CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    rx_handle = xTaskCreateStaticPinnedToCore(
        laser_controller_comms_rx_task,
        "laser_rx",
        LASER_CONTROLLER_COMMS_RX_STACK_BYTES,
        NULL,
        LASER_CONTROLLER_COMMS_RX_PRIORITY,
        s_rx_task_stack,
        &s_rx_task_tcb,
        tskNO_AFFINITY);
#endif

    if (tx_handle != NULL &&
        cmd_handle != NULL &&
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED
        usb_rx_handle != NULL
#endif
#if !CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
        &&
        rx_handle != NULL
#endif
    ) {
        (void)laser_controller_wireless_start();
        s_comms_started = true;
    }
}
