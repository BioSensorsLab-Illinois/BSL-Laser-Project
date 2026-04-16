# Execute the Powered-Ready Console and Contract Rewrite

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and `Outcomes & Retrospective` must be kept up to date as work proceeds.

This document must be maintained in accordance with `.agent/PLANS.md`.

## Purpose / Big Picture

After this rewrite, the host console is a clean powered-bench operator tool instead of a glossy dashboard glued onto the older bench model. Operators use five workspaces with a single runtime flow: `System`, `Operate`, `Integrate`, `Update`, and `History`. The `Operate` workspace owns deployment and runtime control. The deployment checklist updates live, stays compact, and only succeeds when the controller actually reaches ready posture with TEC and LD held in the correct post-checklist state.

The firmware and host use one updated command family. Integrate safety changes become persistent device settings and deployment automatically uses them after reboot. The legacy `.agent/runs/_archive/v2-rewrite-seed/` material remains as historical USB-only context, but it is no longer the source of truth for the active rewrite.

## Progress

- [x] (2026-04-06 11:50 America/Chicago) Created the powered-ready initiative folder and seeded run docs.
- [x] (2026-04-06 12:05 America/Chicago) Captured rendered evidence of the current host UI and confirmed the current Control and Bring-up pages are too tall, duplicate deployment/service concerns, and violate `Uncodixfy`.
- [x] (2026-04-06 12:12 America/Chicago) Confirmed the current firmware already publishes deployment steps in live telemetry and already persists runtime safety in service profile state.
- [x] (2026-04-06 12:34 America/Chicago) Replaced the active host shell with a five-workspace light console and a new `Operate` workspace that uses the powered-ready command family.
- [x] (2026-04-06 12:46 America/Chicago) Added firmware command aliases and deployment payload fields for the powered-ready flow.
- [x] (2026-04-06 13:22 America/Chicago) Patched the post-ready failure output path so late deployment invalidations force the LD path safe without reflexively turning TEC enable off, and drove GPIO6 low during board safe-default initialization.
- [x] (2026-04-06 13:35 America/Chicago) Locked STUSB4500 polling behind explicit permission so deployment mode no longer touches the PD chip in the background.
- [x] (2026-04-06 14:10 America/Chicago) Removed the recurring PD reconcile loop, exposed PD freshness/source metadata to the host, and updated docs plus validation scaffolding for the STUSB ownership boundary.
- [x] (2026-04-06 15:05 America/Chicago) Added Operate runtime controls for a 0.0-5.2 A constant NIR slider, green-laser toggling, and GPIO6 LED brightness, and compacted the checklist layout by removing deployment-target editing.
- [x] (2026-04-06 18:40 America/Chicago) Replaced the active deployment rendering path with a wizard-style Operate workspace driven by deployment-v2 truth fields, compact support cards, and a bottom causal log.
- [x] (2026-04-06 18:40 America/Chicago) Reworked firmware deployment supervision to publish ready-idle truth, primary vs secondary failure attribution, low-current ready-idle acceptance, and explicit deployment-entry GPIO6 shutdown.
- [ ] Persist integrate safety updates immediately and expose save timestamps in the snapshot.
- [x] (2026-04-06 18:40 America/Chicago) Updated protocol, architecture, validation, and active status docs for deployment-v2 and the idle-bias threshold ownership change.
- [x] (2026-04-06 12:57 America/Chicago) Ran host build, rendered verification, and firmware build after the rewrite cutover.
- [x] (2026-04-14) Fixed firmware LED flicker: `capture_tof_readback` now passes `any_owns_tof_sideband()` instead of `service_owns_tof_sideband`; `set_runtime_tof_illumination` bails out when service owns the sideband. Parameter renamed to `anyone_owns_sideband` to prevent recurrence.
- [x] (2026-04-14) Implemented SBDN three-state driver on GPIO13 per ATLS6A214 datasheet (OFF=drive LOW, ON=drive HIGH, STANDBY=input/Hi-Z via R27/R28 2.25 V). Replaced `bool assert_driver_standby` / `bool driver_standby_asserted` with `laser_controller_sbdn_state_t sbdn_state` in board.h + safety.h. Every fault path forces OFF (fast 20 us shutdown).
- [x] (2026-04-14) Fully ungated green alignment laser per user directive: `allow_alignment = true` unconditional in safety.c, LED-only gate in comms.c dispatcher, `enable_alignment_laser` honors request on every post-boot path in derive_outputs.
- [x] (2026-04-14) Firmware now publishes `bench.hostControlReadiness` with `nirBlockedReason`, `alignmentBlockedReason`, `ledBlockedReason`, `sbdnState`. GUI renders precise pre-disabled tooltip reasons without re-implementing the firmware gate ordering.
- [x] (2026-04-14) Host Operate page rewritten ground-up (`operate-v3` namespace): single cleaner file, debounced LED commit fixes the drag-time slider spam, green button is never software-disabled, SBDN state visible in the header and ready-truth grid.
- [x] (2026-04-14) Firmware-wide logic audit (bsl-firmware-auditor) PASS; GPIO ownership audit (bsl-gpio-auditor) PASS; ATLS6A214 datasheet check (bsl-hardware-safety) PASS.
- [x] (2026-04-14) Updated `docs/protocol-spec.md` to document alignment ungating, `hostControlReadiness`, and `sbdnState`.
- [ ] (2026-04-14) BLOCKED: Powered Phase 2 (`aux-control-pass`, `ready-runtime-pass`, `fault-edge-pass`, new `sbdn-tri-state-pass`) — requires flashing the rebuilt image on the live bench; no `/dev/cu.usbmodem*` available this session.
- [x] (2026-04-14, second round) Codified mandatory **Cross-Module Audit Fan-Out** policy in AGENT.md and added new skill `.agent/skills/cross-module-audit/SKILL.md`. Every firmware/host/protocol change now requires spawning the relevant subset of B1/B2/B3 + A1/A2/A3 + hardware-safety + docs-guardian and recording verdicts in Status.md before declaring done.
- [x] (2026-04-14, second round) Added new firmware module `laser_controller_usb_debug_mock` for online testing on USB-only power. Hard-isolated: opt-in only, NEVER drives any GPIO, auto-disables on real PD or any non-auto-clear fault, latches new SYSTEM_MAJOR fault `usb_debug_mock_pd_conflict` on PD conflict.
- [x] (2026-04-14, second round) New `derive_outputs` early-return for SYSTEM_MAJOR faults — closes a 1-tick hazard window where deployment-running READY_POSTURE could force `sbdn_state=ON` despite a fault.
- [x] (2026-04-14, second round) Host: `UsbDebugMockPanel` in Integrate workspace, app-wide warning banner in App.tsx, full type / mock / spec sync.
- [ ] Add `usb-debug-mock-pass` scenario to `host-console/scripts/live_controller_validation.py` so the enable gate hierarchy and PD-conflict latch can be regression-tested on real hardware.
- [ ] Run the updated validation scenarios on live hardware.

