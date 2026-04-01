#include "laser_controller_app.h"

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "laser_controller_faults.h"
#include "laser_controller_board.h"
#include "laser_controller_logger.h"
#include "laser_controller_signature.h"
#include "laser_controller_state.h"

#define LASER_CONTROLLER_COMMS_TX_STACK_BYTES 6144U
#define LASER_CONTROLLER_COMMS_RX_STACK_BYTES 8192U
#define LASER_CONTROLLER_COMMS_TX_PRIORITY    4U
#define LASER_CONTROLLER_COMMS_RX_PRIORITY    3U
#define LASER_CONTROLLER_COMMS_TX_PERIOD_MS   100U
#define LASER_CONTROLLER_COMMS_MAX_LINE_LEN   768U
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

static StaticTask_t s_tx_task_tcb;
static StackType_t s_tx_task_stack[LASER_CONTROLLER_COMMS_TX_STACK_BYTES];
#if !CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
static StaticTask_t s_rx_task_tcb;
static StackType_t s_rx_task_stack[LASER_CONTROLLER_COMMS_RX_STACK_BYTES];
#endif
static StaticTask_t s_usb_rx_task_tcb;
static StackType_t s_usb_rx_task_stack[LASER_CONTROLLER_COMMS_RX_STACK_BYTES];
static laser_controller_log_entry_t s_tx_log_entries[LASER_CONTROLLER_LOG_ENTRY_COUNT];
static bool s_comms_started;
static StaticSemaphore_t s_output_lock_buffer;
static SemaphoreHandle_t s_output_lock;

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

static void laser_controller_comms_write_escaped_string(const char *value)
{
    const char *text = value != NULL ? value : "";

    putchar('\"');
    while (*text != '\0') {
        const char c = *text++;

        switch (c) {
            case '\"':
                fputs("\\\"", stdout);
                break;
            case '\\':
                fputs("\\\\", stdout);
                break;
            case '\r':
                fputs("\\r", stdout);
                break;
            case '\n':
                fputs("\\n", stdout);
                break;
            case '\t':
                fputs("\\t", stdout);
                break;
            default:
                if ((unsigned char)c < 0x20U) {
                    fputs("?", stdout);
                } else {
                    putchar(c);
                }
                break;
        }
    }
    putchar('\"');
}

static void laser_controller_comms_write_module_json(
    const laser_controller_module_status_t *module)
{
    if (module == NULL) {
        fputs("{\"expectedPresent\":false,\"debugEnabled\":false,\"detected\":false,\"healthy\":false}", stdout);
        return;
    }

    fputs("{\"expectedPresent\":", stdout);
    fputs(module->expected_present ? "true" : "false", stdout);
    fputs(",\"debugEnabled\":", stdout);
    fputs(module->debug_enabled ? "true" : "false", stdout);
    fputs(",\"detected\":", stdout);
    fputs(module->detected ? "true" : "false", stdout);
    fputs(",\"healthy\":", stdout);
    fputs(module->healthy ? "true" : "false", stdout);
    putchar('}');
}

static void laser_controller_comms_write_pd_profile_json(
    const laser_controller_service_pd_profile_t *profile)
{
    if (profile == NULL) {
        fputs("{\"enabled\":false,\"voltageV\":0.0,\"currentA\":0.0}", stdout);
        return;
    }

    fputs("{\"enabled\":", stdout);
    fputs(profile->enabled ? "true" : "false", stdout);
    (void)printf(
        ",\"voltageV\":%.2f,\"currentA\":%.2f}",
        profile->voltage_v,
        profile->current_a);
}

static void laser_controller_comms_write_error_name_json(int32_t error_code)
{
    laser_controller_comms_write_escaped_string(
        esp_err_to_name((esp_err_t)error_code));
}

