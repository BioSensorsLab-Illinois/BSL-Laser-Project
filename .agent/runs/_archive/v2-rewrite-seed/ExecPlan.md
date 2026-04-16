# Execute the V2 Rewrite With USB-Only Phase 1

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and `Outcomes & Retrospective` must be kept up to date as work proceeds.

This document must be maintained in accordance with `.agent/PLANS.md`.

## Purpose / Big Picture

After this rewrite, the firmware and GUI use one explicit runtime model instead of the current mix of service-mode paths, deployment-page paths, and bench-control assumptions. The Control page becomes the single runtime surface for deployment entry, checklist status, runtime-mode selection, and host-owned modulation controls. The firmware becomes explicit about who owns each GPIO-sensitive path and when telemetry is genuinely valid instead of floating or inferred.

The rewrite is split into two bench phases. Phase 1 is the current USB-only bench and is the only phase this plan is allowed to complete now. Phase 1 can validate serial/protocol behavior, GPIO ownership, shared-bus stability, IMU, DAC, ToF, DRV2605 register access, GUI rendering, and safe pin-level behavior for `SBDN`, `PCN`, `PWR_TEC_EN`, and `PWR_LD_EN`. Phase 2 begins later on a powered PD bench and is required before any claim about PD negotiation, rail sequencing, deployment-ready completion, or actual laser enable.

## Progress

- [x] (2026-04-05 21:40 America/Chicago) Promoted the seed run folder into the active rewrite workspace.
- [x] (2026-04-05 21:45 America/Chicago) Updated `.agent/AGENT.md` and repo-local validation skills with USB-only Phase 1 and GPIO ownership rules.
- [x] (2026-04-05 22:05 America/Chicago) Refactored the source tree for Phase 1 runtime mode, telemetry validity, inline deployment UI, and bring-up deployment lock behavior.
- [x] (2026-04-05 22:20 America/Chicago) `host-console` build passed and rendered-page screenshots were captured for Control and Bring-up.
- [x] (2026-04-05 22:39 America/Chicago) Installed local ESP-IDF `v6.0`, built the firmware, flashed `/dev/cu.usbmodem201101`, and reran the USB-only Phase 1 serial scenarios against the edited image.
- [x] (2026-04-05 22:58 America/Chicago) Integrated the first independent review findings, rebuilt/reflashed, and reran the full USB-only Phase 1 serial matrix successfully.
- [ ] Independent review findings integrated or cleared.

## Surprises & Discoveries

- Observation: The current firmware already keeps `SBDN` driven as an output in normal runtime and uses `PCN low` as the safe current-selection posture, so the runtime driver-control rewrite can build on that instead of reintroducing Hi-Z.
  Evidence: `components/laser_controller/src/laser_controller_board.c` drives `LD_SBDN` and `LD_PCN` directly in `laser_controller_board_drive_safe_gpio_levels()`.
- Observation: The current GUI and firmware are deeply coupled to the dedicated Deployment page and to `bench` requests, so the safest Phase 1 path is to keep deployment mode in firmware while moving the workflow UI inline into Control.
  Evidence: `components/laser_controller/src/laser_controller_comms.c` and `host-console/src/components/DeploymentWorkbench.tsx` both treat deployment as the runtime lock authority.
- Observation: The repo still has no source-backed stage-1 / stage-2 trigger wiring, so `binary_trigger` cannot be called complete in Phase 1.
  Evidence: `docs/hardware-recon.md` and `docs/firmware-pinmap.md` still mark the button board as unresolved.
- Observation: The local environment can build and flash the firmware once ESP-IDF `v6.0` is installed under `~/esp/esp-idf`; the repo’s CI assumptions match a normal local `IDF_PATH`-based setup.
  Evidence: `idf.py fullclean build` and `idf.py -p /dev/cu.usbmodem201101 flash` both succeeded after the install.
- Observation: The USB-only failure-path bug was not in the high-level deployment step logic itself; it was in the command contract. `run_deployment_sequence` returned before the checklist reached a terminal state, even though the protocol and validation flow treat it as blocking.
  Evidence: before the fix, the live device remained `deployment.running=true` in `pd_inspect` immediately after `run_deployment_sequence` returned; after adding a wait-for-result path in `laser_controller_comms.c`, `deployment-usb-safe-fail` passed.
- Observation: The first strict review pass found three concrete issues in the validated state, and all three were fixable without changing the Phase 1 architecture:
  - deployment-locked Bring-up still exposed service-mode buttons
  - deployment enter/exit responses omitted the new runtime-mode fields
  - deployment-time module readback degraded to error immediately instead of after a grace window
  Evidence: review findings were returned by the review-only agent and fixed in `BringupWorkbench.tsx`, `laser_controller_comms.c`, and `ModuleReadinessPanel.tsx`.

