#include "laser_controller_faults.h"

const char *laser_controller_fault_code_name(laser_controller_fault_code_t code)
{
    switch (code) {
        case LASER_CONTROLLER_FAULT_NONE:
            return "none";
        case LASER_CONTROLLER_FAULT_INVALID_CONFIG:
            return "invalid_config";
        case LASER_CONTROLLER_FAULT_NVS_CRC:
            return "nvs_crc";
        case LASER_CONTROLLER_FAULT_WATCHDOG_RESET:
            return "watchdog_reset";
        case LASER_CONTROLLER_FAULT_BROWNOUT_RESET:
            return "brownout_reset";
        case LASER_CONTROLLER_FAULT_PD_LOST:
            return "pd_lost";
        case LASER_CONTROLLER_FAULT_PD_INSUFFICIENT:
            return "pd_insufficient";
        case LASER_CONTROLLER_FAULT_LD_RAIL_BAD:
            return "ld_rail_bad";
        case LASER_CONTROLLER_FAULT_TEC_RAIL_BAD:
            return "tec_rail_bad";
        case LASER_CONTROLLER_FAULT_IMU_STALE:
            return "imu_stale";
        case LASER_CONTROLLER_FAULT_IMU_INVALID:
            return "imu_invalid";
        case LASER_CONTROLLER_FAULT_HORIZON_CROSSED:
            return "horizon_crossed";
        case LASER_CONTROLLER_FAULT_TOF_STALE:
            return "tof_stale";
        case LASER_CONTROLLER_FAULT_TOF_INVALID:
            return "tof_invalid";
        case LASER_CONTROLLER_FAULT_TOF_OUT_OF_RANGE:
            return "tof_out_of_range";
        case LASER_CONTROLLER_FAULT_LD_OVERTEMP:
            return "ld_overtemp";
        case LASER_CONTROLLER_FAULT_LAMBDA_DRIFT:
            return "lambda_drift";
        case LASER_CONTROLLER_FAULT_TEC_TEMP_ADC_HIGH:
            return "tec_temp_adc_high";
        case LASER_CONTROLLER_FAULT_LD_LOOP_BAD:
            return "ld_loop_bad";
        case LASER_CONTROLLER_FAULT_UNEXPECTED_CURRENT:
            return "unexpected_current";
        case LASER_CONTROLLER_FAULT_CURRENT_MISMATCH:
            return "current_mismatch";
        case LASER_CONTROLLER_FAULT_TEC_NOT_SETTLED:
            return "tec_not_settled";
        case LASER_CONTROLLER_FAULT_TEC_IMPLAUSIBLE:
            return "tec_implausible";
        case LASER_CONTROLLER_FAULT_ILLEGAL_BUTTON_STATE:
            return "illegal_button_state";
        case LASER_CONTROLLER_FAULT_UNEXPECTED_STATE:
            return "unexpected_state";
        case LASER_CONTROLLER_FAULT_COMMS_TIMEOUT:
            return "comms_timeout";
        case LASER_CONTROLLER_FAULT_SERVICE_OVERRIDE_REJECTED:
            return "service_override_rejected";
        default:
            return "unknown_fault";
    }
}

const char *laser_controller_fault_class_name(laser_controller_fault_class_t fault_class)
{
    switch (fault_class) {
        case LASER_CONTROLLER_FAULT_CLASS_NONE:
            return "none";
        case LASER_CONTROLLER_FAULT_CLASS_INTERLOCK_AUTO_CLEAR:
            return "interlock_auto_clear";
        case LASER_CONTROLLER_FAULT_CLASS_SAFETY_LATCHED:
            return "safety_latched";
        case LASER_CONTROLLER_FAULT_CLASS_SYSTEM_MAJOR:
            return "system_major";
        default:
            return "unknown_class";
    }
}
