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
    /*
     * Two-stage trigger buttons on the main button — stage1 is the shallow
     * press, stage2 is the deeper press. Hardware guarantees stage2 implies
     * stage1 (stage2 is mechanically downstream of stage1), so stage2 alone
     * is an illegal wiring fault — see safety.c ILLEGAL_BUTTON_STATE.
     */
    bool stage1_pressed;
    bool stage2_pressed;
    bool stage1_edge;
    bool stage2_edge;
    /*
     * Two independent side buttons on the button board (GPA2 / GPA3 on the
     * MCP23017 expander). Used by the firmware control task to adjust the
     * front LED-board brightness in +/- 10% steps during deployment-mode
     * binary-trigger operation. No safety interlock on side buttons.
     */
    bool side1_pressed;
    bool side2_pressed;
    bool side1_edge;
    bool side2_edge;
    /*
     * `board_reachable` is TRUE when the MCP23017 expander has been probed
     * and configured successfully. When FALSE, all button bits above are
     * held inactive and the firmware falls back to host-request-only
     * behavior. The bench status publishes this so the host can pre-disable
     * the binary-trigger runtime mode when the button board is absent.
     */
    bool board_reachable;
    /*
     * Monotonic count of GPIO7/INTA ISR fires since boot. Published via
     * telemetry so the host Integrate panel can verify the interrupt line
     * is working without having to physically press a button.
     */
    uint32_t isr_fire_count;
} laser_controller_button_state_t;
