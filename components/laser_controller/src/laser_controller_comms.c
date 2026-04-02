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
#include "laser_controller_wireless.h"

#define LASER_CONTROLLER_COMMS_TX_STACK_BYTES 6144U
#define LASER_CONTROLLER_COMMS_RX_STACK_BYTES 8192U
#define LASER_CONTROLLER_COMMS_TX_PRIORITY    4U
#define LASER_CONTROLLER_COMMS_RX_PRIORITY    3U
#define LASER_CONTROLLER_COMMS_CMD_PRIORITY   3U
#define LASER_CONTROLLER_COMMS_TX_PERIOD_MS   40U
#define LASER_CONTROLLER_COMMS_FAST_TELEMETRY_PERIOD_MS 50U
#define LASER_CONTROLLER_COMMS_LIVE_TELEMETRY_PERIOD_MS 250U
#define LASER_CONTROLLER_COMMS_STATUS_PERIOD_MS    1000U
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
#define LASER_CONTROLLER_HAPTIC_GPIO_WAIT_MS   400U
#define LASER_CONTROLLER_GPIO_OVERRIDE_WAIT_MS 400U

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

typedef struct {
    char *data;
    size_t capacity;
    size_t length;
    bool truncated;
} laser_controller_comms_buffer_t;

static bool laser_controller_comms_extract_float(
    const char *line,
    const char *key,
    float *value);