## Surprises & Discoveries

- Observation: The current host build was already failing before the rewrite because `src/domain/*` uses non-type imports under `verbatimModuleSyntax` and `src/platform/tauriBridge.ts` lacks module typings.
  Evidence: `npm run build` failed with TS1484 type-only import errors and TS2307 Tauri module errors.
- Observation: The current app still renders the exact UI style that `Uncodixfy` bans: dark glass surfaces, hero layout, eyebrow labels, oversized radii, and sticky decorative navigation.
  Evidence: rendered screenshots from `/tmp/bsl-overview-full.png`, `/tmp/bsl-control-full.png`, and `/tmp/bsl-bringup-full.png`.
- Observation: The firmware already streams deployment step arrays in `live_telemetry`, so the “all rows update only at the end” issue is likely a host rendering/state-flow problem, not a missing firmware field.
  Evidence: `components/laser_controller/src/laser_controller_comms.c` includes deployment steps in live telemetry JSON.
- Observation: Runtime safety is already stored in the service persisted profile and loaded at boot.
  Evidence: `laser_controller_service_save_profile`, `laser_controller_service_apply_persisted_locked`, and `laser_controller_app_start` already load and consume persisted safety thresholds/timeouts.
- Observation: The current host did not need a full transport rewrite to unlock the new operator flow immediately; replacing the app shell and the Operate workspace on top of the existing session hook was enough to land the five-workspace console while preserving serial, wireless, flash, export, and autosave behavior.
  Evidence: `host-console/src/App.tsx` now drives the new five-workspace shell while `use-device-session.ts` continues to supply the transport plumbing.