## Decision Log

- Decision: Keep firmware deployment mode as the runtime gate, but remove the dedicated Deployment page and move its user-facing flow inline into Control.
  Rationale: This preserves the existing safety authority while simplifying the operator workflow and avoiding a risky Phase 1 protocol break.
  Date/Author: 2026-04-05 / Codex
- Decision: Split runtime control into `binary_trigger` and `modulated_host`.
  Rationale: The user explicitly wants binary ON/OFF and modulated logic to be mutually exclusive, with host control only for the modulated path.
  Date/Author: 2026-04-05 / Codex
- Decision: Treat `binary_trigger` as architected-but-blocked in Phase 1.
  Rationale: The trigger wiring is unresolved in-repo, so Phase 1 can only implement the mode seam and guardrails, not final physical-trigger behavior.
  Date/Author: 2026-04-05 / Codex
- Decision: Make USB-only limitations first-class in docs, validation, and UI.
  Rationale: The project is life-critical and must not blur “not yet powered” with “validated”.
  Date/Author: 2026-04-05 / Codex

## Outcomes & Retrospective

- Outcome: Phase 1 now includes a validated local ESP-IDF setup, a successful firmware build/flash cycle, passing USB-only serial validation on the edited image, and one completed fix cycle from an independent review.
  Gap: One final fresh review-only pass still needs to clear the updated state, and Phase 2 powered-rail validation is still out of scope.

## Context and Orientation

The current firmware lives in `components/laser_controller/`. The top-level control loop is in `components/laser_controller/src/laser_controller_app.c`. Hardware read/write code is in `components/laser_controller/src/laser_controller_board.c`. Host-command parsing and JSON output are in `components/laser_controller/src/laser_controller_comms.c`. Bench/runtime staging is held in `components/laser_controller/src/laser_controller_bench.c`. The current dedicated deployment model is represented in `components/laser_controller/include/laser_controller_deployment.h`.

The browser GUI lives in `host-console/src/`. The app shell is `host-console/src/App.tsx`. Runtime control is in `host-console/src/components/ControlWorkbench.tsx`. The current dedicated deployment page is `host-console/src/components/DeploymentWorkbench.tsx`. Bring-up behavior is `host-console/src/components/BringupWorkbench.tsx`. Snapshot and telemetry types live in `host-console/src/types.ts`. Live snapshot parsing and merging live in `host-console/src/lib/controller-protocol.ts`, `host-console/src/lib/live-telemetry.ts`, and `host-console/src/hooks/use-device-session.ts`. The mock runtime lives in `host-console/src/lib/mock-transport.ts`.

Important definitions used in this plan:

- Deployment mode: the firmware-owned state that locks service writes, owns the pre-enable checklist, and determines whether runtime control may be used.
- Runtime mode: the post-deployment laser-control selection. This plan uses `binary_trigger` and `modulated_host`.
- Telemetry validity: whether a reported numeric value is trustworthy enough to show as live hardware truth. Invalid values must be shown as OFF or INVALID rather than raw floating data.
- GPIO ownership: which subsystem currently controls a pin’s direction, pull state, output level, or peripheral matrix attachment.

## Plan of Work

The first milestone updates repo truth and the runbook. This is already complete and must stay in sync with the code as discoveries happen.

The second milestone refactors the firmware runtime model. In `components/laser_controller/include/laser_controller_bench.h` and `components/laser_controller/src/laser_controller_bench.c`, add a runtime-mode enum and store the active runtime mode in the bench/runtime staging state. The default for Phase 1 should be `modulated_host` so the USB-only bench can still exercise host-owned runtime controls after deployment becomes ready on a future powered bench. Add a setter API that rejects impossible transitions, especially when requests are active or modulation is still enabled.

In `components/laser_controller/src/laser_controller_comms.c`, add the `set_runtime_mode` command. Keep existing deployment commands, but restrict runtime control commands based on the active mode. `set_laser_power`, `laser_output_enable`, `laser_output_disable`, and `configure_modulation` must be rejected unless the mode is `modulated_host`. `enable_alignment` and `disable_alignment` must stop being treated as a normal runtime path; in Phase 1 they should either be removed from the runtime command allow-list or rejected with an explicit message that the binary trigger path is blocked pending trigger-hardware definition.

In `components/laser_controller/include/laser_controller_board.h` and `components/laser_controller/src/laser_controller_board.c`, add explicit LD and TEC telemetry-validity fields to the board input snapshot. LD telemetry is valid only when LD rail `PGOOD` is high and `SBDN` is driven high. TEC telemetry is valid only when TEC rail `PGOOD` is high. When validity is false, publish zero-safe numeric values and false validity instead of reading or reusing floating measurements. Keep the current direct-drive `SBDN` and `PCN` posture, but make the intent explicit in comments and runtime-state derivation: `SBDN` is only for shutdown/fault/safe-off, and `PCN` is the ON/OFF or PWM-owned control depending on runtime mode.

