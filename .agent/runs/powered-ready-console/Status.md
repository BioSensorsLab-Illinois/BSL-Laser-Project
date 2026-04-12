# Powered-Ready Console Rewrite Status

## Current State

- New initiative created to replace the legacy USB-only rewrite seed for active powered-ready work.
- Host rewrite is landed and building.
- Firmware command aliases and the current powered-ready deployment semantics are landed and building.
- Powered-ready recovery patch is now in progress on top of the landed rewrite:
  - deployment-entry stale-output replay fix is implemented in local source
  - split `fault.active*` / `fault.latched*` status model is implemented in local source
  - `operate.set_alignment` / `operate.set_led` are now deployment-active aux controls in local source
  - host `Operate` has been rebuilt around linked temp/wavelength controls and explicit requested/applied/owner state
  - validation harness now supports both serial and WebSocket transports in local source

## Completed

- New initiative folder created.
- Existing host, firmware, mock, protocol, and validation seams were inspected and the powered-ready target was grounded in code.
- Host shell rewritten to the new five-workspace layout:
  - `System`
  - `Operate`
  - `Integrate`
  - `Update`
  - `History`
- New `Operate` workspace replaced the old Control/Deployment composition with a compact checklist, bottom-mounted deployment log, and runtime controls that use the new command family.
- Domain/platform TypeScript landing zone now builds cleanly.
- Firmware command aliases added for:
  - `status.get`
  - `deployment.enter`
  - `deployment.exit`
  - `deployment.run`
  - `deployment.set_target`
  - `operate.set_mode`
  - `operate.set_target`
  - `operate.set_output`
  - `operate.set_modulation`
  - `integrate.set_safety`
  - `integrate.save_profile`
- Firmware deployment payload now includes:
  - `sequenceId`
  - `currentStepKey`
  - `currentStepIndex`
  - `lastCompletedStepKey`
  - step `startedAtMs`
  - step `completedAtMs`
- Integrate safety apply now routes through the persistent command path and the bring-up UI copy reflects automatic persistence.
- Validation script updated to the new command family and asynchronous deployment flow.
- Post-ready deployment failure handling changed so a late invalidation now drops the LD path safe without reflexively deasserting TEC enable.
- GPIO6 / ToF LED sideband is now driven low during board safe-default initialization instead of being left floating until later service-path setup.
- STUSB4500 polling is now permission-gated:
  - normal board-input reads no longer poll the PD chip continuously
  - boot-time polling is only armed when firmware PDO auto-reconcile is enabled
  - explicit `refresh_pd_status` is rejected while deployment mode is active
- Firmware PDO auto-reconcile is now one-shot at boot instead of a recurring background loop.
- Public PD snapshot metadata now includes PD freshness/source fields for the host:
  - `lastUpdatedMs`
  - `snapshotFresh`
  - `source`
- Operate UI copy now explicitly says PD state is passive-only and that any explicit refresh or PDO action belongs to Integrate.
- Repo rule added: every firmware logic change now requires both a full firmware logic audit and a full GUI logic audit.
- Firmware logic audit completed for this change:
  - reviewed boot reconcile scheduling
  - reviewed deployment `pd_inspect` semantics
  - reviewed post-ready invalidation behavior
  - reviewed GPIO6 ownership and sideband enforcement
- GUI logic audit completed for this change:
  - confirmed `Operate` has no PD refresh or PDO-write path
  - confirmed explicit PD controls remain in `BringupWorkbench` / Integrate only
  - confirmed Operate copy now identifies PD state as passive-only
- Additional control-page update completed:
  - Operate now exposes a 0.0-5.2 A constant NIR current slider
  - Operate now exposes green-laser runtime control
  - Operate now exposes GPIO6 LED brightness control
  - deployment target editing was removed from Operate and the page now assumes the default 25 C deployment target
- Deployment-v2 replacement completed in the active path:
  - Operate now renders a wizard layout with a narrow checklist rail, single active-step panel, compact runtime cards, and a bottom causal log
  - deployment telemetry now exposes phase, ready-idle truth, primary failure, and secondary effects directly
  - firmware now treats ready-idle low-current bias below `off_current_threshold_a` as intentional
  - default `off_current_threshold_a` is now `0.2 A`
  - deployment entry explicitly drives GPIO6 low before checklist start
  - green laser and GPIO6 LED remain operator controls in Operate instead of being deployment-gated
