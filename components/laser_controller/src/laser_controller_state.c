#include "laser_controller_state.h"

#include <stddef.h>

void laser_controller_state_machine_init(
    laser_controller_state_machine_t *state_machine,
    laser_controller_time_ms_t now_ms)
{
    if (state_machine == NULL) {
        return;
    }

    state_machine->current = LASER_CONTROLLER_STATE_BOOT_INIT;
    state_machine->previous = LASER_CONTROLLER_STATE_BOOT_INIT;
    state_machine->entered_ms = now_ms;
    state_machine->last_transition_fault = LASER_CONTROLLER_FAULT_NONE;
}

bool laser_controller_state_transition_is_allowed(
    laser_controller_state_t from,
    laser_controller_state_t to)
{
    if (from == to) {
        return true;
    }

    if (to == LASER_CONTROLLER_STATE_SERVICE_MODE) {
        return true;
    }

    switch (from) {
        case LASER_CONTROLLER_STATE_BOOT_INIT:
            return to == LASER_CONTROLLER_STATE_PROGRAMMING_ONLY ||
                   to == LASER_CONTROLLER_STATE_SAFE_IDLE ||
                   to == LASER_CONTROLLER_STATE_POWER_NEGOTIATION ||
                   to == LASER_CONTROLLER_STATE_FAULT_LATCHED;
        case LASER_CONTROLLER_STATE_PROGRAMMING_ONLY:
            return to == LASER_CONTROLLER_STATE_POWER_NEGOTIATION ||
                   to == LASER_CONTROLLER_STATE_SAFE_IDLE ||
                   to == LASER_CONTROLLER_STATE_FAULT_LATCHED;
        case LASER_CONTROLLER_STATE_SAFE_IDLE:
            return to == LASER_CONTROLLER_STATE_POWER_NEGOTIATION ||
                   to == LASER_CONTROLLER_STATE_LIMITED_POWER_IDLE ||
                   to == LASER_CONTROLLER_STATE_TEC_WARMUP ||
                   to == LASER_CONTROLLER_STATE_READY_ALIGNMENT ||
                   to == LASER_CONTROLLER_STATE_READY_NIR ||
                   to == LASER_CONTROLLER_STATE_FAULT_LATCHED ||
                   to == LASER_CONTROLLER_STATE_SERVICE_MODE;
        case LASER_CONTROLLER_STATE_POWER_NEGOTIATION:
            return to == LASER_CONTROLLER_STATE_PROGRAMMING_ONLY ||
                   to == LASER_CONTROLLER_STATE_LIMITED_POWER_IDLE ||
                   to == LASER_CONTROLLER_STATE_SAFE_IDLE ||
                   to == LASER_CONTROLLER_STATE_TEC_WARMUP ||
                   to == LASER_CONTROLLER_STATE_FAULT_LATCHED;
        case LASER_CONTROLLER_STATE_LIMITED_POWER_IDLE:
            return to == LASER_CONTROLLER_STATE_PROGRAMMING_ONLY ||
                   to == LASER_CONTROLLER_STATE_POWER_NEGOTIATION ||
                   to == LASER_CONTROLLER_STATE_READY_ALIGNMENT ||
                   to == LASER_CONTROLLER_STATE_ALIGNMENT_ACTIVE ||
                   to == LASER_CONTROLLER_STATE_FAULT_LATCHED ||
                   to == LASER_CONTROLLER_STATE_SAFE_IDLE;
        case LASER_CONTROLLER_STATE_TEC_WARMUP:
            return to == LASER_CONTROLLER_STATE_TEC_SETTLING ||
                   to == LASER_CONTROLLER_STATE_READY_ALIGNMENT ||
                   to == LASER_CONTROLLER_STATE_FAULT_LATCHED ||
                   to == LASER_CONTROLLER_STATE_PROGRAMMING_ONLY ||
                   to == LASER_CONTROLLER_STATE_POWER_NEGOTIATION;
        case LASER_CONTROLLER_STATE_TEC_SETTLING:
            return to == LASER_CONTROLLER_STATE_READY_ALIGNMENT ||
                   to == LASER_CONTROLLER_STATE_READY_NIR ||
                   to == LASER_CONTROLLER_STATE_ALIGNMENT_ACTIVE ||
                   to == LASER_CONTROLLER_STATE_FAULT_LATCHED ||
                   to == LASER_CONTROLLER_STATE_PROGRAMMING_ONLY ||
                   to == LASER_CONTROLLER_STATE_POWER_NEGOTIATION;
        case LASER_CONTROLLER_STATE_READY_ALIGNMENT:
            return to == LASER_CONTROLLER_STATE_ALIGNMENT_ACTIVE ||
                   to == LASER_CONTROLLER_STATE_READY_NIR ||
                   to == LASER_CONTROLLER_STATE_TEC_SETTLING ||
                   to == LASER_CONTROLLER_STATE_SAFE_IDLE ||
                   to == LASER_CONTROLLER_STATE_FAULT_LATCHED ||
                   to == LASER_CONTROLLER_STATE_PROGRAMMING_ONLY ||
                   to == LASER_CONTROLLER_STATE_POWER_NEGOTIATION;
        case LASER_CONTROLLER_STATE_READY_NIR:
            return to == LASER_CONTROLLER_STATE_NIR_ACTIVE ||
                   to == LASER_CONTROLLER_STATE_ALIGNMENT_ACTIVE ||
                   to == LASER_CONTROLLER_STATE_READY_ALIGNMENT ||
                   to == LASER_CONTROLLER_STATE_TEC_SETTLING ||
                   to == LASER_CONTROLLER_STATE_SAFE_IDLE ||
                   to == LASER_CONTROLLER_STATE_FAULT_LATCHED ||
                   to == LASER_CONTROLLER_STATE_PROGRAMMING_ONLY ||
                   to == LASER_CONTROLLER_STATE_POWER_NEGOTIATION;
        case LASER_CONTROLLER_STATE_ALIGNMENT_ACTIVE:
            return to == LASER_CONTROLLER_STATE_READY_ALIGNMENT ||
                   to == LASER_CONTROLLER_STATE_READY_NIR ||
                   to == LASER_CONTROLLER_STATE_NIR_ACTIVE ||
                   to == LASER_CONTROLLER_STATE_FAULT_LATCHED ||
                   to == LASER_CONTROLLER_STATE_PROGRAMMING_ONLY ||
                   to == LASER_CONTROLLER_STATE_POWER_NEGOTIATION;
        case LASER_CONTROLLER_STATE_NIR_ACTIVE:
            return to == LASER_CONTROLLER_STATE_READY_NIR ||
                   to == LASER_CONTROLLER_STATE_ALIGNMENT_ACTIVE ||
                   to == LASER_CONTROLLER_STATE_FAULT_LATCHED ||
                   to == LASER_CONTROLLER_STATE_PROGRAMMING_ONLY ||
                   to == LASER_CONTROLLER_STATE_POWER_NEGOTIATION;
        case LASER_CONTROLLER_STATE_FAULT_LATCHED:
            return to == LASER_CONTROLLER_STATE_PROGRAMMING_ONLY ||
                   to == LASER_CONTROLLER_STATE_POWER_NEGOTIATION ||
                   to == LASER_CONTROLLER_STATE_SAFE_IDLE;
        case LASER_CONTROLLER_STATE_SERVICE_MODE:
            return to == LASER_CONTROLLER_STATE_SAFE_IDLE ||
                   to == LASER_CONTROLLER_STATE_POWER_NEGOTIATION ||
                   to == LASER_CONTROLLER_STATE_LIMITED_POWER_IDLE ||
                   to == LASER_CONTROLLER_STATE_TEC_WARMUP ||
                   to == LASER_CONTROLLER_STATE_TEC_SETTLING ||
                   to == LASER_CONTROLLER_STATE_READY_ALIGNMENT ||
                   to == LASER_CONTROLLER_STATE_READY_NIR ||
                   to == LASER_CONTROLLER_STATE_FAULT_LATCHED ||
                   to == LASER_CONTROLLER_STATE_PROGRAMMING_ONLY;
        default:
            return false;
    }
}

