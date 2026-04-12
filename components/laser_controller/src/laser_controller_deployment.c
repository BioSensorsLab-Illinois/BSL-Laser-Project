#include "laser_controller_deployment.h"

const char *laser_controller_deployment_target_mode_name(
    laser_controller_deployment_target_mode_t target_mode)
{
    switch (target_mode) {
        case LASER_CONTROLLER_DEPLOYMENT_TARGET_MODE_TEMP:
            return "temp";
        case LASER_CONTROLLER_DEPLOYMENT_TARGET_MODE_LAMBDA:
            return "lambda";
        default:
            return "temp";
    }
}

const char *laser_controller_deployment_phase_name(
    laser_controller_deployment_phase_t phase)
{
    switch (phase) {
        case LASER_CONTROLLER_DEPLOYMENT_PHASE_INACTIVE:
            return "inactive";
        case LASER_CONTROLLER_DEPLOYMENT_PHASE_ENTRY:
            return "entry";
        case LASER_CONTROLLER_DEPLOYMENT_PHASE_CHECKLIST:
            return "checklist";
        case LASER_CONTROLLER_DEPLOYMENT_PHASE_READY_IDLE:
            return "ready_idle";
        case LASER_CONTROLLER_DEPLOYMENT_PHASE_FAILED:
            return "failed";
        default:
            return "inactive";
    }
}

const char *laser_controller_deployment_step_name(
    laser_controller_deployment_step_t step)
{
    switch (step) {
        case LASER_CONTROLLER_DEPLOYMENT_STEP_NONE:
            return "none";
        case LASER_CONTROLLER_DEPLOYMENT_STEP_OWNERSHIP_RECLAIM:
            return "ownership_reclaim";
        case LASER_CONTROLLER_DEPLOYMENT_STEP_PD_INSPECT:
            return "pd_inspect";
        case LASER_CONTROLLER_DEPLOYMENT_STEP_POWER_CAP:
            return "power_cap";
        case LASER_CONTROLLER_DEPLOYMENT_STEP_OUTPUTS_OFF:
            return "outputs_off";
        case LASER_CONTROLLER_DEPLOYMENT_STEP_STABILIZE_3V3:
            return "stabilize_3v3";
        case LASER_CONTROLLER_DEPLOYMENT_STEP_DAC_SAFE_ZERO:
            return "dac_safe_zero";
        case LASER_CONTROLLER_DEPLOYMENT_STEP_PERIPHERALS_VERIFY:
            return "peripherals_verify";
        case LASER_CONTROLLER_DEPLOYMENT_STEP_RAIL_SEQUENCE:
            return "rail_sequence";
        case LASER_CONTROLLER_DEPLOYMENT_STEP_TEC_SETTLE:
            return "tec_settle";
        case LASER_CONTROLLER_DEPLOYMENT_STEP_READY_POSTURE:
            return "ready_posture";
        default:
            return "none";
    }
}

const char *laser_controller_deployment_step_label(
    laser_controller_deployment_step_t step)
{
    switch (step) {
        case LASER_CONTROLLER_DEPLOYMENT_STEP_NONE:
            return "Inactive";
        case LASER_CONTROLLER_DEPLOYMENT_STEP_OWNERSHIP_RECLAIM:
            return "Deployment entry / ownership reclaim";
        case LASER_CONTROLLER_DEPLOYMENT_STEP_PD_INSPECT:
            return "USB-PD inspect only";
        case LASER_CONTROLLER_DEPLOYMENT_STEP_POWER_CAP:
            return "Derive runtime power cap";
        case LASER_CONTROLLER_DEPLOYMENT_STEP_OUTPUTS_OFF:
            return "Confirm all controlled outputs off";
        case LASER_CONTROLLER_DEPLOYMENT_STEP_STABILIZE_3V3:
            return "3V3 settle delay";
        case LASER_CONTROLLER_DEPLOYMENT_STEP_DAC_SAFE_ZERO:
            return "DAC init and safe zeroing";
        case LASER_CONTROLLER_DEPLOYMENT_STEP_PERIPHERALS_VERIFY:
            return "Peripheral init and readback";
        case LASER_CONTROLLER_DEPLOYMENT_STEP_RAIL_SEQUENCE:
            return "Rail sequencing";
        case LASER_CONTROLLER_DEPLOYMENT_STEP_TEC_SETTLE:
            return "TEC settle to deployment target";
        case LASER_CONTROLLER_DEPLOYMENT_STEP_READY_POSTURE:
            return "Final ready posture";
        default:
            return "Inactive";
    }
}

const char *laser_controller_deployment_step_status_name(
    laser_controller_deployment_step_status_t status)
{
    switch (status) {
        case LASER_CONTROLLER_DEPLOYMENT_STEP_STATUS_INACTIVE:
            return "inactive";
        case LASER_CONTROLLER_DEPLOYMENT_STEP_STATUS_PENDING:
            return "pending";
        case LASER_CONTROLLER_DEPLOYMENT_STEP_STATUS_IN_PROGRESS:
            return "in_progress";
        case LASER_CONTROLLER_DEPLOYMENT_STEP_STATUS_PASSED:
            return "passed";
        case LASER_CONTROLLER_DEPLOYMENT_STEP_STATUS_FAILED:
            return "failed";
        default:
            return "inactive";
    }
}

bool laser_controller_deployment_module_required(laser_controller_module_t module)
{
    switch (module) {
        case LASER_CONTROLLER_MODULE_IMU:
        case LASER_CONTROLLER_MODULE_DAC:
        case LASER_CONTROLLER_MODULE_HAPTIC:
        case LASER_CONTROLLER_MODULE_TOF:
        case LASER_CONTROLLER_MODULE_PD:
        case LASER_CONTROLLER_MODULE_LASER_DRIVER:
        case LASER_CONTROLLER_MODULE_TEC:
            return true;
        case LASER_CONTROLLER_MODULE_BUTTONS:
        default:
            return false;
    }
}