static bool laser_controller_comms_extract_bool(
    const char *line,
    const char *key,
    bool *value);

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
    fflush(stdout);
    laser_controller_wireless_broadcast_text(text);
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
               pin->level_high == level_high;
    }

    return pin->pullup_enabled == pullup_enabled &&
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
    const float actual_lambda_nm = laser_controller_comms_lambda_from_temp(
        &status->config,
        status->inputs.tec_temp_c);
    const float lambda_drift_nm = fabsf(actual_lambda_nm - status->bench.target_lambda_nm);
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

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"wireless\":{\"started\":%s,\"apReady\":%s,\"clientCount\":%u,\"ssid\":",
        wireless.started ? "true" : "false",
        wireless.ap_ready ? "true" : "false",
        (unsigned int)wireless.client_count);
    laser_controller_comms_write_escaped_string(buffer, wireless.ssid);
    laser_controller_comms_buffer_append_raw(buffer, ",\"wsUrl\":");
    laser_controller_comms_write_escaped_string(buffer, wireless.ws_url);
    laser_controller_comms_buffer_append_raw(buffer, "},");

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
        "],\"sourceIsHostOnly\":%s},",
        status->inputs.pd_source_is_host_only ? "true" : "false");

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
        "\"laser\":{\"alignmentEnabled\":%s,\"nirEnabled\":%s,\"driverStandby\":%s,\"measuredCurrentA\":%.3f,\"commandedCurrentA\":%.3f,\"loopGood\":%s,\"driverTempC\":%.2f},",
        status->outputs.enable_alignment_laser ? "true" : "false",
        status->decision.nir_output_enable ? "true" : "false",
        status->outputs.assert_driver_standby ? "true" : "false",
        status->inputs.measured_laser_current_a,
        status->bench.high_state_current_a,
        status->inputs.driver_loop_good ? "true" : "false",
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

    laser_controller_comms_write_peripheral_readback_json(buffer, status);
    laser_controller_comms_write_gpio_inspector_json(buffer, status);

    laser_controller_comms_buffer_append_raw(buffer, "\"bench\":{");
    laser_controller_comms_buffer_append_raw(buffer, "\"targetMode\":");
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_bench_target_mode_name(status->bench.target_mode));
    laser_controller_comms_buffer_append_fmt(
        buffer,
        ",\"requestedNirEnabled\":%s,\"modulationEnabled\":%s,\"modulationFrequencyHz\":%lu,\"modulationDutyCyclePct\":%lu,\"lowStateCurrentA\":%.3f},",
        status->bench.requested_nir ? "true" : "false",
        status->bench.modulation_enabled ? "true" : "false",
        (unsigned long)status->bench.modulation_frequency_hz,
        (unsigned long)status->bench.modulation_duty_cycle_pct,
        status->bench.low_state_current_a);

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"safety\":{\"allowAlignment\":%s,\"allowNir\":%s,\"horizonBlocked\":%s,\"distanceBlocked\":%s,\"lambdaDriftBlocked\":%s,\"tecTempAdcBlocked\":%s,\"horizonThresholdDeg\":%.2f,\"horizonHysteresisDeg\":%.2f,\"tofMinRangeM\":%.3f,\"tofMaxRangeM\":%.3f,\"tofHysteresisM\":%.3f,\"imuStaleMs\":%lu,\"tofStaleMs\":%lu,\"railGoodTimeoutMs\":%lu,\"lambdaDriftLimitNm\":%.2f,\"lambdaDriftHysteresisNm\":%.2f,\"lambdaDriftHoldMs\":%lu,\"ldOvertempLimitC\":%.2f,\"tecTempAdcTripV\":%.3f,\"tecTempAdcHysteresisV\":%.3f,\"tecTempAdcHoldMs\":%lu,\"tecMinCommandC\":%.2f,\"tecMaxCommandC\":%.2f,\"tecReadyToleranceC\":%.2f,\"maxLaserCurrentA\":%.2f,\"actualLambdaNm\":%.2f,\"targetLambdaNm\":%.2f,\"lambdaDriftNm\":%.2f,\"tempAdcVoltageV\":%.3f},",
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
        actual_lambda_nm,
        status->bench.target_lambda_nm,
        lambda_drift_nm,
        status->inputs.tec_temp_adc_voltage_v);

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"bringup\":{\"serviceModeRequested\":%s,\"serviceModeActive\":%s,\"persistenceDirty\":%s,\"persistenceAvailable\":%s,\"lastSaveOk\":%s,\"profileRevision\":%lu,\"profileName\":",
        status->bringup.service_mode_requested ? "true" : "false",
        status->state == LASER_CONTROLLER_STATE_SERVICE_MODE ? "true" : "false",
        status->bringup.persistence_dirty ? "true" : "false",
        status->bringup.persistence_available ? "true" : "false",
        status->bringup.last_save_ok ? "true" : "false",
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
        ",\"imuOdrHz\":%lu,\"imuAccelRangeG\":%lu,\"imuGyroRangeDps\":%lu,\"imuGyroEnabled\":%s,\"imuLpf2Enabled\":%s,\"imuTimestampEnabled\":%s,\"imuBduEnabled\":%s,\"imuIfIncEnabled\":%s,\"imuI2cDisabled\":%s,\"tofMinRangeM\":%.3f,\"tofMaxRangeM\":%.3f,\"tofStaleTimeoutMs\":%lu,\"pdProfiles\":[",
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
        (unsigned long)status->bringup.tof_stale_timeout_ms);
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
    const float actual_lambda_nm = laser_controller_comms_lambda_from_temp(
        &status->config,
        status->inputs.tec_temp_c);
    const float lambda_drift_nm =
        fabsf(actual_lambda_nm - status->bench.target_lambda_nm);
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

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"wireless\":{\"started\":%s,\"apReady\":%s,\"clientCount\":%u,\"ssid\":",
        wireless.started ? "true" : "false",
        wireless.ap_ready ? "true" : "false",
        (unsigned int)wireless.client_count);
    laser_controller_comms_write_escaped_string(buffer, wireless.ssid);
    laser_controller_comms_buffer_append_raw(buffer, ",\"wsUrl\":");
    laser_controller_comms_write_escaped_string(buffer, wireless.ws_url);
    laser_controller_comms_buffer_append_raw(buffer, "},");

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
        "],\"sourceIsHostOnly\":%s},",
        status->inputs.pd_source_is_host_only ? "true" : "false");

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
        "\"laser\":{\"alignmentEnabled\":%s,\"nirEnabled\":%s,\"driverStandby\":%s,\"measuredCurrentA\":%.3f,\"commandedCurrentA\":%.3f,\"loopGood\":%s,\"driverTempC\":%.2f},",
        status->outputs.enable_alignment_laser ? "true" : "false",
        status->decision.nir_output_enable ? "true" : "false",
        status->outputs.assert_driver_standby ? "true" : "false",
        status->inputs.measured_laser_current_a,
        status->bench.high_state_current_a,
        status->inputs.driver_loop_good ? "true" : "false",
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
        ",\"requestedNirEnabled\":%s,\"modulationEnabled\":%s,\"modulationFrequencyHz\":%lu,\"modulationDutyCyclePct\":%lu,\"lowStateCurrentA\":%.3f},",
        status->bench.requested_nir ? "true" : "false",
        status->bench.modulation_enabled ? "true" : "false",
        (unsigned long)status->bench.modulation_frequency_hz,
        (unsigned long)status->bench.modulation_duty_cycle_pct,
        status->bench.low_state_current_a);

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"safety\":{\"allowAlignment\":%s,\"allowNir\":%s,\"horizonBlocked\":%s,\"distanceBlocked\":%s,\"lambdaDriftBlocked\":%s,\"tecTempAdcBlocked\":%s,\"horizonThresholdDeg\":%.2f,\"horizonHysteresisDeg\":%.2f,\"tofMinRangeM\":%.3f,\"tofMaxRangeM\":%.3f,\"tofHysteresisM\":%.3f,\"imuStaleMs\":%lu,\"tofStaleMs\":%lu,\"railGoodTimeoutMs\":%lu,\"lambdaDriftLimitNm\":%.2f,\"lambdaDriftHysteresisNm\":%.2f,\"lambdaDriftHoldMs\":%lu,\"ldOvertempLimitC\":%.2f,\"tecTempAdcTripV\":%.3f,\"tecTempAdcHysteresisV\":%.3f,\"tecTempAdcHoldMs\":%lu,\"tecMinCommandC\":%.2f,\"tecMaxCommandC\":%.2f,\"tecReadyToleranceC\":%.2f,\"maxLaserCurrentA\":%.2f,\"actualLambdaNm\":%.2f,\"targetLambdaNm\":%.2f,\"lambdaDriftNm\":%.2f,\"tempAdcVoltageV\":%.3f},",
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
        actual_lambda_nm,
        status->bench.target_lambda_nm,
        lambda_drift_nm,
        status->inputs.tec_temp_adc_voltage_v);

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"bringup\":{\"serviceModeRequested\":%s,\"serviceModeActive\":%s,\"persistenceDirty\":%s,\"persistenceAvailable\":%s,\"lastSaveOk\":%s,\"profileRevision\":%lu,\"profileName\":",
        status->bringup.service_mode_requested ? "true" : "false",
        status->state == LASER_CONTROLLER_STATE_SERVICE_MODE ? "true" : "false",
        status->bringup.persistence_dirty ? "true" : "false",
        status->bringup.persistence_available ? "true" : "false",
        status->bringup.last_save_ok ? "true" : "false",
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
    const float beam_pitch_deg =
        status->inputs.beam_pitch_rad * LASER_CONTROLLER_DEG_PER_RAD;
    const float beam_roll_deg =
        status->inputs.beam_roll_rad * LASER_CONTROLLER_DEG_PER_RAD;
    const float beam_yaw_deg =
        status->inputs.beam_yaw_rad * LASER_CONTROLLER_DEG_PER_RAD;
    const float beam_pitch_limit_deg =
        status->config.thresholds.horizon_threshold_rad * LASER_CONTROLLER_DEG_PER_RAD;

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
        "\"pd\":{\"contractValid\":%s,\"negotiatedPowerW\":%.2f,\"sourceVoltageV\":%.2f,\"sourceCurrentA\":%.2f,\"operatingCurrentA\":%.2f,\"sourceIsHostOnly\":%s},",
        status->inputs.pd_contract_valid ? "true" : "false",
        status->inputs.pd_negotiated_power_w,
        status->inputs.pd_source_voltage_v,
        status->inputs.pd_source_current_a,
        status->inputs.pd_operating_current_a,
        status->inputs.pd_source_is_host_only ? "true" : "false");

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
        "\"tof\":{\"valid\":%s,\"fresh\":%s,\"distanceM\":%.3f},",
        status->inputs.tof_data_valid ? "true" : "false",
        status->inputs.tof_data_fresh ? "true" : "false",
        status->inputs.tof_distance_m);

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"laser\":{\"alignmentEnabled\":%s,\"nirEnabled\":%s,\"driverStandby\":%s,\"measuredCurrentA\":%.3f,\"loopGood\":%s,\"driverTempC\":%.2f},",
        status->outputs.enable_alignment_laser ? "true" : "false",
        status->decision.nir_output_enable ? "true" : "false",
        status->outputs.assert_driver_standby ? "true" : "false",
        status->inputs.measured_laser_current_a,
        status->inputs.driver_loop_good ? "true" : "false",
        status->inputs.laser_driver_temp_c);

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"tec\":{\"tempGood\":%s,\"tempC\":%.2f,\"tempAdcVoltageV\":%.3f,\"currentA\":%.2f,\"voltageV\":%.2f,\"settlingSecondsRemaining\":%u},",
        status->inputs.tec_temp_good ? "true" : "false",
        status->inputs.tec_temp_c,
        status->inputs.tec_temp_adc_voltage_v,
        status->inputs.tec_current_a,
        status->inputs.tec_voltage_v,
        status->inputs.tec_temp_good ? 0U : 1U);

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"safety\":{\"allowAlignment\":%s,\"allowNir\":%s,\"horizonBlocked\":%s,\"distanceBlocked\":%s,\"lambdaDriftBlocked\":%s,\"tecTempAdcBlocked\":%s},",
        status->decision.allow_alignment ? "true" : "false",
        status->decision.allow_nir ? "true" : "false",
        status->decision.horizon_blocked ? "true" : "false",
        status->decision.distance_blocked ? "true" : "false",
        status->decision.lambda_drift_blocked ? "true" : "false",
        status->decision.tec_temp_adc_blocked ? "true" : "false");

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"buttons\":{\"stage1Pressed\":%s,\"stage2Pressed\":%s},",
        status->inputs.button.stage1_pressed ? "true" : "false",
        status->inputs.button.stage2_pressed ? "true" : "false");

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"bringup\":{\"serviceModeRequested\":%s,\"serviceModeActive\":%s,\"illumination\":{\"tof\":{\"enabled\":%s}}},",
        status->bringup.service_mode_requested ? "true" : "false",
        status->state == LASER_CONTROLLER_STATE_SERVICE_MODE ? "true" : "false",
        status->bringup.tof_illumination_enabled ? "true" : "false");

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"fault\":{\"latched\":%s,\"activeCode\":",
        status->fault_latched ? "true" : "false");
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_fault_code_name(status->active_fault_code));
    laser_controller_comms_buffer_append_raw(buffer, "}}");
}