- Firmware logic audit completed for the control-page update:
  - reviewed alignment request flow from bench state into safety/output derivation
  - reviewed GPIO6 sideband ownership between runtime and service
  - reviewed 5.2 A clamp changes in config, bench state, and output command handling
- GUI logic audit completed for the control-page update:
  - verified the checklist now uses a narrower column with less horizontal waste
  - verified auxiliary light controls moved from Bring-up into Operate
  - verified deployment target editing was removed from Operate

## In Progress

- Live hardware confirmation is still needed for the ready-idle contradiction fix, the deployment-entry GPIO6-off behavior, and the new causal fault attribution path.
- Live hardware confirmation is also still needed for the new recovery patch because the current session has no serial flash path to push the rebuilt firmware image onto the Wi-Fi-connected board.

## Next Step

- Flash the updated image and confirm on real hardware that:
  - deployment entry no longer causes PD loss or reboot
  - GPIO6 stays low on deployment entry
  - deployment uses passive PD reads successfully without requiring a manual Integrate refresh first

## Blockers

- No build blocker is open.
- Hardware validation blocker:
  - local firmware image builds successfully, but there is no serial `/dev/cu.usbmodem*` path exposed in this session, so the rebuilt firmware cannot be flashed onto the live Wi-Fi board from here yet

## Validation

- Rendered snapshots captured from the current app:
  - overview
  - control
  - bring-up
- Rendered snapshots captured from the rewritten app:
  - `/tmp/rewrite-system.png`
  - `/tmp/rewrite-operate.png`
  - `/tmp/rewrite-integrate.png`
  - `/tmp/rewrite-operate-running.png`
- Host build result:
  - `cd host-console && npm run build` passed
- Updated host build result after the recovery patch:
  - `cd host-console && npm run build` passed
- Rendered verification:
  - `/tmp/operate-v2-page.png` captured from the new Operate wizard
- Updated rendered verification after the recovery patch:
  - `/tmp/recovery-system-final.png`
  - `/tmp/recovery-operate-final.png`
  - `/tmp/recovery-operate-final-lower.png`
  - verified first-launch Wi-Fi auto-connect behavior against `ws://192.168.4.1/ws`
  - verified no browser console errors beyond the normal Vite / React DevTools dev notices
- Mock runtime verification:
  - entering deployment from the new Operate page succeeded
  - `deployment.run` acknowledged immediately
  - checklist rows advanced live while the mock controller was still running
- Firmware build result:
  - `. /Users/zz4/esp/esp-idf/export.sh && idf.py build` passed
- Additional firmware bugfix validation:
  - post-ready TEC-hold patch compiled successfully
  - GPIO6-low boot patch compiled successfully
  - PD polling ownership patch compiled successfully
  - host PD cache metadata path compiled successfully
- Updated firmware build result after the recovery patch:
  - `. /Users/zz4/esp/esp-idf/export.sh && idf.py build` passed
- Updated validation harness check:
  - `python -m py_compile host-console/scripts/live_controller_validation.py` passed
  - `python host-console/scripts/live_controller_validation.py --transport ws --ws-url ws://192.168.4.1/ws --scenario deployment-lockout` passed on the live Wi-Fi bench
  - `python host-console/scripts/live_controller_validation.py --transport ws --ws-url ws://192.168.4.1/ws --scenario deployment-lockout` passed against the current live bench firmware
- Remaining hardware-only validation:
  - confirm on live hardware that passive PD reads still work normally after the ownership patch
  - confirm deployment entry no longer causes PD loss or reboot
  - flash the rebuilt firmware onto the live board once a serial path is available
  - run `aux-control-pass`, `ready-runtime-pass`, and `fault-edge-pass` against the flashed firmware
- Session-forensics result:
  - the archive captured both the broken `pd_lost` / `lastUpdatedMs=0` state and a later corrected passive-read state with `source=\"cached\"`
  - treat that as evidence of a flashed-image mismatch across attempts, not a single stable firmware behavior

## Notes

- Treat `.agent/runs/v2-rewrite-seed/` as legacy context only.
- Record both powered-ready evidence and any remaining trigger-path limitations as the rewrite progresses.
