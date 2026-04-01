#pragma once

#include <stdbool.h>

#include "laser_controller_faults.h"
#include "laser_controller_types.h"

typedef enum {
    LASER_CONTROLLER_STATE_BOOT_INIT = 0,
    LASER_CONTROLLER_STATE_PROGRAMMING_ONLY,
    LASER_CONTROLLER_STATE_SAFE_IDLE,
    LASER_CONTROLLER_STATE_POWER_NEGOTIATION,
    LASER_CONTROLLER_STATE_LIMITED_POWER_IDLE,
    LASER_CONTROLLER_STATE_TEC_WARMUP,
    LASER_CONTROLLER_STATE_TEC_SETTLING,
    LASER_CONTROLLER_STATE_READY_ALIGNMENT,
    LASER_CONTROLLER_STATE_READY_NIR,
    LASER_CONTROLLER_STATE_ALIGNMENT_ACTIVE,
    LASER_CONTROLLER_STATE_NIR_ACTIVE,
    LASER_CONTROLLER_STATE_FAULT_LATCHED,
    LASER_CONTROLLER_STATE_SERVICE_MODE,
} laser_controller_state_t;

typedef struct {
    laser_controller_state_t current;
    laser_controller_state_t previous;
    laser_controller_time_ms_t entered_ms;
    laser_controller_fault_code_t last_transition_fault;
} laser_controller_state_machine_t;

void laser_controller_state_machine_init(
    laser_controller_state_machine_t *state_machine,
    laser_controller_time_ms_t now_ms);
bool laser_controller_state_transition_is_allowed(
    laser_controller_state_t from,
    laser_controller_state_t to);
bool laser_controller_state_transition(
    laser_controller_state_machine_t *state_machine,
    laser_controller_state_t next,
    laser_controller_time_ms_t now_ms,
    laser_controller_fault_code_t fault_code);
const char *laser_controller_state_name(laser_controller_state_t state);