bool laser_controller_state_transition(
    laser_controller_state_machine_t *state_machine,
    laser_controller_state_t next,
    laser_controller_time_ms_t now_ms,
    laser_controller_fault_code_t fault_code)
{
    if (state_machine == NULL) {
        return false;
    }

    if (!laser_controller_state_transition_is_allowed(state_machine->current, next)) {
        return false;
    }

    state_machine->previous = state_machine->current;
    state_machine->current = next;
    state_machine->entered_ms = now_ms;
    state_machine->last_transition_fault = fault_code;
    return true;
}

const char *laser_controller_state_name(laser_controller_state_t state)
{
    switch (state) {
        case LASER_CONTROLLER_STATE_BOOT_INIT:
            return "BOOT_INIT";
        case LASER_CONTROLLER_STATE_PROGRAMMING_ONLY:
            return "PROGRAMMING_ONLY";
        case LASER_CONTROLLER_STATE_SAFE_IDLE:
            return "SAFE_IDLE";
        case LASER_CONTROLLER_STATE_POWER_NEGOTIATION:
            return "POWER_NEGOTIATION";
        case LASER_CONTROLLER_STATE_LIMITED_POWER_IDLE:
            return "LIMITED_POWER_IDLE";
        case LASER_CONTROLLER_STATE_TEC_WARMUP:
            return "TEC_WARMUP";
        case LASER_CONTROLLER_STATE_TEC_SETTLING:
            return "TEC_SETTLING";
        case LASER_CONTROLLER_STATE_READY_ALIGNMENT:
            return "READY_ALIGNMENT";
        case LASER_CONTROLLER_STATE_READY_NIR:
            return "READY_NIR";
        case LASER_CONTROLLER_STATE_ALIGNMENT_ACTIVE:
            return "ALIGNMENT_ACTIVE";
        case LASER_CONTROLLER_STATE_NIR_ACTIVE:
            return "NIR_ACTIVE";
        case LASER_CONTROLLER_STATE_FAULT_LATCHED:
            return "FAULT_LATCHED";
        case LASER_CONTROLLER_STATE_SERVICE_MODE:
            return "SERVICE_MODE";
        default:
            return "UNKNOWN";
    }
}
