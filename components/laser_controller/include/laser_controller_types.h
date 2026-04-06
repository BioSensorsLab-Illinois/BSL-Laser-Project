#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef uint32_t laser_controller_time_ms_t;
typedef float laser_controller_celsius_t;
typedef float laser_controller_volts_t;
typedef float laser_controller_amps_t;
typedef float laser_controller_distance_m_t;
typedef float laser_controller_radians_t;
typedef float laser_controller_nm_t;

typedef enum {
    LASER_CONTROLLER_RUNTIME_MODE_BINARY_TRIGGER = 0,
    LASER_CONTROLLER_RUNTIME_MODE_MODULATED_HOST,
} laser_controller_runtime_mode_t;

typedef enum {
    LASER_CONTROLLER_POWER_TIER_UNKNOWN = 0,
    LASER_CONTROLLER_POWER_TIER_PROGRAMMING_ONLY,
    LASER_CONTROLLER_POWER_TIER_INSUFFICIENT,
    LASER_CONTROLLER_POWER_TIER_REDUCED,
    LASER_CONTROLLER_POWER_TIER_FULL,
} laser_controller_power_tier_t;

typedef struct {
    bool stage1_pressed;
    bool stage2_pressed;
    bool stage1_edge;
    bool stage2_edge;
} laser_controller_button_state_t;