Still in `laser_controller_board.c`, tighten the current GPIO ownership seams rather than allowing silent multi-owner control. Centralize pin-mode restoration for service overrides, PCN PWM, ToF illumination PWM, and shared-I2C recovery so that each path releases the pad back to the baseline firmware posture before a new owner takes it. Surface enough state through the GPIO inspector that Phase 1 validation can prove there is no lingering mode or pull-state damage after a handoff.

In `components/laser_controller/src/laser_controller_app.c` and `components/laser_controller/src/laser_controller_safety.c`, separate deployment state, runtime mode, and output-derivation rules more cleanly. Phase 1 must keep the current safe-off behavior whenever deployment is inactive, running, or failed. Runtime outputs must only use host-owned NIR/modulation requests in `modulated_host`. `binary_trigger` should exist as a runtime mode, but because the trigger path is unresolved it should not create a hidden host bypass or fake readiness. If deployment loses validity or a major fault occurs, force `PCN` low, `SBDN` low, and safe-off outputs immediately.

The third milestone refactors the host GUI. In `host-console/src/App.tsx`, remove the dedicated Deployment nav item and stop rendering the Deployment page as a standalone workspace. Keep the file if helpful, but render the deployment workflow inline inside `host-console/src/components/ControlWorkbench.tsx`. The Control page must show deployment entry/exit, checklist status, failure reason, target/safety edits, runtime-mode selection, and explicit reasons why the checklist cannot complete on the USB-only Phase 1 bench.

In `host-console/src/types.ts`, `host-console/src/lib/live-telemetry.ts`, `host-console/src/hooks/use-device-session.ts`, and `host-console/src/lib/mock-transport.ts`, add the runtime-mode fields and the LD/TEC telemetry-validity flags so both full snapshots and high-rate telemetry know when to render OFF or INVALID. The mock transport must model the new mode rules and the USB-only Phase 1 limitation so the rendered UI can be exercised before powered hardware exists.

Still in the host, rework Bring-up behavior in `host-console/src/components/BringupWorkbench.tsx`, `host-console/src/lib/bringup.ts`, and `host-console/src/components/ModuleReadinessPanel.tsx`. When deployment is active, keep the module cards visibly connected and healthy-looking where deployment-owned monitoring is still live, but disable all writes with specific reasons and hover help. Deterministic-readback modules must move into an error state after missing readback persists beyond a short grace window. LD and TEC presence/readiness must be based on rail `PGOOD` and telemetry-validity rules, not on `LPGD` or `TEMPGD` alone. Remove the old standby-HiZ framing from the laser bring-up page because Phase 1 no longer treats it as the normal control posture.

The final milestone is validation and review. Build the firmware and host, run the serial validation script and any Phase 1 extensions that were added, run the host app locally and inspect rendered pages, then use a separate review-only pass to look for safety conflicts, GPIO ownership defects, GUI lock-state mistakes, and overclaimed validation. If issues are found, loop back into implementation and update this document plus `Status.md`.

## Concrete Steps

From the repository root, perform the rewrite in this order:

    sed -n '1,260p' .agent/AGENT.md
    sed -n '1,260p' .agent/runs/v2-rewrite-seed/ExecPlan.md
    sed -n '1,260p' .agent/runs/v2-rewrite-seed/Status.md

    sed -n '1,260p' components/laser_controller/include/laser_controller_bench.h
    sed -n '1,340p' components/laser_controller/src/laser_controller_bench.c
    sed -n '1340,1760p' components/laser_controller/src/laser_controller_comms.c
    sed -n '1350,1765p' components/laser_controller/src/laser_controller_app.c
    sed -n '3810,3995p' components/laser_controller/src/laser_controller_board.c

    sed -n '1,280p' host-console/src/App.tsx
    sed -n '1,340p' host-console/src/types.ts
    sed -n '1,340p' host-console/src/components/ControlWorkbench.tsx
    sed -n '1,340p' host-console/src/components/DeploymentWorkbench.tsx
    sed -n '1,260p' host-console/src/lib/bringup.ts
    sed -n '1,260p' host-console/src/lib/mock-transport.ts

Then implement the code changes described above. After the code compiles, run:

    . "$IDF_PATH/export.sh" && idf.py build
    cd host-console && npm run build

For serial validation on the current USB-only bench, use:

    python3 host-console/scripts/live_controller_validation.py --help

