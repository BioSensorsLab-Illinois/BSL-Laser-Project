# Firmware Component Guardrails

You are editing the **BSL laser controller firmware** — life-critical, safety-authoritative C code running on ESP32-S3 under ESP-IDF v6.0. This file applies in addition to the repo-root `CLAUDE.md` and `.agent/AGENT.md`.

**Mandatory before any edit here:**

- `.agent/skills/firmware-change/SKILL.md` — the audit rule, threading model, GPIO6 dual-driver lesson, state-machine invariants.
- `.agent/skills/hardware-safety/SKILL.md` — if GPIO / rails / I2C / SPI are involved.
- `docs/firmware-pinmap.md` — the pin map.
- `docs/datasheet-programming-notes.md` — per-peripheral rules.

## The Five Audit Points (re-read every session)

1. **Threading** — control task (5ms, priority 20) owns `s_context` fault fields WITHOUT locks. Never write `fault_latched`, `active_fault_*`, `latched_fault_*` from comms or main. Use a request flag.
2. **State machine** — every `derive_state()` return value must have an allowed transition from the current state in `laser_controller_state.c`. Power can drop at any time — every state must reach `PROGRAMMING_ONLY` and `FAULT_LATCHED`.
3. **GPIO6 / ToF LED** — `apply_tof_sideband_state` MUST be called from `apply_outputs()` every tick. The runtime path MUST bail out when service owns the sideband. The early-return optimization in `apply_tof_sideband_state_locked` MUST stay intact.
4. **Deployment & faults** — deployment enables rails. Rail-OFF faults must not block deployment entry. Fault latch clearing goes through a control-task request flag — never direct zeroing from another task.
5. **Command gating** — every comms handler checks `deployment.active`, `deployment.running`, `service_mode`, `fault_latched` at the right level. A new command that bypasses a gate is a policy change.

## Rail & Pin Hard Rules

- TEC rail (GPIO15) enables BEFORE LD rail (GPIO17).
- TEC loss (GPIO16 PGOOD falling) → immediate LD shutdown: SBDN (GPIO13) low + PCN (GPIO21) low.
- ADC1 telemetry (GPIO1/2/8/9/10) is INVALID unless its rail is on. Code reading ADC without the rail-on precondition is broken.
- GPIO37 is hazardous — shared net (ERM_TRIG + GN_LD_EN). Any write here needs explicit safety review.
- GPIO4/5 I2C bus carries 4 devices across 3 boards. Bus recovery must restore the intended posture.

## After Every Edit

1. Run `. $IDF_PATH/export.sh && idf.py build`. Capture the result.
2. Run `/audit-firmware` (spawns `bsl-firmware-auditor` and `bsl-gpio-auditor`) on the diff.
3. If a JSON command changed, spawn `bsl-protocol-auditor`.
4. Update `.agent/runs/powered-ready-console/Status.md`.
5. For any powered-ready claim, run `/validate-powered` (spawns `bsl-bench-validator`). USB Phase 1 is never a substitute.

## The One Lesson That Must Stick

A previous session made incremental unaudited firmware edits and caused a real human injury from an unexpected LED activation. The audit rule above is not optional — it is why this file exists.