- Observation: The mock transport needed explicit asynchronous deployment progression; otherwise the new Operate page still looked live but the checklist could only jump from idle to terminal state.
  Evidence: after adding step progression in `host-console/src/lib/mock-transport.ts`, the running screenshot showed rows in `PASSED / IN PROGRESS / PENDING` simultaneously during checklist execution.
- Observation: The ready-invalidation path previously reused the same “all rails off” output branch as pre-ready deployment failure, so any late ready invalidation could deassert TEC even though the tracked product requirement only demands immediate LD shutdown.
  Evidence: `laser_controller_derive_outputs()` returned the all-off posture whenever `deployment.failed` was true, even after `last_completed_step == READY_POSTURE`.
- Observation: The board-input path was still polling STUSB4500 from `laser_controller_board_read_inputs()`, so deployment mode could touch the PD chip even when no explicit PD command was being issued.
  Evidence: `laser_controller_board_refresh_pd_snapshot()` was called unconditionally during every input refresh and performed live I2C reads whenever the poll interval elapsed.
- Observation: Making PD polling opt-in was not enough by itself because the app layer still had a recurring boot/runtime reconcile scheduler. That scheduler had to be collapsed to a one-shot boot window as well.
  Evidence: `laser_controller_maybe_reconcile_pd_runtime()` was still called from every slow cycle and could reschedule itself indefinitely.
- Observation: The user-provided session archives show two distinct PD behaviors in the field: one with `pd_lost` and `lastUpdatedMs=0`, and a later one with valid passive PD data and `source=\"cached\"`.
  Evidence: `/Users/zz4/Downloads/bsl-session-2026-04-06T18-44-38.008Z.json` and `/Users/zz4/Downloads/bsl-session-2026-04-06T18-50-22.549Z.json` differ materially in reported PD state despite the same nominal operator flow.
- Observation: The lowest-risk way to satisfy the new control-page request was to promote green and GPIO6 LED control into explicit Operate runtime commands, because the firmware already had an alignment request seam and the board layer already modeled GPIO6 illumination state.
  Evidence: `laser_controller_bench_set_alignment_requested()` already existed, while `BringupWorkbench.tsx` already contained service-only green and GPIO6 light controls.
- (2026-04-14) Observation: The LED flicker on Operate and the "flash-then-off" on Integrate were a single root cause, not two bugs. `capture_tof_readback` in board.c passed `service_owns_tof_sideband` (NOT `any_owns_tof_sideband()`) to `apply_tof_sideband_state`. Inside the locked function a FALSE parameter forces the sideband LOW. Every TOF poll (every few ms when the sensor is expected) stomped whatever the runtime setter had just written. The runtime setter was also missing the AGENT.md-required "bail out when service owns" guard, so in Integrate the control task's 5 ms tick was similarly stomping service writes.
  Evidence: board.c:2593 (original), and the absence of `laser_controller_board_service_owns_tof_sideband()` early-return at the top of `laser_controller_board_set_runtime_tof_illumination`.
