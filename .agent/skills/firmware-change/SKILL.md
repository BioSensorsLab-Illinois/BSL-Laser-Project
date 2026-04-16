---
name: firmware-change
description: Mandatory before ANY firmware change in components/laser_controller/. Encodes project-specific hard-won lessons — GPIO6 LED dual-driver rule, data-race threading constraints, deployment fault-latch ordering, telemetry validity conditions, and the state-machine gap that caused real injury. Trigger on any edit to laser_controller_*.c/h, GPIO ownership changes, fault/safety logic, rail sequencing, or state-machine edits. Overrides generic GPIO advice.
---

# firmware-change

**Read this ENTIRE file before making ANY firmware change.** A previous session caused a real human injury by making incremental changes without full audits.

## Non-Negotiable Audit Rule

For EVERY line of code you add or modify, trace how it interacts with EVERY other line in the codebase. This means:

1. Find every reader and writer of every field you touch
2. Identify which task/core each reader/writer runs on
3. Verify locking is consistent (same lock for all writers)
4. Verify state machine transitions are allowed for every derive_state return value
5. Trace every path from your change to any GPIO output

## Threading Model — CRITICAL

The ESP32-S3 is dual-core. Three tasks access `s_context`:

- **Control task** (priority 20, 5ms tick): reads/writes `s_context` fields directly with NO lock for most fields. It is the intended sole writer during normal operation.
- **Comms task**: reads `s_runtime_status` (published by control task under `s_runtime_status_lock`). Calls app-layer functions that write `s_context` under `s_context_lock`.
- **Main task**: calls app-layer functions during boot.

**THE DATA RACE**: `s_context.fault_latched`, `active_fault_code`, `active_fault_class`, `latched_fault_code`, `latched_fault_class` are written by the control task WITHOUT any lock. If you write these from another task (even under `s_context_lock`), you have a data race. The control task does not take `s_context_lock` before writing these fields.

**CORRECT PATTERN**: To modify control-task-owned fields from another task, use a flag/queue that the control task reads on its next tick, not direct writes. Or disable the control task during the write.

## GPIO6 (ToF LED) — Two Control Paths

**Service path** (`laser_controller_board_set_tof_illumination`):
- Bring-up/service mode direct control
- Gated on `service_module_write_enabled(TOF)`

**Runtime path** (`laser_controller_board_set_runtime_tof_illumination`):
- Called every 5ms tick from app.c
- MUST bail out when service owns the sideband — otherwise overwrites service-set state every tick

**apply_outputs**: Calls `apply_tof_sideband_state(any_owns_tof_sideband())` every tick. This is what actually drives the LEDC hardware. **NEVER REMOVE THIS CALL.**

**apply_tof_sideband_state_locked**: Has an early-return optimization (lines ~755-759) that skips LEDC reconfiguration when nothing changed. Do not break this — reconfiguring LEDC every 5ms causes visible flicker.

## State Machine

Check `laser_controller_state.c` for allowed transitions. Every state should be able to reach `PROGRAMMING_ONLY` and `FAULT_LATCHED` because power can drop at any time.

Missing transition that caused `unexpected_state` faults:
- `SAFE_IDLE → PROGRAMMING_ONLY` — NOT in the current committed code. Must be added.

## Telemetry Validity

- **LD valid** = `ld_rail_pgood AND SBDN pin high` (board.c ~line 3923)
- **TEC valid** = `tec_rail_pgood` (board.c ~line 3926)
- LD faults (overtemp, loop-bad, unexpected-current) are gated on `ld_rail_pgood` in safety.c — they don't fire when rails are off

## Deployment and Faults

The deployment checklist enables rails. Faults from rail-OFF conditions must not block deployment. BUT: clearing fault latches must be done safely — not by directly zeroing fields from another task (data race).

**Safe approach**: Have deployment request fault clearing through a flag that the control task processes on its next tick inside the critical section it already uses.

## What NOT To Do (Proven Failures)

1. **DO NOT write fault fields from the comms/main task** — data race with the control task
2. **DO NOT remove `apply_tof_sideband_state` from `apply_outputs`** — breaks LED control
3. **DO NOT let the runtime path overwrite `s_tof_illumination` when service mode owns it** — kills bring-up LED
4. **DO NOT call `enter_deployment_mode()` then `run_deployment_sequence()` from the main task** — race between the two calls while the control task runs
5. **DO NOT set `DEPLOYMENT_STEP_DELAY_MS` to 0 without verifying GPIO stabilization** — may cause voltage transients
