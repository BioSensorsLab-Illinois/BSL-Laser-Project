#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "laser_controller_types.h"

typedef enum {
    LASER_CONTROLLER_FAULT_CLASS_NONE = 0,
    LASER_CONTROLLER_FAULT_CLASS_INTERLOCK_AUTO_CLEAR,
    LASER_CONTROLLER_FAULT_CLASS_SAFETY_LATCHED,
    LASER_CONTROLLER_FAULT_CLASS_SYSTEM_MAJOR,
} laser_controller_fault_class_t;

typedef enum {
    LASER_CONTROLLER_FAULT_NONE = 0,
    LASER_CONTROLLER_FAULT_INVALID_CONFIG,
    LASER_CONTROLLER_FAULT_NVS_CRC,
    LASER_CONTROLLER_FAULT_WATCHDOG_RESET,
    LASER_CONTROLLER_FAULT_BROWNOUT_RESET,
    LASER_CONTROLLER_FAULT_PD_LOST,
    LASER_CONTROLLER_FAULT_PD_INSUFFICIENT,
    LASER_CONTROLLER_FAULT_LD_RAIL_BAD,
    LASER_CONTROLLER_FAULT_TEC_RAIL_BAD,
    LASER_CONTROLLER_FAULT_IMU_STALE,
    LASER_CONTROLLER_FAULT_IMU_INVALID,
    LASER_CONTROLLER_FAULT_HORIZON_CROSSED,
    LASER_CONTROLLER_FAULT_TOF_STALE,
    LASER_CONTROLLER_FAULT_TOF_INVALID,
    LASER_CONTROLLER_FAULT_TOF_OUT_OF_RANGE,
    LASER_CONTROLLER_FAULT_LD_OVERTEMP,
    LASER_CONTROLLER_FAULT_LAMBDA_DRIFT,
    LASER_CONTROLLER_FAULT_TEC_TEMP_ADC_HIGH,
    LASER_CONTROLLER_FAULT_LD_LOOP_BAD,
    LASER_CONTROLLER_FAULT_UNEXPECTED_CURRENT,
    LASER_CONTROLLER_FAULT_CURRENT_MISMATCH,
    LASER_CONTROLLER_FAULT_TEC_NOT_SETTLED,
    LASER_CONTROLLER_FAULT_TEC_IMPLAUSIBLE,
    LASER_CONTROLLER_FAULT_ILLEGAL_BUTTON_STATE,
    LASER_CONTROLLER_FAULT_UNEXPECTED_STATE,
    LASER_CONTROLLER_FAULT_COMMS_TIMEOUT,
    LASER_CONTROLLER_FAULT_SERVICE_OVERRIDE_REJECTED,
    /*
     * Latched when the USB-Debug Mock Layer was active and real PD power
     * (any tier above programming_only) was detected. The mock auto-disables
     * itself in the same tick. Class is SYSTEM_MAJOR — collapses rails
     * safe-off via the standard SYSTEM_MAJOR override paths in safety.c
     * and app.c. Operator must explicitly clear-faults before re-enabling
     * the mock; the clear-faults path also calls
     * `laser_controller_usb_debug_mock_clear_pd_conflict_latch` which
     * routes through an atomic request flag (control task remains sole
     * writer of the latch).
     */
    LASER_CONTROLLER_FAULT_USB_DEBUG_MOCK_PD_CONFLICT,
    /*
     * Latched when the ATLS6A214 SBDN pin has been driven HIGH (OPERATE
     * posture) but LD_LPGD (GPIO14, LP_GOOD) has not asserted within 1 s.
     * This is the post-SBDN loop-lock timeout check added to the deployment
     * checklist on 2026-04-15. Class is SYSTEM_MAJOR because the driver's
     * control loop never closed — any attempt to pass current through it
     * would be unsupervised. Operator must clear faults explicitly and the
     * deployment is aborted with this as the primary failure code.
     */
    LASER_CONTROLLER_FAULT_LD_LP_GOOD_TIMEOUT,
    /*
     * Latched when the button-board MCP23017 (@0x20) or TLC59116 (@0x60) is
     * not reachable on the shared I2C bus after the configured retry window
     * at boot or has failed N consecutive reads during runtime. Class is
     * SYSTEM_MAJOR because losing the button board during an active
     * binary-trigger runtime session leaves the physical trigger path
     * unobservable by firmware — no way to detect release, no way to read
     * stage2. The fault collapses rails safe via the normal SYSTEM_MAJOR
     * path and the RGB status LED is driven to flash-red (unrecoverable).
     */
    LASER_CONTROLLER_FAULT_BUTTON_BOARD_I2C_LOST,
} laser_controller_fault_code_t;

typedef struct {
    bool active;
    uint32_t counter;
    laser_controller_time_ms_t last_timestamp_ms;
    laser_controller_fault_code_t code;
    laser_controller_fault_class_t fault_class;
} laser_controller_fault_record_t;

const char *laser_controller_fault_code_name(laser_controller_fault_code_t code);
const char *laser_controller_fault_class_name(laser_controller_fault_class_t fault_class);