- (2026-04-14) Observation: The ATLS6A214 SBDN pin is datasheet-documented as a three-state input (datasheet p.2 Table 1: 0..0.4 V shutdown, 2.1..2.4 V standby, 2.6..14 V operate). The MainPCB already has an R27=13k7 / R28=29k4 divider that produces 2.25 V on Hi-Z, squarely in the standby band. The previous firmware bool-only model was hardware-incorrect.
  Evidence: `docs/Datasheets/ATLS6A214D-3.pdf` p.2-7 and `docs/Schematics/MainPCB.NET` lines 473-481.
- (2026-04-14) Observation: `driver_operate_expected` (app.c) must be TRUE only when SBDN is ON — STANDBY is idle, not operate. Using `!assert_driver_standby` in the three-state world would incorrectly flag STANDBY as "operate expected" and could trigger the `FAULT_UNEXPECTED_CURRENT` heuristic in safety.c. Tightened to `sbdn_state == ON && !select_driver_low_current`.
  Evidence: safety.c:300-312 (UNEXPECTED_CURRENT gate) and app.c:1965-1967 (snapshot population).

## Decision Log

- Decision: Use `.agent/runs/powered-ready-console/` as the new active rewrite initiative and keep `.agent/runs/_archive/v2-rewrite-seed/` as legacy context.
  Rationale: The new target explicitly replaces the USB-only safe-fail rewrite assumptions with powered-ready deployment acceptance.
  Date/Author: 2026-04-06 / Codex
- Decision: Keep existing low-level transport plumbing where it is already correct, but move the active host UI and parser/state surface onto the `domain/*` and `platform/*` path.
  Rationale: The current `use-device-session` path is too large and too coupled to the old page model.
  Date/Author: 2026-04-06 / Codex
- Decision: Treat integrate safety persistence as a device-side action that happens as part of `integrate.set_safety`, not as a separate optional operator save.
  Rationale: The user requirement is persistence across boots with automatic deployment sync, so a separate manual save would keep the broken behavior alive.
  Date/Author: 2026-04-06 / Codex
- Decision: Land the five-workspace console on top of the existing host transport/session hook in this pass, while still cleaning the `domain/*` and `platform/*` landing zone so a later reducer cutover remains viable.
  Rationale: This keeps the rewrite moving and preserves working serial/wireless/Tauri behavior instead of stalling on a second infrastructure migration.
  Date/Author: 2026-04-06 / Codex
- Decision: Special-case post-ready deployment invalidation to keep TEC enable asserted while forcing LD safe, instead of collapsing back to the pre-ready all-off posture.
  Rationale: This matches the repo’s TEC-first / LD-safe requirement and directly addresses the observed field failure where TEC dropped after a nominal checklist pass.
  Date/Author: 2026-04-06 / Codex
- Decision: Make PD polling opt-in instead of periodic, with boot-time refresh armed only when firmware PDO auto-reconcile is enabled.
  Rationale: The STUSB4500 must not be touched during deployment mode except for the narrow boot-time firmware-owned reconcile case or explicit integrate-page PD actions.
  Date/Author: 2026-04-06 / Codex
- Decision: Expose `pd.lastUpdatedMs`, `pd.snapshotFresh`, and `pd.source` to the host and keep Operate read-only for passive PD state.
  Rationale: The operator still needs to understand PD freshness and ownership, but the UI must not silently trigger explicit STUSB refreshes or writes to answer that question.
  Date/Author: 2026-04-06 / Codex
- Decision: Keep green laser as a runtime bench/alignment request and add a separate runtime-owned GPIO6 LED brightness path instead of reusing service-mode overrides from Bring-up.
  Rationale: This satisfies the control-page feature request while preserving clear ownership boundaries between Operate and Integrate.
  Date/Author: 2026-04-06 / Codex
- Decision: Treat ready-idle LD bias below `off_current_threshold_a` as intentional deployment posture, and set the default threshold to `0.2 A` from the Integrate safety page.
  Rationale: The hardware intentionally carries small current with `SBDN` high and `PCN` low after ready qualification, so that state must not be reported as unexpected current.
  Date/Author: 2026-04-06 / Codex
