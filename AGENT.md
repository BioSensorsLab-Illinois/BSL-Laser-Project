# AGENT.md

This repository controls a life-critical surgical laser controller. Every change must assume that a misunderstanding can injure a patient.

## Mission
Preserve a firmware and host architecture where the default answer to ambiguity is:

- NIR off
- green alignment off
- laser driver in standby
- LD VIN off unless explicitly required
- TEC VIN off unless explicitly required

Do not claim clinical or OR readiness from code changes alone. Bench-safe logic and production-candidate sequencing are necessary, but not sufficient.

## Non-Negotiables
1. Never add a path that can emit on boot, reset, panic, brownout, watchdog, task crash, config corruption, stale data, or undefined state.
2. Never bypass the centralized beam-permission gate in [laser_controller_safety.c](/Users/zz4/BSL/BSL-Laser/components/laser_controller/src/laser_controller_safety.c).
3. Never hide safety decisions in UI code, comms handlers, or peripheral drivers.
4. Never assume a rail is good because its enable is asserted. Require PGOOD plus plausibility.
5. Never treat stale or invalid IMU/ToF data as “probably fine.”
6. Never add service overrides to normal runtime paths.
7. Never land hidden debug commands that can defeat interlocks.
8. Never replace explicit state transitions with implicit behavior.
9. Never let the browser flasher program arbitrary `.bin` files. Web flashing must reject any image without a valid embedded BSL firmware signature block.
10. If unsure whether a condition should latch, bias toward latching and forcing explicit recovery.

## Architecture Ownership
- [laser_controller_safety.c](/Users/zz4/BSL/BSL-Laser/components/laser_controller/src/laser_controller_safety.c)
  Only authoritative beam-permission decision point.
- [laser_controller_app.c](/Users/zz4/BSL/BSL-Laser/components/laser_controller/src/laser_controller_app.c)
  Control loop, deployment supervisor, power-tier classification, fault latching, output derivation, top-level state machine.
- [laser_controller_board.c](/Users/zz4/BSL/BSL-Laser/components/laser_controller/src/laser_controller_board.c)
  Bench HAL with real GPIO, ADC, I2C, SPI, DAC, PD, IMU, ToF, haptic, wireless-facing readback, and GPIO inspector truth.
- [laser_controller_service.c](/Users/zz4/BSL/BSL-Laser/components/laser_controller/src/laser_controller_service.c)
  Protected bring-up ownership, module plan, service-only bus tools, GPIO overrides, and NVS-backed bring-up profile persistence.
- [laser_controller_comms.c](/Users/zz4/BSL/BSL-Laser/components/laser_controller/src/laser_controller_comms.c)
  JSON-line protocol parser, command router, status/event serialization, USB CDC and wireless protocol parity.
- [host-console/src/hooks/use-device-session.ts](/Users/zz4/BSL/BSL-Laser/host-console/src/hooks/use-device-session.ts)
  Host session state, command serialization, ACK handling, reconnect behavior, and session export.
- [host-console/src/lib/controller-protocol.ts](/Users/zz4/BSL/BSL-Laser/host-console/src/lib/controller-protocol.ts)
  Shared host-side protocol interpreter. USB and wireless must stay on one parser.

## Current Live Hardware State
Latest validated attached-board facts, update this section with new bench observations instead of burying them in commit messages.

- Attached controller enumerates on macOS as `/dev/cu.usbmodem201101`.
- Flashing over `idf.py -p /dev/cu.usbmodem201101 flash` is working on the current board.
- The current board boots bench-safe into `PROGRAMMING_ONLY` on plain USB host power.
- Real peripheral readback is currently present for DAC, STUSB4500, IMU, ToF, and haptic on the attached board.
- On plain USB host power, live PD readback reports host-only `5.0 V / 0.5 A` and deployment mode correctly fails at PD qualification.
- USB-only deployment validation now confirms:
  - `enter_deployment_mode` succeeds
  - `run_deployment_sequence` aborts safely at `pd_inspect` with `pd_insufficient`
  - both rails stay off
  - runtime optical commands stay rejected before deployment readiness
