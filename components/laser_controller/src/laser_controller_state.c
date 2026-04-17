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
    /*
     * Permissive transition policy (2026-04-17 rewrite). The previous
     * per-state allowlist was a paper model of the "intended" runtime
     * sequence — but the real system can transition between many state
     * pairs as a function of power tier, rail PGOOD, fault clears, and
     * service-mode entry. Each missing edge in the old table caused
     * spurious UNEXPECTED_STATE / SAFETY_LATCHED faults that the operator
     * had to clear manually (e.g. SAFE_IDLE -> TEC_SETTLING during
     * normal warmup).
     *
     * Real safety invariants are enforced by `safety_evaluate` and
     * `derive_outputs`, not by this label-level table. The only
     * meaningful invariant left here is:
     *
     *   - You should not be able to jump from BOOT_INIT directly into
     *     an actively-emitting state (ALIGNMENT_ACTIVE / NIR_ACTIVE)
     *     because no operator request can have been issued on tick 0.
     *     Any other path that reaches an emit state has already passed
     *     the safety evaluator's gates.
     *
     * Everything else is allowed. Self-loops, SERVICE_MODE, and
     * FAULT_LATCHED are universally reachable.
     */
    if (from == to) {
        return true;
    }

    if (to == LASER_CONTROLLER_STATE_SERVICE_MODE) {
        return true;
    }

    if (to == LASER_CONTROLLER_STATE_FAULT_LATCHED) {
        return true;
    }

    if (to == LASER_CONTROLLER_STATE_ALIGNMENT_ACTIVE ||
        to == LASER_CONTROLLER_STATE_NIR_ACTIVE) {
        if (from == LASER_CONTROLLER_STATE_BOOT_INIT) {
            return false;
        }
    }

    return true;
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