- Decision: Keep green laser and GPIO6 LED operator-visible in Operate regardless of deployment readiness, while forcing GPIO6 low on deployment entry before any checklist step begins.
  Rationale: The user wants both controls available at any time, but deployment entry itself must never spuriously turn the LED on.
  Date/Author: 2026-04-06 / Codex
- Decision: Replace the `bool assert_driver_standby` / `bool driver_standby_asserted` fields with a three-state `laser_controller_sbdn_state_t { OFF, ON, STANDBY }` and teach the board layer to reconfigure GPIO13 between INPUT_OUTPUT and INPUT modes at runtime.
  Rationale: User-confirmed (and datasheet-confirmed) semantics require three states. OFF (drive LOW) is the datasheet fast-shutdown path; STANDBY (Hi-Z) is the idle-armed posture used in ready-idle with no NIR; ON (drive HIGH) is the active operate posture.
  Date/Author: 2026-04-14 / Claude (opus-4-6)
- Decision: Every fault / safe-off / service-mode path forces `sbdn_state = OFF`, never STANDBY. Hi-Z still draws 8 mA idle current and takes 20 ms to resume operate; drive-LOW takes 20 us for full shutdown per datasheet. STANDBY is ONLY used in the deliberate ready-idle-no-NIR path.
  Rationale: hardware-safety agent invariant from the ATLS6A214 datasheet audit. Flagged as PASS in the firmware-wide audit; treating STANDBY as the safe-on-fault posture would violate the datasheet fast-shutdown spec.
  Date/Author: 2026-04-14 / Claude
- Decision: Fully ungate green alignment laser at the software level per explicit user directive 2026-04-14 ("safe to activate at ALL TIME, no interlock for it at all"). `allow_alignment = true` unconditional in safety.c; `is_aux_control_command` renamed to `is_led_control_command` so only LED retains the deployment gate; every `derive_outputs` path after `boot_complete` honors `alignment_output_enable`.
  Rationale: Green is inherently eye-safe at this product's power levels. The user accepted the residual GPIO37 shared-net hazard (ERM_TRIG + GN_LD_EN); the bsl-gpio-auditor confirmed this is bounded by existing `enable_haptic_driver` service-mode gating and `ERM_EN=LOW` outside service mode keeps DRV2605 quiescent.
  Date/Author: 2026-04-14 / Claude
- Decision: Publish `bench.hostControlReadiness` ({nir,alignment,led}BlockedReason + sbdnState) from firmware instead of having the GUI re-compute gate ordering client-side.
  Rationale: Avoids mock-vs-firmware drift. Single source of truth for "why is this button disabled right now." Host reads the token and maps to a display string via a small label helper.
  Date/Author: 2026-04-14 / Claude
- Decision: Debounce the LED brightness commit 200 ms trailing on the GUI side, instead of firing `operate.set_led` on every slider mouseup/touchend/blur.
  Rationale: Dragging previously spammed 5-10 commands per second, and the `enabled` field of each intermediate commit was recomputed from the live snapshot before the firmware had acknowledged the prior write — causing visible LED flicker independent from the firmware-side flicker that the sideband fix resolves.
  Date/Author: 2026-04-14 / Claude
- Decision: Transfer GPIO7 ownership from the VL53L1X data-ready interrupt to the MCP23017 INTA (open-drain, active-low). Move ToF data freshness to polling-only via the `RANGE_STATUS` register on the existing 75 ms intermeasurement cadence.
  Rationale: The button board physically shares the J2 connector with the ToF daughterboard; the connector exports a single GPIO7 pin, so only one source can own the line. Per the user directive 2026-04-15, the button board is the new owner. Polling-only ToF is acceptable because the distance interlock is a slow-moving safety check (no need for sub-100 ms latency).
  Date/Author: 2026-04-15 / Claude