- The controller protocol now accepts both compact JSON and ordinary spaced JSON over USB serial.
- Deployment mode now rejects `enter_service_mode` and mutating GPIO/service writes on the live board until deployment mode is exited.
- The current serial validation corpus is hardware-validated. Wireless protocol parity is still code-reviewed but not yet bench-validated on the attached rig.

## Recurring Operator Complaints and Expectations
These are not optional style notes. They are repeated user complaints or expectations and should be treated as product requirements.

- Do not make the host or scripts depend on hidden protocol formatting quirks. Ordinary JSON must work.
- Distinguish carefully between:
  - staged host intent
  - controller-derived safety truth
  - actual peripheral readback
  Never present staged values as if they were live hardware truth.
- Runtime optical control must not be trapped behind service mode. Service mode is for bring-up and debug, not normal operation.
- Control-page runtime actions must never cause obvious rail-drop regressions. `Enable NIR Laser` must not shut down LD/TEC supplies unexpectedly.
- Requested vs active laser state must stay separate in the UI and protocol.
- If a temporary local-only test hook is used, call it out explicitly, keep it obvious in logs, and remove it completely after validation.
- Keep this file useful. Future agents should append validated facts, complaints, and refactor needs here instead of forcing rediscovery every session.

## Compatibility Hazards
- The firmware uses a hand-rolled tolerant JSON-line parser in [laser_controller_comms.c](/Users/zz4/BSL/BSL-Laser/components/laser_controller/src/laser_controller_comms.c). It must stay compatible with:
  - compact JSON
  - spaced JSON
  - key-order variation
  - nested `args` wrappers
  - quoted numeric payloads where host tooling might emit them
- Status freshness after mutating commands is easy to get wrong. If a command response must reflect post-mutation controller state, wait a control-cycle and refresh before replying.
- Analog LD telemetry can float when the LD path is unpowered. Treat unexpected-current and overtemperature decisions carefully when LD rail PGOOD is false.
- Deployment mode and service mode are mutually exclusive. Entering deployment must reclaim ownership from service mode, GPIO overrides, ToF illumination, and direct haptic enable.
- Browser flashing stays USB/Web Serial only. Do not extend it to wireless without separate validation.

## Validated Findings Log
Append dated findings here. Keep observed bench facts separate from inference.

### 2026-04-03
- [Observed on hardware] Deployment mode is implemented as a firmware-owned session mode with a checklist status block in the controller snapshot.
- [Build-verified + observed on hardware] Control-page runtime requests are now gated on deployment readiness instead of service mode.
- [Build-verified + observed on hardware] Host optical requests are wired into the real safety path instead of only staging bench status.
- [Observed on hardware] USB-only hardware validation passed:
  - deployment entry works
  - deployment safe-fails at PD qualification
  - both rails remain off
  - runtime optical request is rejected before deployment readiness
- [Observed on hardware] Deployment target and deployment safety writes work on hardware.
- [Observed on hardware] Deployment current cap no longer collapses to `0` after deployment-safety edits while PD readback is transient.
- [Observed on hardware] The protocol parser was fixed so spaced JSON no longer fails as `Malformed command envelope.`
- [Observed on hardware] A second protocol bug was fixed: nested `args.id` / `args.cmd` can no longer shadow top-level envelope keys. Root envelope fields are now parsed only from the outer JSON object.
- [Observed on hardware] Deployment mode lockout validation passed:
  - `enter_service_mode` is rejected while deployment mode is active
  - mutating GPIO override writes are rejected while deployment mode is active
  - `exit_deployment_mode` returns a fresh snapshot with `deployment.active=false`
