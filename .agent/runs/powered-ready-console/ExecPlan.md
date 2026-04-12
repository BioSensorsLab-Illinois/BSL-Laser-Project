# Execute the Powered-Ready Console and Contract Rewrite

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and `Outcomes & Retrospective` must be kept up to date as work proceeds.

This document must be maintained in accordance with `.agent/PLANS.md`.

## Purpose / Big Picture

After this rewrite, the host console is a clean powered-bench operator tool instead of a glossy dashboard glued onto the older bench model. Operators use five workspaces with a single runtime flow: `System`, `Operate`, `Integrate`, `Update`, and `History`. The `Operate` workspace owns deployment and runtime control. The deployment checklist updates live, stays compact, and only succeeds when the controller actually reaches ready posture with TEC and LD held in the correct post-checklist state.

The firmware and host use one updated command family. Integrate safety changes become persistent device settings and deployment automatically uses them after reboot. The legacy `.agent/runs/v2-rewrite-seed/` material remains as historical USB-only context, but it is no longer the source of truth for the active rewrite.

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

## Decision Log

- Decision: Use `.agent/runs/powered-ready-console/` as the new active rewrite initiative and keep `.agent/runs/v2-rewrite-seed/` as legacy context.
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