- Decision: Gate button-driven NIR/alignment requests on `runtime_mode == BINARY_TRIGGER` and `inputs.button.board_reachable`. In `MODULATED_HOST` mode the buttons are advisory telemetry only — host requests are the truth. Resolves the prior inconsistency between the runtime gate (which silently honored buttons in any mode) and the published `nir_blocked_reason` (which said "not-modulated-host" when not in host mode).
  Rationale: A clean dual-source model with explicit ownership prevents racy "two operators trying to drive NIR" failure modes and matches the documented gate-reason taxonomy. The user-facing semantics are: switch to "Trigger buttons" in Operate to use the physical switches; switch to "Host control" to drive NIR from the GUI.
  Date/Author: 2026-04-15 / Claude
- Decision: Implement a press-and-hold lockout (`button_nir_lockout`) that latches when an interlock fires while either trigger stage is held, and clears only when both stages release on the same control-task tick. While latched, NIR requests from buttons are forced false. Alignment is unaffected (green has no interlock).
  Rationale: Per user directive 2026-04-15, an auto-clearing interlock should NOT cause NIR to immediately re-fire mid-press. The operator must physically release and re-engage. This adds one bit of state to the control task; no new fault code is needed because the underlying interlock fault remains the source-of-truth.
  Date/Author: 2026-04-15 / Claude
- Decision: Add a new deployment checklist step `LP_GOOD_CHECK` between `TEC_SETTLE` and `READY_POSTURE` that drives SBDN to ON with PCN low and waits up to 1 s for `LD_LPGD` to assert. Failure raises a new SYSTEM_MAJOR fault `LD_LP_GOOD_TIMEOUT` and aborts the deployment with that as the primary failure code.
  Rationale: Per user directive 2026-04-15, READY_POSTURE could declare ready-idle on a driver that cannot actually lock its current loop. The new step is a "loop-lock at zero drive" verification before any current is requested. The 1 s timeout is fixed in firmware (not in `config->timeouts`) so an operator cannot inadvertently relax it.
  Date/Author: 2026-04-15 / Claude
- Decision: Compute the RGB status LED color in firmware (priority: integrate-test override > unrecoverable flash-red > recoverable flash-orange > pre-deployment off > NIR firing solid-red > stage1+rails+temp armed solid-green > ready solid-blue) and publish the resulting state via the snapshot. Host renders firmware-decided color without recomputing.
  Rationale: Avoids host/firmware drift on the gate ordering. Matches the precedent set by `hostControlReadiness` on 2026-04-14. The integrate-test override path (`integrate.rgb_led.set` / `integrate.rgb_led.clear`) is service-mode-only with a watchdog window so the LED always returns to firmware control.
  Date/Author: 2026-04-15 / Claude
- Decision: Use TLC59116 hardware group-blink (GRPFREQ=23 → 1 Hz period, GRPPWM=128 → 50% duty, MODE2.DMBLNK=1) for the flashing states instead of toggling MODE2 from the 5 ms control tick. Keeps blink phase deterministic across reconnects and free of jitter from control-task scheduling.
  Rationale: User preference 2026-04-15 + hardware capability already exists. Costs zero ongoing bus traffic — only a single MODE2 byte write at color-state transitions.
  Date/Author: 2026-04-15 / Claude

## Outcomes & Retrospective

- Outcome: The active host shell, Operate workspace, command aliases, mock progression, and validation script were rewritten in code and both the host and firmware now build.
  Gap: Repo-level docs still need final wording alignment and the updated validation script still needs to be exercised on live hardware.

## Context and Orientation

The host application lives in `host-console/`. There are two parallel host models in the tree today:

- the current active path:
  - `host-console/src/App.tsx`
  - `host-console/src/hooks/use-device-session.ts`
  - `host-console/src/types.ts`
  - the large workbench components under `host-console/src/components/`
- the newer but not yet active path:
  - `host-console/src/domain/model.ts`
  - `host-console/src/domain/protocol.ts`
  - `host-console/src/domain/mock.ts`
  - `host-console/src/platform/bridge.ts`
  - `host-console/src/platform/browserMock.ts`
  - `host-console/src/platform/tauriBridge.ts`