- [Observed on hardware with temporary local-only hook, then removed] A dry-run harness was used to spoof PD / PGOOD / loop-good while physically blocking LD and TEC enables low. That dry-run validated:
  - full 10-step deployment checklist progression
  - deployment ready posture
  - `laser_output_enable` no longer drops LD/TEC rails
  - `laser_output_disable` returns to ready posture without powering rails down
  The temporary hook was removed and the clean baseline image was reflashed afterward.

## System-Level Refactors Still Needed
- Replace the hand-rolled JSON parser with a more robust structured parser once footprint and determinism are acceptable.
- Formalize analog signal validity for LD and TEC telemetry so floating ADC paths do not create hidden safety ambiguity.
- Separate bench-service, deployment, and runtime concerns more cleanly in the host UI and protocol surface.
- Add repeatable validation automation beyond ad hoc bench probes. Start with the scripts in `host-console/scripts`.
- Tighten PD supervision into a finished production policy once real PD behavior is stable on the target hardware.
- Resolve final button-path architecture and merge it cleanly with host runtime requests.

## Validation Script Corpus
Permanent live-board validation scripts belong under `host-console/scripts`.

Current script:
- [live_controller_validation.py](/Users/zz4/BSL/BSL-Laser/host-console/scripts/live_controller_validation.py)

Expected usage pattern:
- run the parser compatibility matrix after protocol changes
- run the deployment lockout scenario after service/deployment ownership changes
- run the USB-only deployment safe-fail scenario after deployment changes
- extend this corpus instead of replacing it with one-off shell snippets

Current scenarios:
- `parser-matrix`
  Covers spaced JSON, compact JSON, nested `args`, quoted booleans, and root-envelope shadowing regressions.
- `deployment-lockout`
  Covers deployment entry freshness, service-write rejection during deployment, and deployment exit freshness.
- `deployment-usb-safe-fail`
  Covers the clean USB-only safe-fail path.
- `deployment-runtime-flow`
  Intended for a powered dry-run or real-PD environment where deployment can complete.

## Temporary Local Test-Hook Policy
Temporary local-only hooks are allowed only for controlled bench validation when:

- they are clearly labeled as local dry-run logic
- they are not exposed through normal protocol or UI paths
- they keep physical outputs safe
- they are removed completely after validation

Required behavior for future temporary hooks:
- document the hook here before using it
- record what was spoofed, what remained real, and what physical outputs were blocked
- remove the hook and reflash the baseline image before closing the task

Most recent hook history:
- 2026-04-03
  A local-only deployment dry-run hook spoofed valid PD, rail PGOOD, TEC good, loop-good, and plausible power-dependent telemetry while forcibly holding the physical LD and TEC enable GPIOs low.
  Real DAC, IMU, ToF, and haptic readback remained live.
  The hook was removed after validation, `git diff --check` was clean, and the baseline firmware was rebuilt and reflashed.

## Future-Agent Update Rules
When you touch this repo:

1. Append validated bench facts here instead of silently relying on them.
2. Mark whether each note is:
   - observed on hardware
   - build-verified only
   - inferred from code or schematics
3. Keep recurring user complaints in this file if they affect architecture or workflow.
4. Call out system-wide refactors here if they span firmware, protocol, and host UI.
5. If you add a temporary local-only hook, record it here and confirm its removal afterward.
6. Update [docs/protocol-spec.md](/Users/zz4/BSL/BSL-Laser/docs/protocol-spec.md) when the live protocol changes.
7. Do not rewrite history casually. Preserve old findings unless they are explicitly disproven, then replace them with a dated correction.

## Read Before Hardware Work
Before changing hardware-facing code, read:

- [hardware-recon.md](/Users/zz4/BSL/BSL-Laser/docs/hardware-recon.md)
- [firmware-pinmap.md](/Users/zz4/BSL/BSL-Laser/docs/firmware-pinmap.md)
- [datasheet-programming-notes.md](/Users/zz4/BSL/BSL-Laser/docs/datasheet-programming-notes.md)
- [validation-plan.md](/Users/zz4/BSL/BSL-Laser/docs/validation-plan.md)