static void laser_controller_comms_write_peripheral_readback_json(
    const laser_controller_runtime_status_t *status)
{
    const laser_controller_board_dac_readback_t *dac = &status->inputs.dac;
    const laser_controller_board_pd_readback_t *pd = &status->inputs.pd_readback;
    const laser_controller_board_imu_readback_t *imu = &status->inputs.imu_readback;
    const laser_controller_board_haptic_readback_t *haptic =
        &status->inputs.haptic_readback;
    const laser_controller_board_tof_readback_t *tof =
        &status->inputs.tof_readback;

    fputs("\"peripherals\":{", stdout);

    fputs("\"dac\":{", stdout);
    (void)printf("\"reachable\":%s,", dac->reachable ? "true" : "false");
    (void)printf("\"configured\":%s,", dac->configured ? "true" : "false");
    (void)printf("\"refAlarm\":%s,", dac->ref_alarm ? "true" : "false");
    (void)printf("\"syncReg\":%u,", (unsigned)dac->sync_reg);
    (void)printf("\"configReg\":%u,", (unsigned)dac->config_reg);
    (void)printf("\"gainReg\":%u,", (unsigned)dac->gain_reg);
    (void)printf("\"statusReg\":%u,", (unsigned)dac->status_reg);
    (void)printf("\"dataAReg\":%u,", (unsigned)dac->data_a_reg);
    (void)printf("\"dataBReg\":%u,", (unsigned)dac->data_b_reg);
    (void)printf("\"lastErrorCode\":%ld,", (long)dac->last_error);
    fputs("\"lastError\":", stdout);
    laser_controller_comms_write_error_name_json(dac->last_error);
    fputs("},", stdout);

    fputs("\"pd\":{", stdout);
    (void)printf("\"reachable\":%s,", pd->reachable ? "true" : "false");
    (void)printf("\"attached\":%s,", pd->attached ? "true" : "false");
    (void)printf("\"ccStatusReg\":%u,", (unsigned)pd->cc_status_reg);
    (void)printf("\"pdoCountReg\":%u,", (unsigned)pd->pdo_count_reg);
    (void)printf("\"rdoStatusRaw\":%lu", (unsigned long)pd->rdo_status_raw);
    fputs("},", stdout);

    fputs("\"imu\":{", stdout);
    (void)printf("\"reachable\":%s,", imu->reachable ? "true" : "false");
    (void)printf("\"configured\":%s,", imu->configured ? "true" : "false");
    (void)printf("\"whoAmI\":%u,", (unsigned)imu->who_am_i);
    (void)printf("\"statusReg\":%u,", (unsigned)imu->status_reg);
    (void)printf("\"ctrl1XlReg\":%u,", (unsigned)imu->ctrl1_xl_reg);
    (void)printf("\"ctrl2GReg\":%u,", (unsigned)imu->ctrl2_g_reg);
    (void)printf("\"ctrl3CReg\":%u,", (unsigned)imu->ctrl3_c_reg);
    (void)printf("\"ctrl4CReg\":%u,", (unsigned)imu->ctrl4_c_reg);
    (void)printf("\"ctrl10CReg\":%u,", (unsigned)imu->ctrl10_c_reg);
    (void)printf("\"lastErrorCode\":%ld,", (long)imu->last_error);
    fputs("\"lastError\":", stdout);
    laser_controller_comms_write_error_name_json(imu->last_error);
    fputs("},", stdout);

    fputs("\"haptic\":{", stdout);
    (void)printf("\"reachable\":%s,", haptic->reachable ? "true" : "false");
    (void)printf("\"modeReg\":%u,", (unsigned)haptic->mode_reg);
    (void)printf("\"libraryReg\":%u,", (unsigned)haptic->library_reg);
    (void)printf("\"goReg\":%u,", (unsigned)haptic->go_reg);
    (void)printf("\"feedbackReg\":%u,", (unsigned)haptic->feedback_reg);
    (void)printf("\"lastErrorCode\":%ld,", (long)haptic->last_error);
    fputs("\"lastError\":", stdout);
    laser_controller_comms_write_error_name_json(haptic->last_error);
    fputs("},", stdout);

    fputs("\"tof\":{", stdout);
    (void)printf("\"reachable\":%s,", tof->reachable ? "true" : "false");
    (void)printf("\"configured\":%s,", tof->configured ? "true" : "false");
    (void)printf(
        "\"interruptLineHigh\":%s,",
        tof->interrupt_line_high ? "true" : "false");
    (void)printf(
        "\"ledCtrlAsserted\":%s,",
        tof->led_ctrl_asserted ? "true" : "false");
    (void)printf("\"dataReady\":%s,", tof->data_ready ? "true" : "false");
    (void)printf("\"bootState\":%u,", (unsigned)tof->boot_state);
    (void)printf("\"rangeStatus\":%u,", (unsigned)tof->range_status);
    (void)printf("\"sensorId\":%u,", (unsigned)tof->sensor_id);
    (void)printf("\"distanceMm\":%u,", (unsigned)tof->distance_mm);
    (void)printf("\"lastErrorCode\":%ld,", (long)tof->last_error);
    fputs("\"lastError\":", stdout);
    laser_controller_comms_write_error_name_json(tof->last_error);
    fputs("}", stdout);

    fputs("},", stdout);
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

static void laser_controller_comms_write_snapshot_json(
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

    putchar('{');
    fputs("\"identity\":{", stdout);
    fputs("\"label\":", stdout);
    laser_controller_comms_write_escaped_string(
        signature != NULL ? signature->product_name : LASER_CONTROLLER_FIRMWARE_PRODUCT_NAME);
    putchar(',');
    fputs("\"firmwareVersion\":", stdout);
    laser_controller_comms_write_escaped_string(
        signature != NULL ? signature->firmware_version : app_desc->version);
    fputs(",\"hardwareRevision\":\"rev-", stdout);
    (void)printf("%lu", (unsigned long)status->config.hardware_revision);
    fputs("\",\"serialNumber\":", stdout);
    laser_controller_comms_write_escaped_string(status->config.serial_number);
    fputs(",\"protocolVersion\":", stdout);
    laser_controller_comms_write_escaped_string(
        signature != NULL ? signature->protocol_version :
                            LASER_CONTROLLER_FIRMWARE_PROTOCOL_VERSION);
    fputs(",\"boardName\":", stdout);
    laser_controller_comms_write_escaped_string(
        signature != NULL ? signature->board_name : "unknown");
    fputs(",\"buildUtc\":", stdout);
    laser_controller_comms_write_escaped_string(
        signature != NULL ? signature->build_utc : "unknown");
    fputs("},", stdout);

    fputs("\"session\":{", stdout);
    (void)printf("\"uptimeSeconds\":%lu,", (unsigned long)(status->uptime_ms / 1000U));
    fputs("\"state\":", stdout);
    laser_controller_comms_write_escaped_string(
        laser_controller_state_name(status->state));
    fputs(",\"powerTier\":", stdout);
    laser_controller_comms_write_escaped_string(
        laser_controller_comms_power_tier_name(status->power_tier));
    fputs(",\"bootReason\":", stdout);
    laser_controller_comms_write_escaped_string(
        laser_controller_comms_reset_reason_name(reset_reason));
    fputs(",\"connectedAtIso\":\"1970-01-01T00:00:00Z\"},", stdout);

    fputs("\"pd\":{", stdout);
    (void)printf("\"contractValid\":%s,", status->inputs.pd_contract_valid ? "true" : "false");
    (void)printf("\"negotiatedPowerW\":%.2f,", status->inputs.pd_negotiated_power_w);
    (void)printf("\"sourceVoltageV\":%.2f,", status->inputs.pd_source_voltage_v);
    (void)printf("\"sourceCurrentA\":%.2f,", status->inputs.pd_source_current_a);
    (void)printf("\"operatingCurrentA\":%.2f,", status->inputs.pd_operating_current_a);
    (void)printf("\"contractObjectPosition\":%u,", (unsigned int)status->inputs.pd_contract_object_position);
    (void)printf("\"sinkProfileCount\":%u,", (unsigned int)status->inputs.pd_sink_profile_count);
    fputs("\"sinkProfiles\":[", stdout);
    for (size_t index = 0U;
         index < LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT;
         ++index) {
        if (index > 0U) {
            putchar(',');
        }
        laser_controller_comms_write_pd_profile_json(&status->inputs.pd_sink_profiles[index]);
    }
    (void)printf("],\"sourceIsHostOnly\":%s},",
                  status->inputs.pd_source_is_host_only ? "true" : "false");

    fputs("\"rails\":{", stdout);
    (void)printf("\"ld\":{\"enabled\":%s,\"pgood\":%s},",
                  status->outputs.enable_ld_vin ? "true" : "false",
                  status->inputs.ld_rail_pgood ? "true" : "false");
    (void)printf("\"tec\":{\"enabled\":%s,\"pgood\":%s}},",
                  status->outputs.enable_tec_vin ? "true" : "false",
                  status->inputs.tec_rail_pgood ? "true" : "false");

    fputs("\"imu\":{", stdout);
    (void)printf("\"valid\":%s,", status->inputs.imu_data_valid ? "true" : "false");
    (void)printf("\"fresh\":%s,", status->inputs.imu_data_fresh ? "true" : "false");
    (void)printf("\"beamPitchDeg\":%.2f,", beam_pitch_deg);
    (void)printf("\"beamRollDeg\":%.2f,", beam_roll_deg);
    (void)printf("\"beamYawDeg\":%.2f,", beam_yaw_deg);
    fputs("\"beamYawRelative\":true,", stdout);
    (void)printf("\"beamPitchLimitDeg\":%.2f},", beam_pitch_limit_deg);

    fputs("\"tof\":{", stdout);
    (void)printf("\"valid\":%s,", status->inputs.tof_data_valid ? "true" : "false");
    (void)printf("\"fresh\":%s,", status->inputs.tof_data_fresh ? "true" : "false");
    (void)printf("\"distanceM\":%.3f,", status->inputs.tof_distance_m);
    (void)printf("\"minRangeM\":%.3f,", status->config.thresholds.tof_min_range_m);
    (void)printf("\"maxRangeM\":%.3f},", status->config.thresholds.tof_max_range_m);

    fputs("\"laser\":{", stdout);
    (void)printf("\"alignmentEnabled\":%s,", status->outputs.enable_alignment_laser ? "true" : "false");
    (void)printf("\"nirEnabled\":%s,", status->decision.nir_output_enable ? "true" : "false");
    (void)printf("\"driverStandby\":%s,", status->outputs.assert_driver_standby ? "true" : "false");
    (void)printf("\"measuredCurrentA\":%.3f,", status->inputs.measured_laser_current_a);
    (void)printf("\"commandedCurrentA\":%.3f,", status->bench.high_state_current_a);
    (void)printf("\"loopGood\":%s,", status->inputs.driver_loop_good ? "true" : "false");
    (void)printf("\"driverTempC\":%.2f},", status->inputs.laser_driver_temp_c);

    fputs("\"tec\":{", stdout);
    (void)printf("\"targetTempC\":%.2f,", status->bench.target_temp_c);
    (void)printf("\"targetLambdaNm\":%.2f,", status->bench.target_lambda_nm);
    (void)printf("\"actualLambdaNm\":%.2f,", actual_lambda_nm);
    (void)printf("\"tempGood\":%s,", status->inputs.tec_temp_good ? "true" : "false");
    (void)printf("\"tempC\":%.2f,", status->inputs.tec_temp_c);
    (void)printf("\"tempAdcVoltageV\":%.3f,", status->inputs.tec_temp_adc_voltage_v);
    (void)printf("\"currentA\":%.2f,", status->inputs.tec_current_a);
    (void)printf("\"voltageV\":%.2f,", status->inputs.tec_voltage_v);
    (void)printf("\"settlingSecondsRemaining\":%u},",
                  status->inputs.tec_temp_good ? 0U : 1U);

    laser_controller_comms_write_peripheral_readback_json(status);

    fputs("\"bench\":{", stdout);
    fputs("\"targetMode\":", stdout);
    laser_controller_comms_write_escaped_string(
        laser_controller_bench_target_mode_name(status->bench.target_mode));
    (void)printf(",\"requestedNirEnabled\":%s,", status->bench.requested_nir ? "true" : "false");
    (void)printf("\"modulationEnabled\":%s,", status->bench.modulation_enabled ? "true" : "false");
    (void)printf("\"modulationFrequencyHz\":%lu,", (unsigned long)status->bench.modulation_frequency_hz);
    (void)printf("\"modulationDutyCyclePct\":%lu,", (unsigned long)status->bench.modulation_duty_cycle_pct);
    (void)printf("\"lowStateCurrentA\":%.3f},", status->bench.low_state_current_a);

    fputs("\"safety\":{", stdout);
    (void)printf("\"allowAlignment\":%s,", status->decision.allow_alignment ? "true" : "false");
    (void)printf("\"allowNir\":%s,", status->decision.allow_nir ? "true" : "false");
    (void)printf("\"horizonBlocked\":%s,", status->decision.horizon_blocked ? "true" : "false");
    (void)printf("\"distanceBlocked\":%s,", status->decision.distance_blocked ? "true" : "false");
    (void)printf("\"lambdaDriftBlocked\":%s,", status->decision.lambda_drift_blocked ? "true" : "false");
    (void)printf("\"tecTempAdcBlocked\":%s,", status->decision.tec_temp_adc_blocked ? "true" : "false");
    (void)printf("\"horizonThresholdDeg\":%.2f,", beam_pitch_limit_deg);
    (void)printf("\"horizonHysteresisDeg\":%.2f,", beam_pitch_hysteresis_deg);
    (void)printf("\"tofMinRangeM\":%.3f,", status->config.thresholds.tof_min_range_m);
    (void)printf("\"tofMaxRangeM\":%.3f,", status->config.thresholds.tof_max_range_m);
    (void)printf("\"tofHysteresisM\":%.3f,", status->config.thresholds.tof_hysteresis_m);
    (void)printf("\"imuStaleMs\":%lu,", (unsigned long)status->config.timeouts.imu_stale_ms);
    (void)printf("\"tofStaleMs\":%lu,", (unsigned long)status->config.timeouts.tof_stale_ms);
    (void)printf("\"railGoodTimeoutMs\":%lu,", (unsigned long)status->config.timeouts.rail_good_timeout_ms);
    (void)printf("\"lambdaDriftLimitNm\":%.2f,", status->config.thresholds.lambda_drift_limit_nm);
    (void)printf("\"lambdaDriftHysteresisNm\":%.2f,", status->config.thresholds.lambda_drift_hysteresis_nm);
    (void)printf("\"lambdaDriftHoldMs\":%lu,", (unsigned long)status->config.timeouts.lambda_drift_hold_ms);
    (void)printf("\"ldOvertempLimitC\":%.2f,", status->config.thresholds.ld_overtemp_limit_c);
    (void)printf("\"tecTempAdcTripV\":%.3f,", status->config.thresholds.tec_temp_adc_trip_v);
    (void)printf("\"tecTempAdcHysteresisV\":%.3f,", status->config.thresholds.tec_temp_adc_hysteresis_v);
    (void)printf("\"tecTempAdcHoldMs\":%lu,", (unsigned long)status->config.timeouts.tec_temp_adc_hold_ms);
    (void)printf("\"tecMinCommandC\":%.2f,", status->config.thresholds.tec_min_command_c);
    (void)printf("\"tecMaxCommandC\":%.2f,", status->config.thresholds.tec_max_command_c);
    (void)printf("\"tecReadyToleranceC\":%.2f,", status->config.thresholds.tec_ready_tolerance_c);
    (void)printf("\"maxLaserCurrentA\":%.2f,", status->config.thresholds.max_laser_current_a);
    (void)printf("\"actualLambdaNm\":%.2f,", actual_lambda_nm);
    (void)printf("\"targetLambdaNm\":%.2f,", status->bench.target_lambda_nm);
    (void)printf("\"lambdaDriftNm\":%.2f,", lambda_drift_nm);
    (void)printf("\"tempAdcVoltageV\":%.3f},", status->inputs.tec_temp_adc_voltage_v);

    fputs("\"bringup\":{", stdout);
    (void)printf("\"serviceModeRequested\":%s,", status->bringup.service_mode_requested ? "true" : "false");
    (void)printf("\"serviceModeActive\":%s,", status->state == LASER_CONTROLLER_STATE_SERVICE_MODE ? "true" : "false");
    (void)printf("\"persistenceDirty\":%s,", status->bringup.persistence_dirty ? "true" : "false");
    (void)printf("\"persistenceAvailable\":%s,", status->bringup.persistence_available ? "true" : "false");
    (void)printf("\"lastSaveOk\":%s,", status->bringup.last_save_ok ? "true" : "false");
    (void)printf("\"profileRevision\":%lu,", (unsigned long)status->bringup.profile_revision);
    fputs("\"profileName\":", stdout);
    laser_controller_comms_write_escaped_string(status->bringup.profile_name);
    fputs(",\"modules\":{", stdout);
    fputs("\"imu\":", stdout);
    laser_controller_comms_write_module_json(
        &status->bringup.modules[LASER_CONTROLLER_MODULE_IMU]);
    fputs(",\"dac\":", stdout);
    laser_controller_comms_write_module_json(
        &status->bringup.modules[LASER_CONTROLLER_MODULE_DAC]);
    fputs(",\"haptic\":", stdout);
    laser_controller_comms_write_module_json(
        &status->bringup.modules[LASER_CONTROLLER_MODULE_HAPTIC]);
    fputs(",\"tof\":", stdout);
    laser_controller_comms_write_module_json(
        &status->bringup.modules[LASER_CONTROLLER_MODULE_TOF]);
    fputs(",\"buttons\":", stdout);
    laser_controller_comms_write_module_json(
        &status->bringup.modules[LASER_CONTROLLER_MODULE_BUTTONS]);
    fputs(",\"pd\":", stdout);
    laser_controller_comms_write_module_json(
        &status->bringup.modules[LASER_CONTROLLER_MODULE_PD]);
    fputs(",\"laserDriver\":", stdout);
    laser_controller_comms_write_module_json(
        &status->bringup.modules[LASER_CONTROLLER_MODULE_LASER_DRIVER]);
    fputs(",\"tec\":", stdout);
    laser_controller_comms_write_module_json(
        &status->bringup.modules[LASER_CONTROLLER_MODULE_TEC]);
    fputs("},\"tuning\":{", stdout);
    (void)printf("\"dacLdChannelV\":%.3f,", status->bringup.dac_ld_channel_v);
    (void)printf("\"dacTecChannelV\":%.3f,", status->bringup.dac_tec_channel_v);
    fputs("\"dacReferenceMode\":", stdout);
    laser_controller_comms_write_escaped_string(
        laser_controller_service_dac_reference_name(status->bringup.dac_reference));
    (void)printf(",\"dacGain2x\":%s,", status->bringup.dac_gain_2x ? "true" : "false");
    (void)printf("\"dacRefDiv\":%s,", status->bringup.dac_ref_div ? "true" : "false");
    fputs("\"dacSyncMode\":", stdout);
    laser_controller_comms_write_escaped_string(
        laser_controller_service_dac_sync_mode_name(status->bringup.dac_sync_mode));
    putchar(',');
    (void)printf("\"imuOdrHz\":%lu,", (unsigned long)status->bringup.imu_odr_hz);
    (void)printf("\"imuAccelRangeG\":%lu,", (unsigned long)status->bringup.imu_accel_range_g);
    (void)printf("\"imuGyroRangeDps\":%lu,", (unsigned long)status->bringup.imu_gyro_range_dps);
    (void)printf("\"imuGyroEnabled\":%s,", status->bringup.imu_gyro_enabled ? "true" : "false");
    (void)printf("\"imuLpf2Enabled\":%s,", status->bringup.imu_lpf2_enabled ? "true" : "false");
    (void)printf("\"imuTimestampEnabled\":%s,", status->bringup.imu_timestamp_enabled ? "true" : "false");
    (void)printf("\"imuBduEnabled\":%s,", status->bringup.imu_bdu_enabled ? "true" : "false");
    (void)printf("\"imuIfIncEnabled\":%s,", status->bringup.imu_if_inc_enabled ? "true" : "false");
    (void)printf("\"imuI2cDisabled\":%s,", status->bringup.imu_i2c_disabled ? "true" : "false");
    (void)printf("\"tofMinRangeM\":%.3f,", status->bringup.tof_min_range_m);
    (void)printf("\"tofMaxRangeM\":%.3f,", status->bringup.tof_max_range_m);
    (void)printf("\"tofStaleTimeoutMs\":%lu,", (unsigned long)status->bringup.tof_stale_timeout_ms);
    fputs("\"pdProfiles\":[", stdout);
    for (size_t index = 0U;
         index < LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT;
         ++index) {
        if (index > 0U) {
            putchar(',');
        }
        laser_controller_comms_write_pd_profile_json(
            &status->bringup.pd_profiles[index]);
    }
    (void)printf(
        "],\"pdProgrammingOnlyMaxW\":%.2f,",
        status->bringup.pd_programming_only_max_w);
    (void)printf(
        "\"pdReducedModeMinW\":%.2f,",
        status->bringup.pd_reduced_mode_min_w);
    (void)printf(
        "\"pdReducedModeMaxW\":%.2f,",
        status->bringup.pd_reduced_mode_max_w);
    (void)printf(
        "\"pdFullModeMinW\":%.2f,",
        status->bringup.pd_full_mode_min_w);
    (void)printf(
        "\"pdFirmwarePlanEnabled\":%s,",
        status->bringup.pd_firmware_plan_enabled ? "true" : "false");
    (void)printf("\"hapticEffectId\":%lu,", (unsigned long)status->bringup.haptic_effect_id);
    fputs("\"hapticMode\":", stdout);
    laser_controller_comms_write_escaped_string(
        laser_controller_service_haptic_mode_name(status->bringup.haptic_mode));
    putchar(',');
    (void)printf("\"hapticLibrary\":%lu,", (unsigned long)status->bringup.haptic_library);
    fputs("\"hapticActuator\":", stdout);
    laser_controller_comms_write_escaped_string(
        laser_controller_service_haptic_actuator_name(status->bringup.haptic_actuator));
    putchar(',');
    (void)printf("\"hapticRtpLevel\":%lu", (unsigned long)status->bringup.haptic_rtp_level);
    fputs("},\"tools\":{", stdout);
    fputs("\"lastI2cScan\":", stdout);
    laser_controller_comms_write_escaped_string(status->bringup.last_i2c_scan);
    fputs(",\"lastI2cOp\":", stdout);
    laser_controller_comms_write_escaped_string(status->bringup.last_i2c_op);
    fputs(",\"lastSpiOp\":", stdout);
    laser_controller_comms_write_escaped_string(status->bringup.last_spi_op);
    fputs(",\"lastAction\":", stdout);
    laser_controller_comms_write_escaped_string(status->bringup.last_action);
    fputs("}},", stdout);

    fputs("\"fault\":{", stdout);
    (void)printf("\"latched\":%s,", status->fault_latched ? "true" : "false");
    fputs("\"activeCode\":", stdout);
    laser_controller_comms_write_escaped_string(
        laser_controller_fault_code_name(status->active_fault_code));
    (void)printf(",\"activeCount\":%lu,", (unsigned long)status->active_fault_count);
    (void)printf("\"tripCounter\":%lu,", (unsigned long)status->trip_counter);
    if (status->last_fault_ms == 0U) {
        fputs("\"lastFaultAtIso\":null},", stdout);
    } else {
        fputs("\"lastFaultAtIso\":\"1970-01-01T00:00:00Z\"},", stdout);
    }

    fputs("\"counters\":{", stdout);
    (void)printf("\"commsTimeouts\":0,");
    (void)printf("\"watchdogTrips\":%u,", reset_reason == ESP_RST_TASK_WDT ||
                  reset_reason == ESP_RST_INT_WDT || reset_reason == ESP_RST_WDT ? 1U : 0U);
    (void)printf("\"brownouts\":%u}", reset_reason == ESP_RST_BROWNOUT ? 1U : 0U);
    putchar('}');
}

static void laser_controller_comms_emit_log_event(
    const laser_controller_log_entry_t *entry)
{
    laser_controller_comms_output_lock();
    fputs("{\"type\":\"event\",\"event\":\"log\",\"timestamp_ms\":", stdout);
    (void)printf("%lu", (unsigned long)entry->timestamp_ms);
    fputs(",\"detail\":", stdout);
    laser_controller_comms_write_escaped_string(entry->message);
    fputs(",\"payload\":{\"category\":", stdout);
    laser_controller_comms_write_escaped_string(entry->category);
    fputs(",\"message\":", stdout);
    laser_controller_comms_write_escaped_string(entry->message);
    fputs("}}\n", stdout);
    fflush(stdout);
    laser_controller_comms_output_unlock();
}

static void laser_controller_comms_emit_status_event(
    const laser_controller_runtime_status_t *status)
{
    laser_controller_comms_output_lock();
    fputs("{\"type\":\"event\",\"event\":\"status_snapshot\",\"timestamp_ms\":", stdout);
    (void)printf("%lu", (unsigned long)status->uptime_ms);
    fputs(",\"detail\":\"Periodic controller snapshot.\",\"payload\":", stdout);
    laser_controller_comms_write_snapshot_json(status);
    fputs("}\n", stdout);
    fflush(stdout);
    laser_controller_comms_output_unlock();
}

static void laser_controller_comms_emit_error_response(
    uint32_t id,
    const char *message)
{
    laser_controller_comms_output_lock();
    fputs("{\"type\":\"resp\",\"id\":", stdout);
    (void)printf("%lu", (unsigned long)id);
    fputs(",\"ok\":false,\"error\":", stdout);
    laser_controller_comms_write_escaped_string(message);
    fputs("}\n", stdout);
    fflush(stdout);
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

static void laser_controller_comms_emit_status_response(
    uint32_t id,
    const laser_controller_runtime_status_t *status)
{
    laser_controller_comms_output_lock();
    fputs("{\"type\":\"resp\",\"id\":", stdout);
    (void)printf("%lu", (unsigned long)id);
    fputs(",\"ok\":true,\"result\":", stdout);
    laser_controller_comms_write_snapshot_json(status);
    fputs("}\n", stdout);
    fflush(stdout);
    laser_controller_comms_output_unlock();
}

static void laser_controller_comms_emit_faults_response(
    uint32_t id,
    const laser_controller_runtime_status_t *status)
{
    laser_controller_comms_output_lock();
    fputs("{\"type\":\"resp\",\"id\":", stdout);
    (void)printf("%lu", (unsigned long)id);
    fputs(",\"ok\":true,\"result\":{", stdout);
    (void)printf("\"latched\":%s,", status->fault_latched ? "true" : "false");
    fputs("\"activeCode\":", stdout);
    laser_controller_comms_write_escaped_string(
        laser_controller_fault_code_name(status->active_fault_code));
    (void)printf(",\"activeCount\":%lu,", (unsigned long)status->active_fault_count);
    (void)printf("\"tripCounter\":%lu", (unsigned long)status->trip_counter);
    fputs("}}\n", stdout);
    fflush(stdout);
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

    if (strcmp(command, "get_status") == 0) {
        laser_controller_comms_emit_status_response(id, &status);
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
        laser_controller_comms_emit_status_response(id, &status);
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
        float value_f = 0.0f;
        esp_err_t pd_err = ESP_OK;

        laser_controller_logger_log(
            now_ms,
            "service",
            "pd debug config command received");
        memcpy(
            profiles,
            status.bringup.pd_profiles,
            sizeof(profiles));

        if (laser_controller_comms_extract_float(
                line,
                "\"programming_only_max_w\":",
                &value_f)) {
            power_policy.programming_only_max_w = value_f;
        }
        if (laser_controller_comms_extract_float(
                line,
                "\"reduced_mode_min_w\":",
                &value_f)) {
            power_policy.reduced_mode_min_w = value_f;
        }
        if (laser_controller_comms_extract_float(
                line,
                "\"reduced_mode_max_w\":",
                &value_f)) {
            power_policy.reduced_mode_max_w = value_f;
        }
        if (laser_controller_comms_extract_float(
                line,
                "\"full_mode_min_w\":",
                &value_f)) {
            power_policy.full_mode_min_w = value_f;
        }

        for (size_t index = 0U;
             index < LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT;
             ++index) {
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
        float value_f = 0.0f;
        bool firmware_plan_enabled = true;
        esp_err_t pd_err = ESP_OK;

        memcpy(
            profiles,
            status.bringup.pd_profiles,
            sizeof(profiles));
        (void)laser_controller_comms_extract_bool(
            line,
            "\"firmware_plan_enabled\":",
            &firmware_plan_enabled);

        if (laser_controller_comms_extract_float(
                line,
                "\"programming_only_max_w\":",
                &value_f)) {
            power_policy.programming_only_max_w = value_f;
        }
        if (laser_controller_comms_extract_float(
                line,
                "\"reduced_mode_min_w\":",
                &value_f)) {
            power_policy.reduced_mode_min_w = value_f;
        }
        if (laser_controller_comms_extract_float(
                line,
                "\"reduced_mode_max_w\":",
                &value_f)) {
            power_policy.reduced_mode_max_w = value_f;
        }
        if (laser_controller_comms_extract_float(
                line,
                "\"full_mode_min_w\":",
                &value_f)) {
            power_policy.full_mode_min_w = value_f;
        }

        for (size_t index = 0U;
             index < LASER_CONTROLLER_SERVICE_PD_PROFILE_COUNT;
             ++index) {
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
        laser_controller_logger_log(
            now_ms,
            "service",
            "pd NVM burn requested");
        laser_controller_comms_emit_error_response(
            id,
            "STUSB4500 NVM programming is not implemented in this bench image. Validate PDOs with runtime apply first, then use a dedicated manufacturing flow.");
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

static void laser_controller_comms_tx_task(void *argument)
{
    size_t last_log_count = 0U;

    (void)argument;

    for (;;) {
        laser_controller_runtime_status_t status;
        const size_t total_log_count = laser_controller_logger_total_count();

        if (laser_controller_app_copy_status(&status)) {
            laser_controller_comms_emit_status_event(&status);
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

        laser_controller_comms_handle_command_line(line);
    }
}
#endif

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
            laser_controller_comms_handle_command_line(line);
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

void laser_controller_comms_start(void)
{
    usb_serial_jtag_driver_config_t usb_config =
        USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    TaskHandle_t tx_handle = NULL;
    TaskHandle_t usb_rx_handle = NULL;
#if !CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    TaskHandle_t rx_handle = NULL;
#endif

    if (s_comms_started) {
        return;
    }

    if (s_output_lock == NULL) {
        s_output_lock = xSemaphoreCreateMutexStatic(&s_output_lock_buffer);
    }

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

    tx_handle = xTaskCreateStaticPinnedToCore(
        laser_controller_comms_tx_task,
        "laser_tx",
        LASER_CONTROLLER_COMMS_TX_STACK_BYTES,
        NULL,
        LASER_CONTROLLER_COMMS_TX_PRIORITY,
        s_tx_task_stack,
        &s_tx_task_tcb,
        tskNO_AFFINITY);

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    usb_rx_handle = xTaskCreateStaticPinnedToCore(
        laser_controller_comms_usb_rx_task,
        "laser_usb_rx",
        LASER_CONTROLLER_COMMS_RX_STACK_BYTES,
        NULL,
        LASER_CONTROLLER_COMMS_RX_PRIORITY,
        s_usb_rx_task_stack,
        &s_usb_rx_task_tcb,
        tskNO_AFFINITY);
#else
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
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
        usb_rx_handle != NULL
#else
        rx_handle != NULL
#endif
    ) {
        s_comms_started = true;
    }
}