The firmware lives in `components/laser_controller/`. The active control loop is `components/laser_controller/src/laser_controller_app.c`. Host commands and JSON envelopes are handled in `components/laser_controller/src/laser_controller_comms.c`. Device-side integrate persistence is implemented in `components/laser_controller/src/laser_controller_service.c`.

Terms used in this plan:

- powered-ready deployment: a successful deployment terminal state where TEC and LD rails are up, the controller has validated the ready posture, and idle-ready runtime handoff is complete
- operate: the runtime control domain for deployment, mode switching, output requests, and runtime setpoints
- integrate: the service domain for persistent safety policy, module/tuning configuration, and bring-up tools

## Plan of Work

Start by making the host compile on the `domain/*` and `platform/*` path. Convert type imports to `import type` where required, add minimal Tauri module declarations for host TypeScript, and remove unused imports that block the build. Do not spend time polishing the old `App.tsx`; the goal is to unblock the rewrite landing zone.

Next, replace the active host shell in `host-console/src/App.tsx` and create a new state hook for the five-workspace console. The new app should use a reducer around `WorkbenchState`, interpret protocol lines through `host-console/src/domain/protocol.ts`, and drive mock or Tauri bridge events through `host-console/src/platform/bridge.ts`. Where browser serial or wireless support is still needed, extend the bridge or provide a browser adapter, but keep the active UI on the new domain model rather than the old `types.ts` snapshot flow.

Then rewrite the UI as five workspaces with straightforward light surfaces and compact sections. `Operate` must show:

- compact deployment target editor
- compact live checklist with step timestamps and statuses
- runtime mode selector
- runtime output controls
- read-only safety summary
- deployment log at the bottom of the page

`Integrate` must show:

- service mode state
- persistent safety policy editor
- profile state including last saved time
- module configuration and tuning tools

After the host cutover, update firmware commands. Add new command aliases under `status.*`, `deployment.*`, `operate.*`, and `integrate.*`. Keep old commands only as temporary compatibility shims during migration. Change `deployment.run` so it acknowledges start immediately instead of blocking until terminal completion. Publish live step changes through telemetry and events with `sequenceId`, `startedAtMs`, and `completedAtMs`.

Then fix deployment terminal behavior in firmware. A deployment sequence must fail unless the final ready-posture checks truly pass. Ready invalidation on TEC loss must immediately force the LD path safe. Integrate safety updates must save to NVS as part of the command flow and expose persistence timing in the snapshot.

Finally, update `docs/protocol-spec.md`, `docs/host-console-architecture.md`, `docs/validation-plan.md`, and `.agent/AGENT.md` so the new active rewrite target is explicit. Update `host-console/scripts/live_controller_validation.py` to the new command names and powered-ready expectations.

## Concrete Steps

From the repository root:

    sed -n '1,260p' .agent/AGENT.md
    sed -n '1,320p' .agent/runs/powered-ready-console/ExecPlan.md
    sed -n '1,260p' .agent/runs/powered-ready-console/Status.md

    sed -n '1,260p' host-console/src/domain/model.ts
    sed -n '1,260p' host-console/src/domain/protocol.ts
    sed -n '1,260p' host-console/src/platform/bridge.ts
    sed -n '1,260p' host-console/src/platform/browserMock.ts
    sed -n '1,260p' host-console/src/platform/tauriBridge.ts

    sed -n '1,260p' components/laser_controller/src/laser_controller_comms.c
    sed -n '1,260p' components/laser_controller/src/laser_controller_app.c
    sed -n '1,260p' components/laser_controller/src/laser_controller_service.c

Then implement the host rewrite, firmware aliases, deployment semantics, and docs updates.

Run:

    cd host-console && npm run build

For rendered verification:

    cd host-console && npm run dev -- --host 127.0.0.1 --port 4173