static void laser_controller_comms_write_fast_telemetry_json(
    laser_controller_comms_buffer_t *buffer,
    const laser_controller_runtime_status_t *status)
{
    const float beam_pitch_deg =
        status->inputs.beam_pitch_rad * LASER_CONTROLLER_DEG_PER_RAD;
    const float beam_roll_deg =
        status->inputs.beam_roll_rad * LASER_CONTROLLER_DEG_PER_RAD;
    const float beam_yaw_deg =
        status->inputs.beam_yaw_rad * LASER_CONTROLLER_DEG_PER_RAD;
    const float beam_pitch_limit_deg =
        status->config.thresholds.horizon_threshold_rad * LASER_CONTROLLER_DEG_PER_RAD;

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
        "\"imu\":{\"valid\":%s,\"fresh\":%s,\"beamPitchDeg\":%.2f,\"beamRollDeg\":%.2f,\"beamYawDeg\":%.2f,\"beamYawRelative\":true,\"beamPitchLimitDeg\":%.2f},",
        status->inputs.imu_data_valid ? "true" : "false",
        status->inputs.imu_data_fresh ? "true" : "false",
        beam_pitch_deg,
        beam_roll_deg,
        beam_yaw_deg,
        beam_pitch_limit_deg);

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"tof\":{\"valid\":%s,\"fresh\":%s,\"distanceM\":%.3f},",
        status->inputs.tof_data_valid ? "true" : "false",
        status->inputs.tof_data_fresh ? "true" : "false",
        status->inputs.tof_distance_m);

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"laser\":{\"alignmentEnabled\":%s,\"nirEnabled\":%s,\"driverStandby\":%s,\"measuredCurrentA\":%.3f,\"loopGood\":%s,\"driverTempC\":%.2f},",
        status->outputs.enable_alignment_laser ? "true" : "false",
        status->decision.nir_output_enable ? "true" : "false",
        status->outputs.assert_driver_standby ? "true" : "false",
        status->inputs.measured_laser_current_a,
        status->inputs.driver_loop_good ? "true" : "false",
        status->inputs.laser_driver_temp_c);

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"tec\":{\"tempGood\":%s,\"tempC\":%.2f,\"tempAdcVoltageV\":%.3f,\"currentA\":%.2f,\"voltageV\":%.2f,\"settlingSecondsRemaining\":%u},",
        status->inputs.tec_temp_good ? "true" : "false",
        status->inputs.tec_temp_c,
        status->inputs.tec_temp_adc_voltage_v,
        status->inputs.tec_current_a,
        status->inputs.tec_voltage_v,
        status->inputs.tec_temp_good ? 0U : 1U);

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"safety\":{\"allowAlignment\":%s,\"allowNir\":%s,\"horizonBlocked\":%s,\"distanceBlocked\":%s,\"lambdaDriftBlocked\":%s,\"tecTempAdcBlocked\":%s},",
        status->decision.allow_alignment ? "true" : "false",
        status->decision.allow_nir ? "true" : "false",
        status->decision.horizon_blocked ? "true" : "false",
        status->decision.distance_blocked ? "true" : "false",
        status->decision.lambda_drift_blocked ? "true" : "false",
        status->decision.tec_temp_adc_blocked ? "true" : "false");

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"buttons\":{\"stage1Pressed\":%s,\"stage2Pressed\":%s},",
        status->inputs.button.stage1_pressed ? "true" : "false",
        status->inputs.button.stage2_pressed ? "true" : "false");

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"fault\":{\"latched\":%s,\"activeCode\":",
        status->fault_latched ? "true" : "false");
    laser_controller_comms_write_escaped_string(
        buffer,
        laser_controller_fault_code_name(status->active_fault_code));
    laser_controller_comms_buffer_append_raw(buffer, "}}");
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
    const float actual_lambda_nm = laser_controller_comms_lambda_from_temp(
        &status->config,
        status->inputs.tec_temp_c);
    const float lambda_drift_nm =
        fabsf(actual_lambda_nm - status->bench.target_lambda_nm);

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
        "\"pd\":{\"contractValid\":%s,\"negotiatedPowerW\":%.2f,\"sourceVoltageV\":%.2f,\"sourceCurrentA\":%.2f,\"operatingCurrentA\":%.2f,\"sourceIsHostOnly\":%s},",
        status->inputs.pd_contract_valid ? "true" : "false",
        status->inputs.pd_negotiated_power_w,
        status->inputs.pd_source_voltage_v,
        status->inputs.pd_source_current_a,
        status->inputs.pd_operating_current_a,
        status->inputs.pd_source_is_host_only ? "true" : "false");

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
        "\"laser\":{\"alignmentEnabled\":%s,\"nirEnabled\":%s,\"driverStandby\":%s,\"measuredCurrentA\":%.3f,\"commandedCurrentA\":%.3f,\"loopGood\":%s,\"driverTempC\":%.2f},",
        status->outputs.enable_alignment_laser ? "true" : "false",
        status->decision.nir_output_enable ? "true" : "false",
        status->outputs.assert_driver_standby ? "true" : "false",
        status->inputs.measured_laser_current_a,
        status->bench.high_state_current_a,
        status->inputs.driver_loop_good ? "true" : "false",
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

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"safety\":{\"allowAlignment\":%s,\"allowNir\":%s,\"horizonBlocked\":%s,\"distanceBlocked\":%s,\"lambdaDriftBlocked\":%s,\"tecTempAdcBlocked\":%s,\"actualLambdaNm\":%.2f,\"targetLambdaNm\":%.2f,\"lambdaDriftNm\":%.2f,\"tempAdcVoltageV\":%.3f},",
        status->decision.allow_alignment ? "true" : "false",
        status->decision.allow_nir ? "true" : "false",
        status->decision.horizon_blocked ? "true" : "false",
        status->decision.distance_blocked ? "true" : "false",
        status->decision.lambda_drift_blocked ? "true" : "false",
        status->decision.tec_temp_adc_blocked ? "true" : "false",
        actual_lambda_nm,
        status->bench.target_lambda_nm,
        lambda_drift_nm,
        status->inputs.tec_temp_adc_voltage_v);

    laser_controller_comms_buffer_append_fmt(
        buffer,
        "\"bringup\":{\"serviceModeRequested\":%s,\"serviceModeActive\":%s,\"power\":{\"ldRequested\":%s,\"tecRequested\":%s},\"illumination\":{\"tof\":{\"enabled\":%s,\"dutyCyclePct\":%lu,\"frequencyHz\":%lu}}},",
        status->bringup.service_mode_requested ? "true" : "false",
        status->state == LASER_CONTROLLER_STATE_SERVICE_MODE ? "true" : "false",
        status->bringup.ld_rail_debug_enabled ? "true" : "false",
        status->bringup.tec_rail_debug_enabled ? "true" : "false",
        status->bringup.tof_illumination_enabled ? "true" : "false",
        (unsigned long)status->bringup.tof_illumination_duty_cycle_pct,
        (unsigned long)status->bringup.tof_illumination_frequency_hz);

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
        laser_controller_comms_buffer_append_raw(buffer, "\"lastFaultAtIso\":null}}");
    } else {
        laser_controller_comms_buffer_append_raw(
            buffer,
            "\"lastFaultAtIso\":\"1970-01-01T00:00:00Z\"}}");
    }
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
    laser_controller_comms_buffer_append_fmt(
        &buffer,
        ",\"activeCount\":%lu,\"tripCounter\":%lu}}\n",
        (unsigned long)status->active_fault_count,
        (unsigned long)status->trip_counter);
    laser_controller_comms_emit_buffer_locked(&buffer, "faults_response");
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

    match = strstr(line, bare_key);
    if (match == NULL) {
        return NULL;
    }

    match += bare_len;
    while (*match != '\0' && isspace((unsigned char)*match)) {
        ++match;
    }
    if (*match != ':') {
        return NULL;
    }

    ++match;
    while (*match != '\0' && isspace((unsigned char)*match)) {
        ++match;
    }

    if (quoted) {
        if (*match != '\"') {
            return NULL;
        }
        ++match;
    }

    return match;
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

    if (laser_controller_comms_extract_string(
            line,
            "\"type\":\"",
            envelope_type,
            sizeof(envelope_type)) &&
        strcmp(envelope_type, "cmd") != 0) {
        return;
    }

    if (!laser_controller_comms_extract_uint(line, "\"id\":", &id) ||
        !laser_controller_comms_extract_string(
            line,
            "\"cmd\":\"",
            command,
            sizeof(command))) {
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

    if (strcmp(command, "get_status") == 0) {
        laser_controller_comms_emit_full_status_response(id, &status);
        return;
    }

    if (strcmp(command, "get_faults") == 0) {
        laser_controller_comms_emit_faults_response(id, &status);
        return;
    }

    if (strcmp(command, "clear_faults") == 0) {
        if (!laser_controller_comms_service_mode_active(&status)) {
            laser_controller_comms_emit_error_response(
                id,
                "Enter service mode before clearing latched faults.");
            return;
        }

        if (laser_controller_app_clear_fault_latch() != ESP_OK) {
            laser_controller_comms_emit_error_response(
                id,
                "Fault latch clear rejected because recovery criteria are not met.");
            return;
        }

        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_status_response(id, &status);
        return;
    }

    if (strcmp(command, "enter_service_mode") == 0) {
        laser_controller_service_set_mode_requested(true, now_ms);
        laser_controller_logger_log(now_ms, "service", "service mode requested");
        if (!laser_controller_comms_wait_for_service_mode(true, &status)) {
            laser_controller_comms_emit_error_response(
                id,
                "Service mode request timed out before the controller entered write-safe state.");
            return;
        }
        laser_controller_comms_emit_status_response(id, &status);
        return;
    }

    if (strcmp(command, "exit_service_mode") == 0) {
        laser_controller_service_set_mode_requested(false, now_ms);
        laser_controller_bench_clear_requests(now_ms);
        laser_controller_logger_log(now_ms, "service", "service mode exited");
        if (!laser_controller_comms_wait_for_service_mode(false, &status)) {
            laser_controller_comms_emit_error_response(
                id,
                "Service mode exit timed out before the controller returned to normal supervision.");
            return;
        }
        laser_controller_comms_emit_status_response(id, &status);
        return;
    }

    if (strcmp(command, "get_bringup_profile") == 0) {
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_full_status_response(id, &status);
        return;
    }

    if (strcmp(command, "refresh_pd_status") == 0) {
        laser_controller_board_force_pd_refresh();
        vTaskDelay(pdMS_TO_TICKS(LASER_CONTROLLER_PD_REFRESH_WAIT_MS));
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_status_response(id, &status);
        return;
    }

    if (!laser_controller_comms_service_mode_active(&status) &&
        (strcmp(command, "set_laser_power") == 0 ||
         strcmp(command, "set_max_current") == 0 ||
         strcmp(command, "set_runtime_safety") == 0 ||
         strcmp(command, "set_supply_enable") == 0 ||
         strcmp(command, "set_haptic_enable") == 0 ||
         strcmp(command, "tof_illumination_set") == 0 ||
         strcmp(command, "set_gpio_override") == 0 ||
         strcmp(command, "clear_gpio_overrides") == 0 ||
         strcmp(command, "pd_debug_config") == 0 ||
         strcmp(command, "pd_save_firmware_plan") == 0 ||
         strcmp(command, "pd_burn_nvm") == 0 ||
         strcmp(command, "set_target_temp") == 0 ||
         strcmp(command, "set_target_lambda") == 0 ||
         strcmp(command, "laser_output_enable") == 0 ||
         strcmp(command, "enable_alignment") == 0 ||
         strcmp(command, "configure_modulation") == 0 ||
         strcmp(command, "apply_bringup_preset") == 0 ||
         strcmp(command, "dac_debug_set") == 0 ||
         strcmp(command, "dac_debug_config") == 0 ||
         strcmp(command, "imu_debug_config") == 0 ||
         strcmp(command, "tof_debug_config") == 0 ||
         strcmp(command, "haptic_debug_config") == 0 ||
         strcmp(command, "haptic_external_trigger_pattern") == 0 ||
         strcmp(command, "haptic_debug_fire") == 0 ||
         strcmp(command, "i2c_write") == 0 ||
         strcmp(command, "spi_write") == 0)) {
        laser_controller_comms_emit_error_response(
            id,
            "Enter service mode before issuing mutating bring-up or bench commands.");
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
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_status_response(id, &status);
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
        laser_controller_board_force_pd_refresh();
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

        laser_controller_board_force_pd_refresh();
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

        laser_controller_board_force_pd_refresh();
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

    if (strcmp(command, "set_runtime_safety") == 0) {
        laser_controller_runtime_safety_policy_t policy = {
            .thresholds = status.config.thresholds,
            .timeouts = status.config.timeouts,
        };
        float value_f = 0.0f;
        uint32_t value_u = 0U;

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

        if (laser_controller_app_set_runtime_safety_policy(&policy) != ESP_OK) {
            laser_controller_comms_emit_error_response(
                id,
                "Runtime safety update rejected because one or more values are invalid.");
            return;
        }

        laser_controller_logger_log(
            now_ms,
            "config",
            "runtime safety policy updated from host");
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_status_response(id, &status);
        return;
    }

    if (strcmp(command, "set_target_temp") == 0) {
        float target_temp_c = 0.0f;

        if (!laser_controller_comms_extract_float(line, "\"temp_c\":", &target_temp_c)) {
            laser_controller_comms_emit_error_response(id, "Missing temp_c.");
            return;
        }

        laser_controller_bench_set_target_temp_c(&status.config, target_temp_c, now_ms);
        laser_controller_logger_logf(
            now_ms,
            "bench",
            "bench tec target staged -> %.2f C",
            target_temp_c);
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_status_response(id, &status);
        return;
    }

    if (strcmp(command, "set_target_lambda") == 0) {
        float target_lambda_nm = 0.0f;

        if (!laser_controller_comms_extract_float(line, "\"lambda_nm\":", &target_lambda_nm)) {
            laser_controller_comms_emit_error_response(id, "Missing lambda_nm.");
            return;
        }

        laser_controller_bench_set_target_lambda_nm(&status.config, target_lambda_nm, now_ms);
        laser_controller_logger_logf(
            now_ms,
            "bench",
            "bench wavelength target staged -> %.2f nm",
            target_lambda_nm);
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_status_response(id, &status);
        return;
    }

    if (strcmp(command, "enable_alignment") == 0) {
        laser_controller_bench_set_alignment_requested(true, now_ms);
        laser_controller_logger_log(now_ms, "bench", "alignment request staged");
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_status_response(id, &status);
        return;
    }

    if (strcmp(command, "disable_alignment") == 0) {
        laser_controller_bench_set_alignment_requested(false, now_ms);
        laser_controller_logger_log(now_ms, "bench", "alignment request cleared");
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_status_response(id, &status);
        return;
    }

    if (strcmp(command, "laser_output_enable") == 0) {
        laser_controller_bench_set_nir_requested(true, now_ms);
        laser_controller_logger_log(now_ms, "bench", "nir request staged");
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_status_response(id, &status);
        return;
    }

    if (strcmp(command, "laser_output_disable") == 0) {
        laser_controller_bench_set_nir_requested(false, now_ms);
        laser_controller_logger_log(now_ms, "bench", "nir request cleared");
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_status_response(id, &status);
        return;
    }

    if (strcmp(command, "configure_modulation") == 0) {
        bool enabled = false;
        uint32_t frequency_hz = 0U;
        uint32_t duty_cycle_pct = 0U;
        float low_state_current_a = 0.0f;

        if (!laser_controller_comms_extract_bool(line, "\"enabled\":", &enabled) ||
            !laser_controller_comms_extract_uint(line, "\"frequency_hz\":", &frequency_hz) ||
            !laser_controller_comms_extract_uint(line, "\"duty_cycle_pct\":", &duty_cycle_pct) ||
            !laser_controller_comms_extract_float(line, "\"low_current_a\":", &low_state_current_a)) {
            laser_controller_comms_emit_error_response(id, "Missing modulation arguments.");
            return;
        }

        laser_controller_bench_set_modulation(
            enabled,
            frequency_hz,
            duty_cycle_pct,
            low_state_current_a,
            now_ms);
        laser_controller_logger_logf(
            now_ms,
            "bench",
            "pcn modulation staged enabled=%d freq=%lu duty=%lu low=%.3f",
            enabled,
            (unsigned long)frequency_hz,
            (unsigned long)duty_cycle_pct,
            low_state_current_a);
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_status_response(id, &status);
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
        laser_controller_comms_emit_status_response(id, &status);
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
        laser_controller_comms_emit_status_response(id, &status);
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
        laser_controller_comms_emit_status_response(id, &status);
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
        laser_controller_comms_emit_status_response(id, &status);
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
        laser_controller_comms_emit_status_response(id, &status);
        return;
    }

    if (strcmp(command, "save_bringup_profile") == 0) {
        laser_controller_service_save_profile(now_ms);
        laser_controller_logger_log(
            now_ms,
            "service",
            "device-side bring-up profile save requested");
        (void)laser_controller_app_copy_status(&status);
        laser_controller_comms_emit_status_response(id, &status);
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
        laser_controller_comms_emit_status_response(id, &status);
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
        laser_controller_comms_emit_status_response(id, &status);
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
        laser_controller_comms_emit_status_response(id, &status);
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
        laser_controller_comms_emit_status_response(id, &status);
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
        laser_controller_comms_emit_status_response(id, &status);
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
        laser_controller_comms_emit_status_response(id, &status);
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
        laser_controller_comms_emit_status_response(id, &status);
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
        laser_controller_comms_emit_status_response(id, &status);
        return;
    }

    laser_controller_comms_emit_error_response(
        id,
        "Command not implemented in bring-up firmware.");
}

void laser_controller_comms_receive_line(const char *line)
{
    laser_controller_comms_line_t item = { 0 };
    uint32_t id = 0U;

    if (line == NULL || line[0] == '\0' || s_command_queue == NULL) {
        return;
    }

    (void)strlcpy(item.line, line, sizeof(item.line));

    if (xQueueSend(s_command_queue, &item, 0U) == pdPASS) {
        return;
    }

    if (laser_controller_comms_extract_uint(line, "\"id\":", &id)) {
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
            laser_controller_comms_handle_command_line(item.line);
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
            const bool emit_fast_telemetry =
                last_fast_telemetry_ms == 0U ||
                (now_ms - last_fast_telemetry_ms) >=
                    LASER_CONTROLLER_COMMS_FAST_TELEMETRY_PERIOD_MS;
            const bool emit_live_telemetry =
                last_live_telemetry_ms == 0U ||
                (now_ms - last_live_telemetry_ms) >=
                    LASER_CONTROLLER_COMMS_LIVE_TELEMETRY_PERIOD_MS;
            const bool emit_status =
                last_status_ms == 0U ||
                (now_ms - last_status_ms) >=
                    LASER_CONTROLLER_COMMS_STATUS_PERIOD_MS;

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