Extend the validation script as needed so it proves the Phase 1 runtime-mode gating and the no-false-ready behavior on USB-only power.

For rendered GUI validation, from `host-console/`:

    npm run dev

Open the Vite URL in a browser automation session and verify:

- Control shows the inline deployment workflow
- there is no separate Deployment workspace in nav
- Control explains why the checklist cannot complete on USB-only power
- Bring-up writes are locked during deployment with explicit reasons
- LD and TEC telemetry render OFF / INVALID / IN-DEPLOYMENT correctly
- runtime-mode switching and host control buttons respect the current mode

Expected Phase 1 result:

    The firmware and host build successfully.
    The GUI no longer exposes a standalone Deployment page.
    Host runtime controls are accepted only in modulated_host mode.
    The USB-only bench never reports a false deployment-ready state.

## Validation and Acceptance

Phase 1 acceptance is behavioral and must be proved with build output, serial evidence, rendered-page inspection, and a separate review pass.

Firmware acceptance:

- `idf.py build` succeeds.
- The command protocol accepts `set_runtime_mode`.
- Host-controlled runtime enable and modulation commands are rejected in `binary_trigger`.
- Host-controlled runtime enable and modulation commands are accepted only in `modulated_host` when deployment preconditions are met.
- `SBDN`, `PCN`, `PWR_TEC_EN`, and `PWR_LD_EN` remain in safe-off postures on the USB-only bench.
- LD and TEC telemetry are marked invalid when their power/control prerequisites are not met.
- No powered-hardware checklist step can falsely report success on the USB-only bench.

GUI acceptance:

- `npm run build` succeeds.
- The nav no longer includes a standalone Deployment page.
- Control renders deployment entry/checklist/status inline.
- Control clearly explains the USB-only Phase 1 limitation.
- Bring-up keeps deployment-owned modules visible instead of looking disconnected, while write controls are disabled with reasons.
- LD and TEC readings render OFF / INVALID / IN-DEPLOYMENT rather than floating values.

Review acceptance:

- a separate review-only pass runs after the milestone
- any findings are either fixed in the same loop or explicitly recorded in `Status.md`

Phase 2 acceptance is not in scope for completion now. This plan must explicitly leave these as blocked:

- STUSB4500 / PD validation
- TEC-first rail sequencing on real powered hardware
- LD shutdown on TEC loss under real powered hardware
- deployment-ready completion on a powered bench
- actual laser enable
- final physical-trigger validation

## Idempotence and Recovery

The doc updates are safe to reapply by editing the same files. The firmware and host changes are intended to be additive and refactoring-safe. If a code change breaks validation, restore behavior by re-running the build and serial checks immediately rather than papering over the regression in UI text. If the runtime-mode or inline-deployment refactor becomes unstable, keep the protocol and snapshot schema consistent and roll the UI and firmware back together rather than splitting their assumptions.

Because this is a life-critical controller, any uncertainty about hardware truth, trigger wiring, or rail behavior is a stop condition for that specific path. Record the blocker in `Status.md`, keep the rest of the milestone moving, and do not silently infer missing hardware behavior.

## Artifacts and Notes

Important reference files:

    .agent/AGENT.md
    docs/hardware-recon.md
    docs/firmware-pinmap.md
    docs/datasheet-programming-notes.md
    docs/protocol-spec.md
    host-console/scripts/live_controller_validation.py

Important hardware facts to preserve:

    ATLS6A214:
      SBDN low = shutdown
      SBDN high = operate
      PCN low selects LISL, which is hard-grounded on this board
      PCN high selects LISH

    Current USB-only bench:
      programming link is available
      PD / TEC / LD powered validation is not available

## Interfaces and Dependencies

The firmware must define a stable runtime-mode interface reachable from the host:

    enum laser_controller_runtime_mode_t {
        LASER_CONTROLLER_RUNTIME_MODE_BINARY_TRIGGER = 0,
        LASER_CONTROLLER_RUNTIME_MODE_MODULATED_HOST,
    };

The bench/runtime status must carry:

    runtime mode
    whether mode switching is currently allowed
    why mode switching is blocked when blocked

The hardware input snapshot must carry:

    bool ld_telemetry_valid;
    bool tec_telemetry_valid;

The host snapshot types must expose matching fields so UI rendering can distinguish live hardware truth from OFF / INVALID states.

The protocol command surface must include:

    set_runtime_mode { "mode": "binary_trigger" | "modulated_host" }

Existing deployment commands remain in place for Phase 1. Existing host runtime commands remain present, but they must be gated by the active runtime mode.

Change note: This document replaces the previous seed-only ExecPlan because the rewrite is now active and the seed file was no longer sufficient for implementation safety.