Capture `System`, `Operate`, and `Integrate` screenshots with Playwright or an equivalent real browser path.

For firmware:

    . "$IDF_PATH/export.sh" && idf.py build

For protocol validation:

    python3 host-console/scripts/live_controller_validation.py --help

Update the validation script to the new contract and run the powered-ready scenarios.

## Validation and Acceptance

Host acceptance:

- the new host build passes
- the active app uses the five-workspace model
- the rendered app is a restrained light console
- `Operate` shows a compact checklist and the deployment log at the bottom
- `Integrate` is the only safety-edit surface
- OFF / INVALID / deployment-owned states render correctly

Firmware acceptance:

- `deployment.run` acknowledges start without blocking the host UI
- step statuses stream live
- ready is only true after actual ready posture
- successful deployment leaves TEC and LD logically enabled
- ready invalidation on TEC loss immediately drops LD safe
- integrate safety updates survive reboot and are applied automatically

Validation-script acceptance:

- powered ready flow passes
- runtime-mode gating passes
- false-ready failure path passes
- ready invalidation on TEC loss passes
- persisted safety after reboot passes

## Idempotence and Recovery

The host rewrite should proceed in additive compatibility slices where practical. If a new command family breaks the host before firmware aliases are finished, keep old command names temporarily as shims rather than leaving the console unusable. If build failures appear in unrelated legacy files, either fix the blocking type surface or remove those files from the active compile path, but do not silently ship a half-compiled host.

For firmware, if asynchronous deployment publication destabilizes the host during migration, keep the terminal state polling helper only as a temporary fallback in validation scripts, not as the final runtime UX.

## Artifacts and Notes

Important evidence gathered before implementation:

    Current rendered screenshots:
      /tmp/bsl-overview-full.png
      /tmp/bsl-control-full.png
      /tmp/bsl-bringup-full.png

    Current host build failure:
      TS1484 type-only import violations in src/domain/*
      TS2307 missing Tauri typings in src/platform/tauriBridge.ts

Important existing firmware facts:

    Runtime safety is already persisted in service profile NVS.
    Deployment steps are already included in live telemetry.
    Current deployment success path already intends to leave TEC and LD enabled after ready.

## Interfaces and Dependencies

The rewritten host snapshot should end with these active control domains:

    deployment: {
      active: boolean
      running: boolean
      ready: boolean
      failed: boolean
      sequenceId: number
      currentStepKey: string
      currentStepIndex: number
      lastCompletedStepKey: string
      failureCode: string
      failureReason: string
      targetMode: "temp" | "lambda"
      targetTempC: number
      targetLambdaNm: number
      maxLaserCurrentA: number
      maxOpticalPowerW: number
      steps: Array<{
        key: string
        label: string
        status: "inactive" | "pending" | "in_progress" | "passed" | "failed"
        startedAtMs: number
        completedAtMs: number
      }>
    }

    operate: {
      targetMode: "temp" | "lambda"
      runtimeMode: "binary_trigger" | "modulated_host"
      runtimeModeSwitchAllowed: boolean
      runtimeModeLockReason: string
      requestedOutputEnabled: boolean
      modulationEnabled: boolean
      modulationFrequencyHz: number
      modulationDutyCyclePct: number
      lowStateCurrentA: number
    }

    integrate: {
      serviceModeRequested: boolean
      serviceModeActive: boolean
      profileName: string
      profileRevision: number
      persistenceDirty: boolean
      persistenceAvailable: boolean
      lastSaveOk: boolean
      lastSavedAtMs: number
      safety: {
        thresholds: ...
        timeouts: ...
      }
    }

The rewritten command surface must include at least:

    status.get
    deployment.enter
    deployment.exit
    deployment.run
    deployment.set_target
    operate.set_mode
    operate.set_target
    operate.set_output
    operate.set_modulation
    integrate.set_safety
    integrate.save_profile

Change note: This document creates the new active rewrite initiative for the powered-ready console because the prior USB-only seed run was no longer the correct implementation target.
