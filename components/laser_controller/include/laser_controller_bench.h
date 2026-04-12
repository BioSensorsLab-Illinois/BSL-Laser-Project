#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "laser_controller_config.h"
#include "laser_controller_types.h"

typedef enum {
    LASER_CONTROLLER_BENCH_TARGET_MODE_TEMP = 0,
    LASER_CONTROLLER_BENCH_TARGET_MODE_LAMBDA,
} laser_controller_bench_target_mode_t;

typedef struct {
    laser_controller_bench_target_mode_t target_mode;
    laser_controller_runtime_mode_t runtime_mode;
    bool requested_alignment;
    bool requested_nir;
    bool illumination_enabled;
    uint32_t illumination_duty_cycle_pct;
    uint32_t illumination_frequency_hz;
    bool modulation_enabled;
    uint32_t modulation_frequency_hz;
    uint32_t modulation_duty_cycle_pct;
    laser_controller_amps_t low_state_current_a;
    laser_controller_amps_t high_state_current_a;
    laser_controller_celsius_t target_temp_c;
    laser_controller_nm_t target_lambda_nm;
} laser_controller_bench_status_t;

void laser_controller_bench_init_defaults(void);
void laser_controller_bench_copy_status(laser_controller_bench_status_t *status);
void laser_controller_bench_clear_requests(laser_controller_time_ms_t now_ms);
void laser_controller_bench_set_alignment_requested(
    bool enable,
    laser_controller_time_ms_t now_ms);
void laser_controller_bench_set_nir_requested(
    bool enable,
    laser_controller_time_ms_t now_ms);
void laser_controller_bench_set_illumination(
    bool enable,
    uint32_t duty_cycle_pct,
    uint32_t frequency_hz,
    laser_controller_time_ms_t now_ms);
void laser_controller_bench_set_laser_current_a(
    const laser_controller_config_t *config,
    laser_controller_amps_t current_a,
    laser_controller_time_ms_t now_ms);
void laser_controller_bench_set_target_temp_c(
    const laser_controller_config_t *config,
    laser_controller_celsius_t target_temp_c,
    laser_controller_time_ms_t now_ms);
void laser_controller_bench_set_target_lambda_nm(
    const laser_controller_config_t *config,
    laser_controller_nm_t target_lambda_nm,
    laser_controller_time_ms_t now_ms);
void laser_controller_bench_set_modulation(
    bool enabled,
    uint32_t frequency_hz,
    uint32_t duty_cycle_pct,
    laser_controller_time_ms_t now_ms);
void laser_controller_bench_set_runtime_mode(
    laser_controller_runtime_mode_t runtime_mode,
    laser_controller_time_ms_t now_ms);
const char *laser_controller_bench_target_mode_name(
    laser_controller_bench_target_mode_t target_mode);
const char *laser_controller_runtime_mode_name(
    laser_controller_runtime_mode_t runtime_mode);
