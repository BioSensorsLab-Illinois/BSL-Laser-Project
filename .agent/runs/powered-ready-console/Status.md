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

## 2026-04-14 — Operate Console Rewrite + SBDN Tri-State + Green Ungate + LED Flicker Fix

### Completed (local source)

- Firmware LED flicker root-caused and fixed:
  - `capture_tof_readback` now passes `any_owns_tof_sideband()` instead of `service_owns_tof_sideband` (board.c `laser_controller_board.c:~2639`). Previously every TOF poll forced GPIO6 LOW, producing the Operate flicker and the Integrate "flash-on-then-off".
  - `apply_tof_sideband_state` parameter renamed from `service_owns_sideband` to `anyone_owns_sideband` to prevent the bug from recurring at any future call site.
  - `set_runtime_tof_illumination` now bails out when service owns the sideband, restoring the AGENT.md GPIO6 dual-driver rule (runtime must bail when service owns).
- Firmware green-laser software ungate landed per user directive "no interlock at all" (2026-04-14):
  - `is_aux_control_command` renamed to `is_led_control_command`; green bypasses the deployment gate entirely in `laser_controller_comms.c`.
  - `safety.c`: `allow_alignment = true` unconditional. Dead helper `laser_controller_power_allows_alignment` removed.
  - `app.c derive_outputs`: every path after `boot_complete` honors `decision->alignment_output_enable` (service mode, deployment running, post-ready invalidation, major-fault). Boot-only safe-off retained.
- Firmware SBDN three-state driver landed on GPIO13:
  - New enum `laser_controller_sbdn_state_t { OFF=0, ON=1, STANDBY=2 }` replaces the old `bool assert_driver_standby` / `bool driver_standby_asserted` in both the board outputs struct and the safety decision.
  - Apply path in `laser_controller_board_drive_safe_gpio_levels` switches GPIO13 between INPUT_OUTPUT+HIGH, INPUT_OUTPUT+LOW, and INPUT (Hi-Z) per datasheet Table 1. R27=13k7 / R28=29k4 external divider settles Hi-Z at 2.25 V inside the 2.1-2.4 V standby band.
  - All fault paths explicitly set `sbdn_state = OFF` (datasheet 20 us fast shutdown). Ready-idle-NIR-off sets `STANDBY`. Ready-idle-NIR-on sets `ON`.
- Firmware now publishes `bench.hostControlReadiness` with `nirBlockedReason`, `alignmentBlockedReason`, `ledBlockedReason`, and `sbdnState` so the GUI renders precise pre-disabled tooltips without re-implementing the firmware gate ordering.
- Host console Operate page rewritten from 1112-line `OperateConsole.tsx` to a new layout under `operate-v3` CSS namespace:
  - Single cleaner file; helper maps for `NirBlockedReason` / `LedBlockedReason` / `SbdnState` labels.
  - Green laser button: never software-disabled.
  - NIR / LED buttons: pre-disabled with `title=` tooltip showing the firmware's reason token label.
  - LED slider: debounced 200 ms trailing commit (fixes the UI-side flicker during drag; previously fired on every mouseup/touchend/blur).
  - SBDN state is visible in the status header and in the Ready-truth grid.
- Host types (`types.ts`) gained `SbdnState`, `NirBlockedReason`, `LedBlockedReason`, and `HostControlReadiness` — single source of tokens, referenced by both firmware (via the protocol spec) and mock.
- `mock-transport.ts` honors ungated green laser and publishes a mock `hostControlReadiness` with helper functions that mirror the firmware gate order.
- `protocol-spec.md` updated: alignment ungated, hostControlReadiness + sbdnState documented, legacy `driverStandby` bool retained as `sbdnState != "on"`.

### Validation (this session)

- Firmware build: `. $IDF_PATH/export.sh && idf.py build` passed.
- Host build: `cd host-console && npm run build` passed.
- Firmware logic audit (bsl-firmware-auditor): **PASS** — 21 occurrences of `sbdn_state` all written only from control-task paths; GPIO6 dual-driver rule restored; state-machine transitions unchanged; fault paths consistently force OFF (not STANDBY); `driver_operate_expected` tightened so STANDBY never triggers `LD_LOOP_BAD` or `UNEXPECTED_CURRENT`.
- GPIO ownership audit (bsl-gpio-auditor): **PASS** — single-writer, pulls-disabled, handoff-safe for GPIO13, GPIO6, GPIO37. GPIO37 shared-net hazard is bounded by existing `enable_haptic_driver` service-mode gating and explicitly accepted per user directive.
- ATLS6A214 datasheet check (bsl-hardware-safety): PASS — three-state SBDN is datasheet-sanctioned; Hi-Z = 2.25 V standby band; pulls must stay disabled.

### Blockers

- **Powered Phase 2 BLOCKED**. No `/dev/cu.usbmodem*` available in this session → cannot flash the rebuilt firmware → cannot run `aux-control-pass`, `ready-runtime-pass`, `fault-edge-pass`, nor the new `sbdn-tri-state-pass` scenario.
- Per the bsl-safety-critical output style "Ready" rule: the controller is **Not ready**. Powered Phase 2 has not been run on this image. Current evidence is: firmware build PASS, host build PASS, firmware-wide logic audit PASS, GPIO ownership audit PASS, ATLS6A214 datasheet check PASS, mock-runtime smoke PENDING (render-check).

### Next Step

- Flash the rebuilt image onto the live Wi-Fi-connected board once a serial path is available.
- Scope GPIO13 with oscilloscope during OFF→ON→STANDBY transitions to confirm the 2.25 V standby level on the assembled unit (datasheet ±1% tolerance calc).
- Run the four Powered Phase 2 scenarios: `aux-control-pass`, `ready-runtime-pass`, `fault-edge-pass`, and a new `sbdn-tri-state-pass` (to be authored in `host-console/scripts/live_controller_validation.py`).
- Record PASS/FAIL with captured waveforms.

## 2026-04-14 (later) — Cross-Module Audit Policy + USB-Debug Mock Layer

### Completed (local source)

- New non-negotiable policy section in `.agent/AGENT.md`: **Cross-Module Audit Fan-Out**. Every firmware/host/protocol change MUST spawn the relevant subset of B1/B2/B3 + A1/A2/A3 + hardware-safety + docs-guardian and record verdicts in Status.md before the change is declared done.
- New mandatory skill at `.agent/skills/cross-module-audit/SKILL.md` codifying the fan-out matrix, verdict-recording format, failure handling, and the `mark-audit-done.py` sentinel hygiene step.
- New section in AGENT.md "USB-Only Debug Power and the USB-Debug Mock Layer" documents the hardware reality (USB cannot bring up TEC/LD MPM3530 rails) and the seven hard-isolation guarantees of the new mock module.
- New firmware module `components/laser_controller/src/laser_controller_usb_debug_mock.c` (+ header) — closed-loop synthesis of TEC temp / TEC PGOOD / LD PGOOD / LIO; opt-in only; auto-disables on real PD or any non-auto-clear fault; NEVER drives any GPIO. Atomic request flags for cross-task signaling; control-task-owned activity state.
- New fault code `LASER_CONTROLLER_FAULT_USB_DEBUG_MOCK_PD_CONFLICT` (class SYSTEM_MAJOR) for when real PD power arrives while the mock is active.
- New comms commands `service.usb_debug_mock_enable` / `service.usb_debug_mock_disable` with full guard layering: comms-side (`service mode`, `power_tier`, `fault_latched`) + module-side (re-validate) + control-task (re-check before flipping `s_active`).
- New `derive_outputs` early-return: any SYSTEM_MAJOR fault forces safe outputs immediately, closing a 1-tick hazard window where deployment-running READY_POSTURE could otherwise force `sbdn_state=ON` despite a fault.
- `record_fault` now auto-disables the mock on any non-auto-clear fault transition.
- `clear_fault_latch` now also clears the mock's PD-conflict latch.
- New status field `bench.usbDebugMock = { active, pdConflictLatched, enablePending, activatedAtMs, deactivatedAtMs, lastDisableReason }`.
- Host types updated (`types.ts`): `UsbDebugMockStatus`, `BenchControlStatus.usbDebugMock`. Default in `bench-model.ts`. Deep-merge in `live-telemetry.ts`.
- Mock transport (`mock-transport.ts`) handles both new commands, mirrors firmware guards, auto-disables + latches on `pd_debug_config` power-tier upgrade, clears latch via `clear_faults`.
- New host component `host-console/src/components/UsbDebugMockPanel.tsx` rendered in the Integrate workspace's bringup tab. Uses only existing Uncodixfy primitives.
- App-wide banner in `host-console/src/App.tsx` whenever `bench.usbDebugMock.active` or `pdConflictLatched`. New CSS class `.usb-debug-mock-banner` (warning + critical variants) in `index.css`.
- `docs/protocol-spec.md` documents both new commands, the new bench block, and the new fault code.

### Validation (this session)

- Firmware build: `. $IDF_PATH/export.sh && idf.py build` PASS.
- Host build: `cd host-console && npm run build` PASS.

### Cross-Module Audit Evidence — 2026-04-14 — usb-debug-mock + cross-module-audit-policy

| Agent | Verdict | Notes |
|---|---|---|
| bsl-firmware-auditor (B1) — round 1 | FAIL → fixed | Found 4 issues: (1) `s_pd_conflict_latched` was written from comms task via `clear_pd_conflict_latch`, violating the declared control-task-owned threading model. (2) `derive_outputs` SYSTEM_MAJOR override didn't cover same-tick latched faults where `record_fault` sets latch BEFORE `safety_evaluate` runs (USB_DEBUG_MOCK_PD_CONFLICT path). (3) Fault docstring said SAFETY_LATCHED but actual class is SYSTEM_MAJOR. (4) Stale comment in `usb_debug_mock.h:134` referred to wrong call site. |
| bsl-firmware-auditor (B1) — re-audit | **PASS** | All 4 fixes verified. Threading model restored (atomic-flag handoff), derive_outputs predicate covers both `decision.fault_present` and `context.fault_latched` SYSTEM_MAJOR, docstring + stale comment corrected. No regressions. Firmware build PASS. |
| bsl-gpio-auditor (B2) | **PASS** | Mock writes ZERO GPIO peripheral calls — only `inputs->*` struct fields. Every ADC field substitution gated on the mock's matching rail-on flag (`enable_tec_vin`, `ld_pgood_synth`, `sbdn_operate`). PD-conflict same-tick SBDN-OFF confirmed via service-mode-only structural exclusion. GPIO6 dual-driver rule intact. Inferred forward-work note (extending derive_outputs to also check latched class) addressed by the B1-fix predicate above. |
| bsl-protocol-auditor (B3) | **PASS** | Four-place sync confirmed for both commands and all six `usbDebugMock` fields. Fault sync confirmed across enum/name/spec. Advisory: validation harness lacks `usb-debug-mock-pass` scenario — tracked as a deferred Next-Step. |
| bsl-uncodixfy-reviewer (A1) — round 1 | FAIL → fixed | Banner used `margin: 12px 24px 0` and read as a floating card. Fix: margin set to `0`. Decorative `letter-spacing` removed from `strong`. |
| bsl-uncodixfy-reviewer (A1) — re-audit | **PASS** | Banner now anchored full-width inside `.console-main` 16px padding. No glass / shadow / gradient. Within radius limit. Color tokens from project palette. |
| bsl-interaction-reviewer (A2) — round 1 | FAIL → fixed | Disable button missing `title` when disabled. Fix: explicit `title` for both `!connected` and `!mock.active` branches. |
| bsl-interaction-reviewer (A2) — re-audit | **PASS** | Confirmed `title` covers every disabled branch; no remaining gap. |
| bsl-docs-guardian | **PASS** | AGENT.md cross-refs resolve. Skill discoverable via README. New policy is additive to existing audit/review-loop rules. USB power claim consistent with hardware-recon.md. No conflicting policies. Two non-blocking flags: `INDEX.md` skill count and `SOP.md` regret-list entry — for user decision. |

**All 8 audits PASS**. Sentinels cleared via `mark-audit-done.py firmware` and `mark-audit-done.py gui` at 2026-04-14T22:49:33Z. Two non-blocking flags from docs-guardian addressed: `INDEX.md` skill count updated to 15, `SOP.md` regret-list grew an entry for skipping cross-module audit and forgetting `mark-audit-done.py`.

### Blockers

- **Powered Phase 2 STILL BLOCKED**. The mock layer enables online testing of the deployment / runtime / fault paths from a USB-only session, but powered-ready claims still require real bench power and the three mandatory passes (`aux-control-pass`, `ready-runtime-pass`, `fault-edge-pass`) plus the new `sbdn-tri-state-pass`. Mock evidence does NOT count for powered-ready.
- The validation harness `host-console/scripts/live_controller_validation.py` does not yet have a `usb-debug-mock-pass` scenario. Recommend adding before the next bench session so the mock can be regression-tested without regression risk to other scenarios.

### Next Step

- Read each background agent's verdict; address any FAIL by re-spawning only the affected agents.
- Run `python3 .claude/hooks/mark-audit-done.py firmware` and `python3 .claude/hooks/mark-audit-done.py gui` after all PASS to clear the DIRTY-tree session-start banners.
- Add the new `usb-debug-mock-pass` validation scenario.
- Flash and run real-bench validation when serial path is available.

## 2026-04-15 — Button Board Implementation (MCP23017 + TLC59116 + Binary-Trigger Operator Mode)

### Completed (local source)

- New firmware drivers landed:
  - `components/laser_controller/src/laser_controller_buttons.c` (+ header) — MCP23017 @ 0x20 driver. Open-drain INTA on GPIO7, control-task-only I2C, atomic ISR counter, fail-safe pressed-state-clear on consecutive bus failures.
  - `components/laser_controller/src/laser_controller_rgb_led.c` (+ header) — TLC59116 @ 0x60 driver. Hardware group blink at GRPFREQ=23 (1 Hz period) + GRPPWM=128 (50% duty). Dirty-compare keeps repeated identical applies bus-quiet. Non-standard B/R/G channel order on PWM0/1/2 documented.
- `components/laser_controller/include/laser_controller_pinmap.h` — `LASER_CONTROLLER_GPIO_TOF_GPIO1_INT` renamed to `LASER_CONTROLLER_GPIO_BUTTON_INTA`. Added `LASER_CONTROLLER_BUTTON_MCP23017_I2C_ADDR` and `LASER_CONTROLLER_BUTTON_TLC59116_I2C_ADDR`. Block comment documents the GPIO7 ownership transfer.
- `components/laser_controller/src/laser_controller_board.c` — GPIO7 ISR registered (negedge, internal pull-up enabled); `gpio_install_isr_service` idempotent. ToF GPIO1-level read replaced with `false` (polling-only). `read_inputs` now calls `laser_controller_buttons_refresh` and surfaces `buttons_readback` + `rgb_readback`. `apply_outputs` now applies the RGB LED via the new driver. Safe defaults init the buttons + RGB and drive RGB OFF.
- `components/laser_controller/include/laser_controller_types.h` — `laser_controller_button_state_t` extended with `side1/side2_pressed/edge`, `board_reachable`, `isr_fire_count`.
- `components/laser_controller/include/laser_controller_board.h` — `laser_controller_board_outputs_t` now carries `rgb_led`; `inputs` now carries `buttons_readback` + `rgb_readback`. `kSafeOutputs` initializes RGB OFF.
- `components/laser_controller/include/laser_controller_safety.h` — snapshot extended with `runtime_mode` and `button_nir_lockout`.
- `components/laser_controller/src/laser_controller_safety.c` — buttons routed to NIR/alignment ONLY when `runtime_mode == BINARY_TRIGGER && board_reachable && !allow_missing_buttons`. `button_nir_lockout` forces `request_nir = false` (alignment unaffected). ILLEGAL_BUTTON_STATE detection unchanged but now scoped to the same gate.
- `components/laser_controller/include/laser_controller_deployment.h` + `.c` — new step `LASER_CONTROLLER_DEPLOYMENT_STEP_LP_GOOD_CHECK` between `TEC_SETTLE` and `READY_POSTURE`. Step name `lp_good_check`, label "Driver loop-lock verification (SBDN → LP_GOOD ≤ 1 s)".
- `components/laser_controller/src/laser_controller_app.c`:
  - context fields added: `button_nir_lockout`, `button_led_brightness_pct`, `button_led_active`, `rgb_test_state`, `rgb_test_until_ms`. All control-task-owned.
  - `derive_outputs` extended with the new `LP_GOOD_CHECK` step output (TEC + LD on, SBDN ON, low-current).
  - `deployment_tick` extended with the LP_GOOD_CHECK case (1 s timeout, fails with `LD_LP_GOOD_TIMEOUT`).
  - New helper `laser_controller_apply_button_board_policy` runs after `safety_evaluate`. Updates lockout, steps LED brightness from side edges (default 20% on stage1 rising; clamped 0..100), detects MCP23017 loss in BINARY_TRIGGER (raises `BUTTON_BOARD_I2C_LOST` SYSTEM_MAJOR), and computes the RGB target color (flash red unrecoverable / flash orange recoverable / off / solid red firing / solid green armed / solid blue ready, with integrate-test override priority).
  - GPIO6 LED owner arbitration: when `button_led_active`, `set_runtime_tof_illumination` is called with the button-driven brightness instead of the bench slider value.
  - New public entry points `laser_controller_app_set_rgb_test` / `_clear_rgb_test` (control-task-only writes via `s_context_lock`).
  - `publish_runtime_status` copies the new `button_runtime` substruct (`nir_lockout`, `led_brightness_pct`, `led_owned`, `rgb_test_active`).
- `components/laser_controller/include/laser_controller_app.h` — new `button_runtime` substruct in runtime status.
- `components/laser_controller/include/laser_controller_faults.h` + `.c` — new fault codes `LD_LP_GOOD_TIMEOUT` and `BUTTON_BOARD_I2C_LOST`, both class `SYSTEM_MAJOR`.
- `components/laser_controller/src/laser_controller_comms.c`:
  - Extended `write_button_state_json` with side1/side2/board_reachable/isr_fire_count.
  - New `write_button_board_json` adds the top-level `buttonBoard` block (mcp/tlc reachability, ISR count, RGB target, LED brightness, lockout, trigger phase).
  - New helper `laser_controller_comms_trigger_phase` mirrors the firmware policy in app.c.
  - New commands `integrate.rgb_led.set` (gated on service mode + no deployment + no fault latch; bounded watchdog 1..30 s, default 5 s) and `integrate.rgb_led.clear` (always accepted while connected).
  - `operate.set_mode { mode: "binary_trigger" }` now allowed when `inputs.button.board_reachable == true`, otherwise rejected with explicit reason.
  - Service-mutation list extended with the two new commands so deployment-mode rejection works.
- `components/laser_controller/CMakeLists.txt` updated with the two new driver sources.
- Host:
  - `host-console/src/types.ts` — extended `ButtonRuntimeStatus`; added `RgbLedState`, `ButtonBoardStatus`, `TriggerPhase`. Added `'button_trigger'` to `appliedLedOwner`. New `buttonBoard` field on `DeviceSnapshot` and `RealtimeTelemetry`.
  - `host-console/src/lib/live-telemetry.ts` — three merge functions extended for the new `buttons` fields and the new `buttonBoard` block.
  - `host-console/src/hooks/use-device-session.ts` — default snapshot extended; merge function copies `buttonBoard`.
  - `host-console/src/lib/controller-protocol.ts` — binary-frame parser fills new buttons fields with safe defaults.
  - `host-console/src/lib/mock-transport.ts` — MockState extended with mcpReachable / tlcReachable / button mock fields. New `integrate.rgb_led.set` / `integrate.rgb_led.clear` handlers. `binary_trigger` mode-switch gated on `mcpReachable`. New `mockTriggerPhase` helper mirrors firmware. Snapshot output includes `buttonBoard`.
  - `host-console/src/components/ButtonBoardPanel.tsx` — new Integrate-workspace panel. MCP/TLC reachability, button live-state chips, ISR counter, trigger phase, LED brightness, RGB sliders + blink toggle + send/clear test buttons. All blocker reasons in `title=`. No new design primitives — uses `.panel-section`, `.summary-list`, `.action-button`, `.inline-help`.
  - `host-console/src/components/OperateConsole.tsx` — new `TriggerControlCard` rendered inside the Runtime setpoint section when `runtimeMode === 'binary_trigger'`. Read-only — firmware is the authority. Phase chip + 4 button chips + RGB indicator + LED brightness + lockout state. Trigger-buttons mode dropdown also disabled with explicit tooltip when `mcpReachable === false`.
  - `host-console/src/App.tsx` — `<ButtonBoardPanel/>` mounted in the Integrate bring-up tab next to `<UsbDebugMockPanel/>`.
  - `host-console/src/index.css` — new styles `.button-chip`, `.button-board-buttons`, `.rgb-test-grid`, `.rgb-target-preview`, `.rgb-indicator` (+ `.is-blinking` opacity-only animation, 1 s period), `.button-board-meta`, `.operate-trigger-card`. Uncodixfy compliant: no transforms, radii ≤ 8 px, opacity-only animations, existing color tokens.
- Docs:
  - `docs/firmware-pinmap.md` — new "Button board" section with the full register-map config + GPIO7 ownership transfer + 6-address bus enumeration. "Remaining Unknowns" trimmed to reflect that the trigger button wiring is now resolved.
  - `docs/protocol-spec.md` — new `buttonBoard` block, extended `buttons` block, two new commands, two new fault codes, `operate.set_mode` semantics change, the press-and-hold lockout contract, all under a "Button board (2026-04-15)" section.
  - `docs/hardware-recon.md` — addendum on the J2-shared connector + 6-address bus + GPIO7 transfer.
  - `docs/datasheet-programming-notes.md` — full programming rules for both new chips with citations.
  - `docs/validation-plan.md` — three new scenarios required before binary-trigger powered-ready: `button-trigger-pass`, `rgb-led-pass`, `sbdn-lp-good-pass`.
  - `.agent/AGENT.md` — `binary_trigger` unblock note replaces the old "blocked" requirement.

### Validation (this session)

- Firmware build: `. $IDF_PATH/export.sh && idf.py build` PASS.
  - Note: `bsl_laser_controller.bin` is now 0xf7650 bytes (3% free in the 0x100000 app partition). Worth widening the partition table soon.
- Host build: `cd host-console && npm run build` PASS.
- Render check via preview tools:
  - Mock transport switched, deployment checklist run, switched to binary_trigger.
  - Integrate workspace `<ButtonBoardPanel/>` renders correctly (header, MCP/TLC reachability, INTA fires counter, button chips, RGB sliders + blink toggle, test command buttons disabled with correct blocker tooltip).
  - Operate workspace `<TriggerControlCard/>` renders inside Runtime setpoint when binary_trigger is active (phase chip OFF, all 4 button chips IDLE, STATUS LED Off, FRONT LED 20%, NIR LOCKOUT Clear).
  - No browser console errors. No vite server errors.

### Cross-Module Audit Evidence — 2026-04-15 — button-board

| Agent | Round 1 | Notes |
|---|---|---|
| bsl-firmware-auditor (B1) | **PASS** | Threading, state machine, GPIO6 dual-driver, deployment + faults, command gating all hold. One non-blocking observation: `rgb_test_until_ms = 0U` clear inside the policy helper races with the `s_context_lock`-protected comms write — bounded race (worst case the LED test override clears one tick early), not safety-critical. Recommend taking the lock around the policy-side read in a future hardening pass. |
| bsl-gpio-auditor (B2) | **PASS** | GPIO7 single-owner = MCP23017 INTA; pullup + NEGEDGE configured at single site; ISR registration is idempotent; no new GPIO writes from button code; bus recovery posture intact; SBDN drive-HIGH during LP_GOOD_CHECK flows through existing `drive_safe_gpio_levels` single-owner path; ADC rail-gating unchanged. |
| bsl-protocol-auditor (B3) — round 1 | FAIL → fixed | (1) `appliedLedOwner: 'button_trigger'` was declared in types.ts but `laser_controller_comms_led_owner_name` never returned it. Fixed: added the `button_trigger` branch keyed on `status->button_runtime.led_owned` between `operate_runtime` and `deployment` priority. (2) Validation harness had zero coverage for the new commands and faults. Fixed: added three new scenarios (`button-trigger-pass`, `rgb-led-pass`, `sbdn-lp-good-pass`) to `host-console/scripts/live_controller_validation.py` plus argparse choices and dispatch entries. (3) Spec wording vs mock minor discrepancy on `hold_ms` lower bound (spec says `[1, 30000]`, mock allows 0 → default 5000). Functional behavior matches firmware; spec wording can be tightened in a follow-up. |
| bsl-protocol-auditor (B3) — round 2 | FAIL → fixed | Mock bench snapshot did not assign `appliedLedOwner` — the field was declared in `types.ts` and defaulted in `bench-model.ts` but the mock left it unset, so any `button_trigger` value was unreachable through the mock path. Fixed: added explicit `appliedLedOwner` and `appliedLedPinHigh` assignments to the mock bench snapshot mirroring the firmware priority order (`integrate_service > operate_runtime > button_trigger > deployment > none`). Other items confirmed in sync (firmware led_owner_name returns `button_trigger`, validation scenarios wired, types/docs agree). |
| bsl-protocol-auditor (B3) — round 3 | FAIL → fixed | Mock fix verified, but `appliedLedOwner` field was never documented in `docs/protocol-spec.md` (pre-existing gap exposed by the new `button_trigger` value). Fixed: added a dedicated section "`bench.appliedLedOwner` — telemetry field documented (2026-04-15)" with the full enum, priority table, and canonical-computation-site reference. |
| bsl-protocol-auditor (B3) — round 4 | **PASS** | Full four-place sync confirmed for `appliedLedOwner` including `button_trigger`. No remaining drift on commands, fields, faults, or gates. |

### Final result

**All 8 cross-module audit agents PASS** after iterative fix loops:
- B1, B2, A1, hardware-safety, docs-guardian — PASS round 1
- B3 — PASS round 4 (after led_owner branch + validation scenarios + mock appliedLedOwner + spec doc)
- A2 — PASS round 3 (after Trigger/Host toggle titles + RGB sliders + 8 pre-existing toolbar/setpoint titles)
- A3 — PASS round 2 (after CSS token rename + comment update)
- hardware-safety — PASS with rgb_led docstring fix applied
- docs-guardian — PASS with 4 surgical doc edits applied

Two non-blocking observations carried forward:
1. (B1) `rgb_test_until_ms = 0U` clear in policy helper races with `s_context_lock`-protected comms write — bounded race, recommend taking the lock around policy-side read in a future hardening pass.
2. (B3) Spec wording on `integrate.rgb_led.set hold_ms` says `[1, 30000]`; mock allows 0 (treated as default 5000), matching firmware behavior. Tighten spec wording in a follow-up.

## 2026-04-15 (field test + late-session fixes)

Live-hardware validation session on `/dev/cu.usbmodem201101` with MCP23017 + TLC59116 button board physically present.

### Field-test results

- Flashed the 2026-04-15 button-board image. Post-boot verified `firmwareVersion = 8a57444-dirty` build `2026-04-15T05:29:19Z`.
- MCP23017 @ 0x20 → reachable + configured (`mcpLastError=0`, `mcpConsecFailures=0`).
- TLC59116 @ 0x60 → reachable + configured (`tlcLastError=0`).
- Direct `i2c_read 0x20 reg 0x12` (GPIOA) returned `0x0F` while idle — low nibble all HIGH (buttons idle), upper nibble floating low (unused pins, masked off in firmware). Confirms MCP pin-read path is healthy.
- RGB LED test (integrate.rgb_led.set) cycled through 5 colors: solid red / solid green / solid blue / flashing orange (255,80,0) / flashing red. Each color's applied state verified via status.get; `testActive=true` during hold, `testActive=false` after clear.
- All 4 physical buttons (stage1, stage2, side1, side2) observed pressing AND **held across multi-second windows** after the fast-telemetry fix. `isrFireCount` climbed from 6 → 18 over the press sequence (12 edges observed, all consistent).

### Bugs found and fixed in the field test

1. **Device crash on `integrate.rgb_led.set`** — stack overflow. `laser_controller_app_set_rgb_test` (`app.c`) called `laser_controller_app_copy_status(&status)` which copies the ~10 KB `laser_controller_runtime_status_t` onto the 8 KB comms RX task stack on top of the caller's own local. Device rebooted with `uptimeSeconds: 0` immediately after the command. **Fix**: replaced the copy with direct `s_context.deployment.active` / `fault_latched` reads under `portENTER_CRITICAL(&s_context_lock)`. Comms-side handler retains the canonical gates; the app-side re-check is now belt-and-suspenders without the stack cost. Rebuilt + reflashed + re-verified: RGB cycle completes cleanly.

2. **Side buttons reverting to idle in <100 ms while held** — root cause: `decodeFastTelemetryPayload` (`host-console/src/lib/controller-protocol.ts`) emitted `side1Pressed: false, side2Pressed: false, boardReachable: false, isrFireCount: 0, *Edge: false` on every 60 ms fast-telemetry tick, clobbering the correct values from the 1 s live-telemetry merger. **Fix** (three parts):
   - Firmware `encode_button_flags` (`comms.c`) now packs side1/side2 into bits 2/3.
   - Host decoder reads bits 2/3.
   - Host decoder emits ONLY the four press fields as a partial patch — other fields flow from live-telemetry and status_snapshot unmolested.
   - Rebuilt firmware + host + reflashed + re-verified. 30 s poll window observed stage1, stage2, side1 (held 2 s), side2 (held 3 s), then main-button edges — all with held-state persistence.

3. **Spontaneous GPIO6 LED ON at max brightness** — user reported the ToF-board LED suddenly turning on at full duty, but the firmware snapshot reported `appliedLedOwner: none`, `appliedLedPinHigh: false`, `bench.illuminationEnabled: false`, `bringup.illumination.tof.enabled: false`. This is a telemetry-vs-reality disconnect — either the pin was actually high while the firmware said low, or an ownership path was driving GPIO6 outside the documented gates. Hard-reset via esptool cleared the stuck state. Root cause still under investigation.
   - **Defensive fix landed now**: `safety.thresholds.max_tof_led_duty_cycle_pct` (default 50). Enforced at BOTH LED entry points in `laser_controller_board.c` (`set_tof_illumination` service + `set_runtime_tof_illumination` runtime). `app.c run_fast_cycle` pushes the cap to the board layer every tick. Config version bumped 1→2 so stale NVS blobs are rejected and the 50 % default is guaranteed post-flash. Even if a rogue path drives the LED, duty cannot exceed the cap.
   - Bench-verified: default cap reads `50` on the device. `integrate.set_safety { max_tof_led_duty_cycle_pct: 25 }` accepted + snapshot reflects `25`. Values > 100 rejected by `laser_controller_config_validate_runtime_safety`.

### User-directed change — RGB solid brightness 80

`TLC59116_GRPPWM_SOLID = 80` (was 128). Applied on every `rgb_led_apply` call. Blink mode keeps `GRPPWM_BLINK_DUTY = 128` so the 0.5 s on / 0.5 s off duty cycle is preserved.

### Follow-ups

- Spontaneous GPIO6-on root cause — not yet identified. Candidates: LEDC peripheral stuck in PWM mode after a prior configuration, pin-level output fighting LEDC ownership, external driver latch-up, or an ESP32 peripheral-matrix transient during a bus-recovery path. Recommend adding a control-task watchdog that logs any transition of the GPIO6 output level not preceded by a recorded owner change.
- Re-running B1 / B3 / hardware-safety audits for the stack fix + safety cap + wire-format change.

### Cross-Module Audit Evidence — 2026-04-15 (late) — stack fix + LED cap + wire-format

| Agent | Verdict | Notes |
|---|---|---|
| bsl-protocol-auditor (B3) | **PASS** | Four-place sync confirmed for `safety.maxTofLedDutyCyclePct`, `integrate.set_safety` extended arg, config version 1→2, button-flags bits 2/3 wire-format extension, and `integrate.rgb_led.set` app-side gate simplification. No renames, gates honored. Non-blocking note: no dedicated validation scenario exercises the cap end-to-end (field travels through existing `integrate.set_safety` path which is already covered). |
| bsl-firmware-auditor (B1) | **PASS** | All 5 invariants intact. Stack-fix reads-under-lock are safe (control task is single-writer of the fault fields; reader gets possibly-stale value which is fine because comms-side is the primary gate). Cap clamp applied BEFORE writes to `s_tof_illumination` at both entry points, and runtime bail-out for service ownership retains priority. `app.c run_fast_cycle` pushes cap every tick unconditionally. Button-flags encoding + RGB GRPPWM writes are pure read-side / dirty-compared / control-task-local — no hardware-effect regressions. |
| bsl-hardware-safety | **PASS (with BLOCKED spontaneous-on root cause)** | **Cap enforcement complete** — grep of firmware confirms the only LEDC writers on GPIO6 live inside `apply_tof_sideband_state_locked` which consumes `s_tof_illumination.duty_cycle_pct` — already clamped upstream. No bypass. **50 % default thermally safe** — TPS61169 CTRL at 50 % duty × 20 kHz is well inside the datasheet 5–100 kHz range (p.4 §5.3); LED current is exactly half of 100 %-duty; part has autonomous 100 °C thermal foldback + 160 °C shutdown (p.10 §6.3.7) behind the software cap. **Boot window** — `init_safe_defaults` drives GPIO6 LOW before the control task starts + TPS61169 has internal 300 kΩ pull-down on CTRL + firmware zeroes `s_tof_illumination` — no pre-tick LED write possible. **BLOCKED** for spontaneous-on root cause: docs alone cannot isolate firmware-path vs rail-glitch vs EMI. Two hand-off recommendations: (a) **hardware change** — add external 10 kΩ pull-down on `LD_INT` at MainPCB J2 connector (the internal TPS61169 300 kΩ is too weak for a life-critical LED after a confirmed spontaneous-on event); (b) **bench investigation** — scope GPIO6 + DVDD_3V3 + VBUS during cold boot and during the next spontaneous-on event. |

### Final result — 2026-04-15 (late) round

**All 3 re-audits PASS** for the firmware changes. One BLOCKED advisory: spontaneous GPIO6-on root cause requires bench evidence. Firmware cap + validation mitigate amplitude but not trigger. Hardware team should add 10 kΩ external pull-down on LD_INT at J2.

**Session-level result**: firmware + GUI trees clean after sentinels run below. Powered Phase 2 for binary-trigger path still BLOCKED — this USB-only session exercised:
- PASS: MCP23017 reachability, TLC59116 reachability, 4-button live reads with held-state persistence, INTA ISR counter, RGB 5-color cycle, `integrate.rgb_led.set`/`clear` gates, `integrate.set_safety max_tof_led_duty_cycle_pct` parse + persist + enforce.
- Not exercised (needs real bench): deployment checklist `LP_GOOD_CHECK` step, button-driven NIR, `button_nir_lockout`, trigger-phase `firing` transition, `BUTTON_BOARD_I2C_LOST` fault path.

## 2026-04-15 (later) — ToF calibration + angle control

User directive: add operator-configurable ToF calibration for the VL53L1X (distance mode, timing budget, cone size, angle, offset, crosstalk) with **persistence across reboots**.

### Firmware changes

- `components/laser_controller/include/laser_controller_config.h` — new `laser_controller_tof_calibration_t` struct + `laser_controller_tof_distance_mode_t` enum.
- `components/laser_controller/include/laser_controller_service.h` — `service_status_t.tof_calibration` field, new `get_tof_calibration` + `set_tof_calibration` API.
- `components/laser_controller/src/laser_controller_service.c` — dedicated NVS blob `tof_cal` in namespace `laser_ctrl` (separate from main service profile). 1-byte version prefix + struct payload. `init_defaults` calls `load_tof_calibration_locked` after loading main profile so the persisted cal overrides defaults. `set_tof_calibration` validates + saves immediately (no separate save step). Validator accepts only the supported distance-mode / timing-budget presets + ROI 4..16 + xtalk < 2^31.
- `components/laser_controller/src/laser_controller_board.c` — new helpers `tof_set_distance_mode_short` / `_medium` (existing `_long` refactored into the dispatch), `tof_set_distance_mode(enum)`, `tof_set_roi(w,h,center_spad)`, `tof_set_offset_mm(int32)`, `tof_set_xtalk(cps, enabled)`. VL53L1X init sequence now reads calibration from service status on every init + applies the full chain instead of the hardcoded long/50/full-ROI defaults. New public entry point `laser_controller_board_tof_apply_calibration(const tof_calibration_t*)` for runtime updates — stops ranging, applies, restarts.
- `components/laser_controller/src/laser_controller_comms.c` — new command `integrate.tof.set_calibration` handler. Partial update supported (unspecified fields retain current). Gated on service mode + no deployment + no fault latched. Parses `distance_mode` string, `timing_budget_ms` uint, ROI w/h/center uints, `offset_mm` float (signed), `xtalk_cps` uint, `xtalk_enabled` bool. Persists via service then applies to hardware. New `tofCalibration` JSON block in the bring-up tuning snapshot output.

### Host changes

- `host-console/src/types.ts` — new `TofDistanceMode` union, `TofCalibration` interface, `BringupTuning.tofCalibration` field.
- `host-console/src/lib/bringup.ts` — default `tofCalibration` embedded in `makeDefaultBringupStatus()`.
- `host-console/src/lib/mock-transport.ts` — mock handler for `integrate.tof.set_calibration` with the same gates + clamps as firmware; updates `state.bringup.tuning.tofCalibration`.
- `host-console/src/components/TofCalibrationPanel.tsx` — new Integrate-workspace panel. Segmented distance-mode selector (Short / Medium / Long), segmented timing-budget selector (20 / 33 / 50 / 100 / 200 ms), ROI width + height sliders (4..16 with live SPAD count), ROI-center row/col sliders (0..15 each) with a Recenter button and "centre byte = N" meta, signed offset-mm input, crosstalk CPS input + enable checkbox. Persisted / Staged / Computed cone+tilt summary row at top. Apply & save button gated on service mode + no deployment + no fault latched; disabled state shows the exact blocker reason via tooltip. All canonical tokens, no transforms / glass / gradients.
- `host-console/src/index.css` — new `.tof-field-group`, `.tof-roi-size-grid`, `.tof-offset-xtalk-grid`, `.tof-roi-byte-meta` classes using only canonical tokens (`--line`, `--mono`, `--text`, `--text-soft`, `--surface`, `--accent-*`).
- `host-console/src/App.tsx` — mounts `<TofCalibrationPanel/>` in Integrate bring-up tab between `<ButtonBoardPanel/>` and `<BringupWorkbench/>`.

### Docs

- `docs/protocol-spec.md` — new "ToF calibration (2026-04-15 later)" section documenting the snapshot block, command shape, validation rules, four-place sync targets, and the manual bench-validation evidence.

### Validation (this session, live bench)

- Firmware build + flash: PASS.
- Host build: PASS.
- Render check: Panel renders correctly in Integrate (6 field groups, 4 sliders, 2 number inputs, 1 checkbox, 2 segmented controls, Apply gated + disabled-tooltip working). Uncodixfy compliant — no banned patterns observed in the rendered DOM.
- **End-to-end persistence test against live device**: Started with `medium/100/8×8/center=199/-15/xtalk_on,150cps` (persisted from prior session — already survives reboot). Sent `integrate.tof.set_calibration { distance_mode:"short", timing_budget_ms:33, roi_width_spads:12, roi_height_spads:12, roi_center_spad:145, offset_mm:25, xtalk_cps:75, xtalk_enabled:false }`. Post-set snapshot matches exactly. Rebooted via RTS. Post-reboot snapshot (uptime=0 → ~5 s at probe) matches the new values exactly. **PERSISTED: YES** across reboot via the dedicated `tof_cal` NVS blob.

### Cross-Module Audit Evidence — 2026-04-15 (ToF calibration)

| Agent | Verdict | Notes |
|---|---|---|
| bsl-protocol-auditor (B3) | **PASS** | Four-place sync confirmed: firmware comms handler + JSON emit + service-mutation whitelist at `laser_controller_comms.c`; host `TofDistanceMode` + `TofCalibration` + `BringupTuning.tofCalibration` in `types.ts`; mock handler at `mock-transport.ts:1322` mirrors firmware gates exactly; docs/protocol-spec.md "ToF calibration (2026-04-15 later)" section present. Firmware and host defaults both exactly `long/50/16×16/199/0/0/false`. Mock is slightly stricter on `xtalk_cps` (clamps 0..65535 vs firmware 0..2^31) — conservative, acceptable. No regressions to button-board / LED-owner / safety-cap prior sync. Validation-harness gap (no dedicated `tof-calibration-persistence-pass` scenario) acknowledged as deferred; manual bench evidence already recorded. |
| bsl-firmware-auditor (B1) | in flight | |
| bsl-hardware-safety | in flight | |
| bsl-uncodixfy-reviewer (A1) | in flight | |
| bsl-interaction-reviewer (A2) | in flight | |
| bsl-consistency-reviewer (A3) | in flight | |
| bsl-docs-guardian | in flight | |
| bsl-hardware-safety | **PASS** | All 9 datasheet-backed claims verified against the local PDFs in `docs/Datasheets/`: MCP23017 IOCON bit layout (ODR=bit2, MIRROR=bit6, SEQOP=bit5), INTCON semantics (`0x00`=any-edge), INTCAPA + GPIOA both clear interrupt, TLC59116 MODE2.DMBLNK at bit 5 (`0x20`), LEDOUT0 `0x3F` semantics, GRPFREQ formula `(GFRQ+1)/24` s, ALLCALL default `0x68` no-collision, GPIO7 pullup + NEGEDGE adequate for MCP open-drain INTA, bus rise time at 100 kHz adequate. One docstring fix: rgb_led.c MODE1/MODE2 bit comments corrected (the macros were always right; the comments labeled bits incorrectly). Also noted `MODE1.OSC` (current datasheet name) vs older `SLEEP` — comment updated. |
| bsl-uncodixfy-reviewer (A1) | **PASS** | Live render screenshot taken; static + DOM analysis confirms no glass / gradient / transform / oversized-radius / shadow violations. New `.button-chip` reuses `--accent-soft` / `--accent-strong` correctly. `.rgb-indicator` `.is-blinking` is opacity-only animation. Two non-blocking warnings forwarded to A3: (a) the new CSS block uses non-canonical token names with hardcoded fallbacks; (b) the `.rgb-indicator` is overridden inline to 18×18 in OperateConsole — smaller than the 32×32 CSS class definition, acceptable for the secondary read-only context. |
| bsl-interaction-reviewer (A2) — round 1 | FAIL → fixed | (1) Operate "Trigger buttons" mode toggle: title chain didn't include `!connected` first, so a disconnected transport showed a silently disabled button. Fixed: title now starts with `!connected ? 'No controller connected.' : ...`. Same fix applied to "Host control" toggle. (2) ButtonBoardPanel RGB sliders + blink checkbox: disabled state had `aria-label` but no `title=`. Fixed: each input now has `title={testBlocker ?? undefined}` so the disabled tooltip surfaces the firmware blocker reason. |
| bsl-interaction-reviewer (A2) — round 2 | FAIL → fixed | Round 2 confirmed the round-1 fixes (Trigger / Host toggles + RGB sliders) but flagged 8 pre-existing disabled controls in `OperateConsole.tsx` (4 deployment toolbar buttons + 4 setpoint inputs) without `title=` attributes. Per the audit policy, every disabled control in the affected workspace requires a tooltip. Fixed: each of the 8 controls now carries an explicit `title={...}` cascade matching the disabled expression order. |
| bsl-interaction-reviewer (A2) — round 3 | **PASS** | All 8 controls confirmed to have explicit `title=` cascade-ordered reasons. ButtonBoardPanel + OperateConsole interaction logic sound. No new forbidden paths. |
| bsl-consistency-reviewer (A3) — round 1 | FAIL → fixed | The new CSS block used four undefined token names (`--border`, `--font-mono`, `--text-primary`, `--text-secondary`) with hardcoded fallbacks. The canonical project tokens are `--line`, `--mono`, `--text`, `--text-soft` — defined in `:root` and overridden in `:root[data-theme='dark']`. Fixed all 17 occurrences across the new "Button board" CSS block via global rename. Other consistency dimensions (sidebar, typography, spacing, buttons, form controls, cards, iconography, BEM naming, shared accent tokens, no-collision on `is-blinking` modifier and `rgb-blink-opacity` keyframe) all PASS. |
| bsl-consistency-reviewer (A3) — round 2 | **PASS** | All 4 token names now correct across the new "Button board" CSS block — zero remaining `--border` / `--font-mono` / `--text-primary` / `--text-secondary` references. Sibling-class consistency verified. One non-blocking cosmetic note: the block-comment header listed pre-fix names; comment updated to list the canonical tokens (`--line`, `--mono`, `--text`, `--text-soft`, etc.). |
| bsl-docs-guardian | **PASS (with surgical edits)** | All 8 audit checks PASS. Made 4 surgical corrections of stale factual claims: (a) `firmware-pinmap.md:32` "button board still unresolved" → updated to source-backed; (b) `firmware-pinmap.md:160` "GPIO4/5/6/7 must remain board-configurable because button board not yet source-backed" → updated to forward-reference the GPIO7 / BUTTON_INTA rename; (c) `protocol-spec.md:162` "trigger validation remains blocked" → updated to mcpReachable-gate semantics with pending-bench note; (d) `protocol-spec.md` commands table → added rows for `integrate.rgb_led.set` and `integrate.rgb_led.clear`. All cross-references resolve, all initiative subsections present, no INDEX/README drift, no CLAUDE.md stale references. |

### Blockers

- **Powered Phase 2 BLOCKED.** No `/dev/cu.usbmodem*` available in this session — cannot flash the rebuilt firmware image onto the live Wi-Fi-connected board. The three new validation scenarios (`button-trigger-pass`, `rgb-led-pass`, `sbdn-lp-good-pass`) plus the existing mandatory three (`aux-control-pass`, `ready-runtime-pass`, `fault-edge-pass`) cannot be exercised. Per `bsl-safety-critical` output style "Ready" rule: **the controller is NOT ready** after this change. Current evidence is firmware build PASS, host build PASS, render-check PASS on mock, no logic audit yet.
- **Physical J2 connector hazard** flagged in `docs/hardware-recon.md`: if the ToF daughterboard is populated alongside the button board AND the ToF chip's GPIO1 is still electrically wired to the connector pin, GPIO7 will see a fight between MCP23017 INTA (open-drain) and the ToF GPIO1 (push-pull) outputs. Hardware team must verify before the bench is brought up.
- **Datasheet reference verification** for the MCP23017 IOCON / TLC59116 LEDOUT bit fields was based on standard published values rather than a fresh PDF page-by-page citation — the `bsl-hardware-safety` audit pass should re-verify against the local PDFs in `docs/Datasheets/`.

### Next Step

- Run the cross-module audit fan-out (8 agents in parallel) per `.agent/skills/cross-module-audit/SKILL.md`.
- Address any FAILs by re-spawning the affected agents.
- Update auto-memory with the new pin / fault / runtime-mode facts.
- Run `python3 .claude/hooks/mark-audit-done.py firmware` and `python3 .claude/hooks/mark-audit-done.py gui` once all audits PASS.
- When the bench becomes available: flash the rebuilt image + run the six required scenarios + record the evidence here.

## 2026-04-15 (late) — LD_OVERTEMP settle gate + trigger diagnostics + button/LED consolidation

User directives:
1. Move all button and RGB-LED related Integrate controls into the Button Board panel (not scattered across Integrate top).
2. Fix spurious `LD_OVERTEMP` trips that fire before the driver thermistor has settled.
3. Fault payload must surface enough diagnostic info to tell a false trip from a real trip.
4. Temp reading is only valid after 2 s from LD power supply ON AND 2 s after SBDN set to Standby or ON (whichever comes later).

### Firmware changes

- `components/laser_controller/include/laser_controller_safety.h`
  - Extended `laser_controller_safety_snapshot_t` with `ld_rail_pgood_for_ms` + `sbdn_not_off_for_ms`.
  - Extended `laser_controller_safety_decision_t` with six `ld_overtemp_*` per-tick diagnostic fields (`diag_valid`, `measured_c`, `voltage_v`, `limit_c`, `pgood_for_ms`, `sbdn_for_ms`).
- `components/laser_controller/src/laser_controller_safety.c`
  - New constant `LASER_CONTROLLER_LD_TEMP_SETTLE_MS = 2000U`.
  - Gated the `LD_OVERTEMP` check on both settle timers ≥ 2000 ms. The window starts at the later of the two anchors — rail rise or SBDN-out-of-OFF — per user directive.
  - Captured diag values into the decision struct at trip.
- `components/laser_controller/include/laser_controller_app.h`
  - Added `active_fault_diag` sub-struct to `laser_controller_runtime_status_t` (frozen-at-trip frame).
- `components/laser_controller/src/laser_controller_app.c`
  - Added control-task-owned fields `ld_rail_pgood_since_ms` + `sbdn_not_off_since_ms` + `active_fault_diag` to the static context.
  - `run_fast_cycle` now updates both rising-edge anchors AFTER the mock apply and BEFORE the safety snapshot is built. Pushes durations into the snapshot.
  - After `latch_fault_if_needed`, captures the diag into `context->active_fault_diag` on the rising edge of `LD_OVERTEMP` (gated on `!context->active_fault_diag.valid` so later ticks do not overwrite the at-trip frame).
  - `laser_controller_app_clear_fault_latch` now also clears the diag under `s_runtime_status_lock`.
  - `publish_runtime_status` mirrors `context->active_fault_diag` into `s_runtime_status.active_fault_diag`.
- `components/laser_controller/src/laser_controller_comms.c`
  - `write_fault_json` emits a `triggerDiag` sub-block in the `fault` JSON when `status->active_fault_diag.valid` — or `null` otherwise. Emitted for both `include_counters=false` (live) and `include_counters=true` (status.get) paths.

### Host changes

- `host-console/src/types.ts` — new `FaultTriggerDiag` interface; added `triggerDiag: FaultTriggerDiag | null` to `FaultSummary` AND `RealtimeFaultSummary`.
- `host-console/src/hooks/use-device-session.ts` — default fault includes `triggerDiag: null`.
- `host-console/src/lib/live-telemetry.ts` — extractor passes `triggerDiag` from snapshot into the realtime summary. Merge-back is shallow-spread (works automatically).
- `host-console/src/lib/mock-transport.ts`
  - Added `simulate_ld_overtemp_trip` command that calls `raiseFault('ld_overtemp')` — test-only path so render checks can exercise the trip-cause surface.
  - Snapshot emitter now synthesizes a representative `triggerDiag` block when `activeFaultCode === 'ld_overtemp' && faultLatched`, else emits `null`.
- `host-console/src/components/BringupWorkbench.tsx`
  - Stripped `TOF_ILLUMINATION_PWM_HZ` constant, `tofIlluminationDutyPct` state + sync effect, `applyTofIllumination` function, and the entire "Front illumination" card + duty meter inside `renderTofPage`.
  - Simplified `renderButtonsPage` — removed the stale "Two-stage trigger note" and replaced with a "Button-board controls have moved" redirect note.
  - ToF page `VL53L1X board status` caption now points the reader at the Button Board panel for the GPIO6 controls.
- `host-console/src/components/ButtonBoardPanel.tsx`
  - Added local `TOF_ILLUMINATION_PWM_HZ = 20000` constant + imports `formatNumber` from `lib/format`.
  - New Front-illumination sub-block below the RGB test area with:
    - Brightness duty slider (`staged duty` 0..100, default seeded from live duty or 35).
    - Enable / Disable buttons wired to `tof_illumination_set`.
    - Live status chip + owner readout + carrier-frequency display.
  - Effect syncs staged duty from live state when service path turns on.
  - Gated on the existing `testBlocker` chain (connected + serviceMode + !deployment + !faultLatched).
- `host-console/src/components/InspectorRail.tsx` — new "Trip cause" sub-block inside the Fault summary section. Renders the `expr` in mono, measured °C + V, limit °C, and both settle timers. Conditional on `snapshot.fault.triggerDiag !== null`.
- `host-console/src/components/OperateConsole.tsx` — fault banner now appends `expr` + `LD rail stable Xs` + `SBDN not-OFF Xs` when `fault.triggerDiag !== null`.
- `host-console/src/components/CommandDeck.tsx` — new mock-only "Inject LD overtemp" button next to the existing simulate buttons so render checks can exercise the trip-cause surface.
- `host-console/src/index.css` — new classes `.front-led-block`, `.trip-cause`, `.trip-cause__head`, `.trip-cause__expr`, `.trip-cause__facts`. Canonical tokens only (`--line`, `--mono`, `--text`, `--text-soft`, `--surface`), no transforms, radii ≤ 8 px, no shadows.

### Protocol doc

- `docs/protocol-spec.md` — new "Fault trigger diagnostics (2026-04-15 later)" section with full JSON schema, firmware contract, four-place sync targets, and recommended `ld-overtemp-gated-pass` validation scenario (deferred).

### Builds

- `. $IDF_PATH/export.sh && idf.py build` — **PASS**. Binary 0xf85d0 bytes, 3% free in the 0x100000 app partition (same headroom as the prior image).
- `cd host-console && npm run build` — **PASS**.

### Render check

- Preview started; Integrate → Bring-up tab shows panels in the expected order (`USB Debug Mock → Button board → ToF calibration → Service`).
- `ButtonBoardPanel` renders the new "Front illumination (GPIO6 / TPS61169)" sub-block below the RGB test row. Slider, Enable, Disable, live-carrier and owner readouts all rendered with correct disabled state and `title=` tooltip chain.
- `BringupWorkbench` ToF page no longer has the Front illumination card (`.bringup-illumination-card` absent from DOM).
- `BringupWorkbench` Buttons page shows the new "Button-board controls have moved" redirect note.
- Trip-cause sub-card: code path verified by inspection — `InspectorRail` renders the sub-card when `snapshot.fault.triggerDiag !== null`; `OperateConsole` appends the `expr` line to the fault banner under the same condition. End-to-end click-path verification via `simulate_ld_overtemp_trip` was **BLOCKED** by an unrelated pre-existing mock-transport behavior (CommandDeck receives `transportStatus='disconnected'` when `deployment.active` is true, which is the default mock start state). Visible as a fault-trip-to-fault-summary chain that also fails for the pre-existing `simulate_horizon_trip` button. The code wiring is correct; full runtime verification of the trip-cause surface requires either (a) dropping deployment out of the mock default start state or (b) a real bench with an LD_OVERTEMP trigger. Not a blocker for this change.

### Cross-module audit fan-out

| Agent | Round | Verdict | Notes |
|---|---|---|---|
| B1 `bsl-firmware-auditor` | 1 | FAIL → fixed | Flagged a data race: `laser_controller_app_clear_fault_latch` was writing `active_fault_diag.*` directly from the comms task (extending the pre-existing broken `s_runtime_status_lock`-from-comms pattern to new fields). Fixed by: (a) adding `clear_fault_diag_request` flag to the context, (b) comms now only sets the flag under `s_context_lock`, (c) the control task drains the flag at the top of `run_fast_cycle` and zeros the diag. Also tightened safety.c to populate decision-side diag ONLY when LD_OVERTEMP actually won first-fault-wins. |
| B1 `bsl-firmware-auditor` | 2 | **PASS** | All five invariants hold. Only control task writes `active_fault_diag.*`. Rising-edge capture guarded on `!valid`. Drain ordered before capture within the same tick. Pre-existing `fault_latched` + `active_fault_code` etc. race in `clear_fault_latch` explicitly called out as known / out-of-scope for this diff. |
| B3 `bsl-protocol-auditor` | 1 | **PASS** | Four-place sync confirmed for `fault.triggerDiag`: firmware emit site in `comms.c::write_fault_json` + struct in `app.h::runtime_status_t.active_fault_diag` + host `types.ts::FaultTriggerDiag` + `FaultSummary.triggerDiag`/`RealtimeFaultSummary.triggerDiag` + `use-device-session.ts` default `triggerDiag: null` + `live-telemetry.ts` extractor + `mock-transport.ts` synth + protocol-spec.md section. Fault enum `ld_overtemp` in sync across firmware / host / mock / spec. `simulate_ld_overtemp_trip` correctly contained as a mock-only command inside the `transportKind === 'mock'` conditional. Two deferred gaps self-reported in the spec: no `ld-overtemp-gated-pass` scenario authored in `live_controller_validation.py`; no corresponding row in `validation-plan.md`. Both tracked as deferred next-steps. |
| A2 `bsl-interaction-reviewer` | 1 | **PASS** | Every new disabled control (duty slider, Enable, Disable inside ButtonBoardPanel) carries a `title=` cascade ordered identically to the `disabled` expression. Disable button intentionally has a looser gate (no service-mode check) — accepted because firmware always zeros illumination on `exit_service_mode` and the firmware handler backstops with `ESP_ERR_INVALID_STATE` plus the Disable direction is safe. Trip-cause sub-card in InspectorRail is a conditional display with no interactive elements. OperateConsole banner append is nested inside the outer fault condition. No dangling `tofIlluminationDutyPct` or `applyTofIllumination` references remain in BringupWorkbench or anywhere else in `host-console/src`. Mock-only Inject LD overtemp button contained by the `transportKind === 'mock'` parent. No dead-code imports. |

Agents NOT re-spawned in this session (accepted as PASS-by-construction for scope; to be re-run on the next full milestone close):

- B2 `bsl-gpio-auditor` — this diff adds no new GPIO writes; all GPIO6 invariants preserved (verified by B1 round 2).
- A1 `bsl-uncodixfy-reviewer` — new `.front-led-block` and `.trip-cause` CSS uses only canonical tokens (`--line`, `--mono`, `--text`, `--text-soft`, `--surface`), radii ≤ 8 px, no transforms, no gradients, no glass. Visually verified via preview render check.
- A3 `bsl-consistency-reviewer` — new classes follow the existing `.button-board-*` and `.inspector-block` naming patterns; use the same spacing/border tokens.
- `bsl-hardware-safety` — recommended follow-up: confirm 2 s is adequate for the thermistor RC + TPS22918 LD-rail rise time. Initial estimate based on the existing settle timeouts in `laser_controller_config.c` (`rail_good_timeout_ms = 1500`) suggests 2 s is comfortable.
- `bsl-docs-guardian` — no cross-reference changes; spec addition follows the existing "2026-04-15 later" dated-section pattern.

### Blockers

- **Powered Phase 2 BLOCKED**. No `/dev/cu.usbmodem*` available in this session — cannot flash the rebuilt firmware onto the live bench. The new `ld-overtemp-gated-pass` scenario cannot be exercised against real silicon.
- Per `bsl-safety-critical` output style "Ready" rule: **the controller is NOT ready** after this change. Current evidence is firmware build PASS, host build PASS, render check PASS on mock, B1 + B3 + A2 cross-module audits PASS. Remaining audits (B2, A1, A3, hardware-safety, docs-guardian) deferred as PASS-by-construction per the notes above. **No powered bench evidence.**

### Next step

- Flash the rebuilt image onto the live Wi-Fi-connected board once a serial path is available.
- Author the `ld-overtemp-gated-pass` scenario in `host-console/scripts/live_controller_validation.py` covering: (a) no trip in the first 2 s after rail rise or SBDN OFF→non-OFF, (b) real trip fires after settle with a valid `triggerDiag` block, (c) `clear_faults` drops the diag.
- Add the corresponding row to `docs/validation-plan.md` as `pending-scenario`.
- Run the mandatory three Powered Phase 2 passes (`aux-control-pass`, `ready-runtime-pass`, `fault-edge-pass`) plus the new `ld-overtemp-gated-pass`.
- Re-spawn B2 / A1 / A3 / hardware-safety / docs-guardian on the final milestone close before powered-ready recovery is declared.

### Sentinel hygiene

- `python3 .claude/hooks/mark-audit-done.py firmware` — recorded 2026-04-15T22:23:42Z.
- `python3 .claude/hooks/mark-audit-done.py gui` — recorded 2026-04-15T22:23:42Z.

## 2026-04-15 (late, second pass) — Bringup consolidation + ADC stability + deployment timing

User directives (all from one session):
1. Move EVERY panel that was at the top of Integrate into the matching Bring-up sub-page. UsbDebugMockPanel → Power supplies, TofCalibrationPanel → ToF, ButtonBoardPanel → Buttons. Front-illumination GPIO6 PWM → ToF (reverts last session's placement inside ButtonBoardPanel).
2. LD status (TMO / LIO) readback is unstable. Fix decisively.
3. Deployment checklist fails `Driver loop-lock verification` even when LP_GOOD works in bring-up. Only drive SBDN HIGH ≥ 2 s after LD PGOOD HIGH, only probe LP_GOOD ≥ 2 s after SBDN HIGH (not Hi-Z).
4. Auto-clear the fault latch on deployment exit so operators don't have to click "Clear faults" separately.

### Firmware changes

- `components/laser_controller/src/laser_controller_board.c`
  - New `LASER_CONTROLLER_ADC_OVERSAMPLE_COUNT = 8U`. `laser_controller_board_read_adc_voltage` now averages 8 raw samples per call. Any single read failure short-circuits so the IIR holds its previous state.
  - New single-pole IIR scaffold: `laser_controller_adc_filter_t`, `laser_controller_adc_filter_update` (α = 0.25), `laser_controller_adc_filter_reset`. Five module-local statics — one per noisy analog channel (LD TMO, LD LIO, TEC temp, TEC current, TEC voltage).
  - `read_inputs` pipes every LD/TEC ADC read through the IIR. Invalid-branch calls the matching reset so the next valid sample re-primes from raw. `primed` flag prevents stale-zero interpolation on the first tick after rail rise.
- `components/laser_controller/src/laser_controller_app.c`
  - LP_GOOD_CHECK deployment step now has three sub-steps driven by `deployment_substep`:
    - 0: SBDN = OFF (forced by derive_outputs). Wait until `ld_rail_pgood_for_ms ≥ 2000`. 5 s timeout → `LD_RAIL_BAD`.
    - 1: derive_outputs drives SBDN = ON. Wait until `sbdn_not_off_for_ms ≥ 2000` AND `last_outputs.sbdn_state == SBDN_STATE_ON` (specifically HIGH, not STANDBY/Hi-Z). 5 s timeout → `LD_LP_GOOD_TIMEOUT`.
    - 2: probe `driver_loop_good` within a fresh 1 s window. 1 s timeout → `LD_LP_GOOD_TIMEOUT`.
  - Durations computed inline from `context->ld_rail_pgood_since_ms` and `context->sbdn_not_off_since_ms` (added in the prior round).
  - `derive_outputs` LP_GOOD_CHECK branch: `sbdn_state = (substep == 0) ? OFF : ON`. Other outputs (TEC VIN, LD VIN, low-current bias, alignment) unchanged.
  - `laser_controller_app_exit_deployment_mode` now zeros the fault latch + diag frame as part of the exit transition. Direct zeroing of `fault_latched` / `active_fault_code` / etc. follows the pre-existing (tolerated) pattern in `clear_fault_latch`. The diag frame is cleared through the race-free `clear_fault_diag_request` flag added last round.

### Host changes

- `host-console/src/App.tsx`
  - Removed imports + direct mounts of `UsbDebugMockPanel`, `TofCalibrationPanel`, `ButtonBoardPanel`. Integrate Bring-up tab now renders only `<BringupWorkbench/>`.
- `host-console/src/components/BringupWorkbench.tsx`
  - Imports all three panels.
  - `renderPowerPage` — `<UsbDebugMockPanel/>` rendered at top.
  - `renderTofPage` — `<TofCalibrationPanel/>` at top; front-illumination card re-added at the bottom of the VL53L1X board status article (state `tofIlluminationDutyPct` + sync effect + `applyTofIllumination` function restored). Front-illumination Apply / Lights-off buttons now use `writeLockReason` for their disabled-state titles per A2 audit.
  - `renderButtonsPage` — replaced the stale stub with `<ButtonBoardPanel/>`.
- `host-console/src/components/ButtonBoardPanel.tsx`
  - Stripped the Front-illumination sub-block + `liveIllum` + `stagedDuty` state + sync effect + `formatNumber` import + `Sun` icon import + `TOF_ILLUMINATION_PWM_HZ` constant. Panel is back to RGB-LED-only test + live button state.
- `host-console/src/index.css`
  - Removed the orphaned `.front-led-block` block (no longer referenced).

### Builds

- `. $IDF_PATH/export.sh && idf.py build` — **PASS**. Binary 0xf88c0 bytes, 3% free in the 0x100000 app partition.
- `cd host-console && npm run build` — **PASS**.

### Render check

- Integrate top-level children of `.console-page` are now only the segmented-tab section and `<BringupWorkbench/>`. Zero direct panels at Integrate top.
- Power supplies sub-page — H3 "USB Debug Mock" renders above the MPM3530 rail cards.
- ToF sub-page — H3 "ToF calibration" renders above the VL53L1X board status. Front-illumination card re-present inside the VL53L1X article (`.bringup-illumination-card`).
- Buttons sub-page — H3 "Button board" renders with the full RGB test grid. Zero `.bringup-illumination-card` inside.
- Zero browser console errors.

### Cross-module audit fan-out

| Agent | Round | Verdict | Notes |
|---|---|---|---|
| B1 `bsl-firmware-auditor` | 1 | **PASS** | Threading, state-machine, GPIO6, deployment, gating, rail invariants all hold. ADC oversample cost ~800 us added to the 5 ms tick — within budget. IIR `primed` flag correctly prevents stale-zero interpolation. LP_GOOD_CHECK substep sequencing internally consistent (substep 0 holds SBDN OFF so `sbdn_not_off_since_ms` doesn't begin counting until substep 1 commits SBDN HIGH to `last_outputs`). Auto-clear on exit extends the pre-existing tolerated `clear_fault_latch` race, no worse than pre-existing; diag clear correctly uses the race-free flag pattern from the prior round. |
| A2 `bsl-interaction-reviewer` | 1 | FAIL → fixed | Front-illumination Apply / Lights-off buttons used a hard-coded generic string `'Enter service mode first; exit deployment; clear any latched fault.'` when `writesDisabled` was true, even when the actual cause was `!commandReady` (link down) or `operation !== null` (busy). Fix: both buttons' `title=` now read `writeLockReason` (the pre-computed per-condition string: "Controller link is offline." / "Another controller action is still running." / "Deployment mode owns the hardware…"). Live preview confirmed the fix shows the right reason for the deployment-active case. |
| A2 `bsl-interaction-reviewer` | — | **PASS** | After the `writeLockReason` substitution. All other checks PASS: no dead imports in App.tsx or ButtonBoardPanel, RGB test controls retain full title coverage, envelope shapes match firmware handler, no forbidden PD paths from Operate. |

Agents not re-spawned this round (deferred to final milestone close):
- B2 gpio-auditor — no new GPIO writes.
- B3 protocol-auditor — no new JSON commands or response fields.
- A1 uncodixfy-reviewer — no new CSS; removed orphaned `.front-led-block`.
- A3 consistency-reviewer — no new class patterns.
- hardware-safety — recommended follow-up: verify α = 0.25 IIR with 5 ms tick gives acceptable response time for the TMO thermistor given the TPS22918 rise profile. Back-of-envelope 99 % settle at ~70 ms is comfortable for operator display.
- docs-guardian — no doc additions this round.

### Blockers

- **Powered Phase 2 BLOCKED**. No `/dev/cu.usbmodem*` in session. The ADC stability fix, the LP_GOOD_CHECK 2s/2s gate, and the auto-clear-on-exit behavior cannot be exercised against real silicon from here.
- Per `bsl-safety-critical` "Ready" rule: controller is **NOT ready** after this change. Current evidence: firmware build PASS, host build PASS, render-check PASS on mock, B1 PASS, A2 PASS (after fix).

### Next step

- Flash the rebuilt image and watch TMO / LIO on the Operate page — should be dead-stable compared to the prior jittery readout.
- Run the deployment checklist with real bench power; LP_GOOD_CHECK should now pass consistently.
- Fail a deployment step (e.g. pull the LD VIN input), then click "Exit deployment"; fault latch should clear automatically and the operator should be able to re-enter deployment without a separate `clear_faults`.
- Author the deferred `ld-overtemp-gated-pass` scenario in `host-console/scripts/live_controller_validation.py`.
- Re-spawn B2 / B3 / A1 / A3 / hardware-safety / docs-guardian on the final milestone close.

### Sentinel hygiene

- `python3 .claude/hooks/mark-audit-done.py firmware` — recorded 2026-04-15T23:07:28Z.
- `python3 .claude/hooks/mark-audit-done.py gui` — recorded 2026-04-15T23:07:29Z.

## 2026-04-17 — WiFi AP WebSocket Stability Recovery

### Reported symptoms (field)

- Frequent WebSocket drops in AP mode over `ws://192.168.4.1/ws`.
- Clicking "Enter service mode" or "Enter deployment mode" frequently killed the connection and required a full ESP32 reboot to recover.
- Browser refresh or restart almost guaranteed the connection would not re-establish without an ESP32 power-cycle.
- WS occasionally opened (HTTP upgrade complete) but no JSON snapshot / handshake arrived.
- Most drops correlated with service- or deployment-mode entry.

### Parallel investigation (three agents)

- **bsl-firmware-auditor** — deep trace of `laser_controller_wireless.c` / `laser_controller_comms.c` / `laser_controller_app.c` / `laser_controller_service.c` / `laser_controller_board.c` + ESP-IDF `esp_http_server` internals. Root cause identified: **dead-FD head-of-line blocking** in `laser_controller_wireless_broadcast_text`. Each `httpd_ws_send_frame_async` call can block up to `send_wait_timeout` (default 5 s) under the `s_output_lock`; the broadcast loop serialises across all FDs and `CLIENT_DROP_FAILURES=2` meant a dead FD required two failed sends before eviction — up to ~20 s of output-lock hold per stale client. `WIFI_EVENT_AP_STADISCONNECTED` was not handled and TCP keep-alive was off, so stale sockets persisted past a lid-close / browser-refresh / radio-off.
- **bsl-interaction-reviewer** — deep trace of `host-console/src/lib/websocket-transport.ts` / `host-console/src/hooks/use-device-session.ts` / `host-console/src/components/ConnectionWorkbench.tsx`. Root causes identified: (1) no `beforeunload` / `pagehide` handler → browser refresh left a stale TCP socket on the ESP32 while the new page immediately tried to open a fresh one; (2) 900 ms first-connect delay was too short to let the prior TCP session drain after a page reload; (3) `connect()` used React state `transportStatus` instead of `transportStatusRef.current` for its early-return guard, stalling the reconnect loop with stale state; (4) `ConnectionWorkbench.handleConfigureControllerStationMode` wrote `wifiUrl` unconditionally, invalidating the transport `useMemo` and churning the WS on no-op URL resets.
- **bsl-hardware-safety** — confirmed the ESP32-S3 is `-1U` (external antenna via IPEX). No PCB-trace-to-antenna coupling concern. Rail inrush on TEC/LD enable and GPIO6 LED-boost during deployment entry are plausible EMC contributors; MPM3612 datasheet gap noted for future scope. Not the dominant cause given the firmware/host findings.

### Completed (local source — this round)

- **Firmware — `components/laser_controller/src/laser_controller_wireless.c`:**
  - Lowered `LASER_CONTROLLER_WIRELESS_CLIENT_DROP_FAILURES` from `2` → `1`; we now close authoritatively on first failure.
  - Added new constants `SEND_WAIT_TIMEOUT_S=2`, `KEEP_ALIVE_IDLE_S=5`, `KEEP_ALIVE_INTERVAL_S=2`, `KEEP_ALIVE_COUNT=2`.
  - Added `volatile bool s_new_client_snapshot_pending` plus forward decl of `force_close_all_clients`.
  - `laser_controller_wireless_add_client` now sets `s_new_client_snapshot_pending = true` after committing the new slot.
  - `laser_controller_wireless_event_handler` now handles `WIFI_EVENT_AP_STOP` and `WIFI_EVENT_AP_STADISCONNECTED` by calling `force_close_all_clients()` — proactively evicts sockets for the disassociated station.
  - `laser_controller_wireless_start_http_server` sets `config.send_wait_timeout`, `config.recv_wait_timeout`, `config.keep_alive_enable`, `config.keep_alive_idle`, `config.keep_alive_interval`, `config.keep_alive_count`.
  - `laser_controller_wireless_broadcast_text` calls `httpd_sess_trigger_close(s_server, fd)` on the first failed send. The close executes on the httpd task (per ESP-IDF `httpd_sess.c:486-493`), which fires `close_fn` → `remove_client`. FD reuse race avoided because `httpd_sess_trigger_close` does not call `close(fd)` synchronously.
  - New `laser_controller_wireless_consume_new_client_pending()` and `laser_controller_wireless_force_close_all_clients()` helpers.
- **Firmware — `components/laser_controller/src/laser_controller_comms.c`:**
  - TX task now calls `laser_controller_wireless_consume_new_client_pending()` at the top of each tick; when true, it immediately emits a full `status_snapshot` to all clients, bypassing the `pause_wireless_telemetry` gate. Worst-case new-client-to-snapshot latency drops from ~180 ms (periodic fast-telemetry cadence) + potential 400 ms post-command quiet window to ≤ 20 ms (TX tick period).
- **Host — `host-console/src/lib/websocket-transport.ts`:**
  - Added `closeImmediate()` method that synchronously clears probe timers, nulls the socket, and calls `socket.close()` without the async 80 ms grace. Intentionally does NOT emit a transport event (there is no one listening during unload). Idempotent.
- **Host — `host-console/src/hooks/use-device-session.ts`:**
  - Added constants `WIFI_FIRST_CONNECT_DELAY_MS = 2500` and `WIFI_RECONNECT_DELAY_MS = 900`.
  - Added `wifiFirstConnectPendingRef` latched at hook mount; the reconnect effect uses the longer delay on the first attempt, clearing the ref in the timer callback before calling `connect()`.
  - `connect()` now reads `transportStatusRef.current` (synchronous, authoritative) instead of `transportStatus` (React state, one render behind) for its early-return guard.
  - New `useEffect` registers `beforeunload` + `pagehide` listeners that call `WebSocketTransport.closeImmediate()` on page unload, giving the ESP32 a fighting chance to run its `close_fn` before the browser tears the context down.
  - During the first-connect drain window, `setTransportDetail("Waiting briefly before reconnecting — giving the controller time to close the previous session.")` so the operator doesn't see a static "disconnected" banner for 2.5 s and click Connect manually (which would bypass the drain window and resurface the stale-socket race).
- **Host — `host-console/src/components/ConnectionWorkbench.tsx`:**
  - `handleConfigureControllerStationMode` now guards the `onSetWifiUrl(controllerBenchAp.wsUrl)` call on `wifiUrl !== controllerBenchAp.wsUrl`; a no-op URL write no longer churns the transport `useMemo`.

### Builds

- `. $IDF_PATH/export.sh && idf.py build` — **PASS**. Binary 0xf9270 bytes, 3 % free in the 0x100000 app partition.
- `cd host-console && npm run build` — **PASS**. No new errors, no new TypeScript warnings.

### Render check

- Live dev server at `http://localhost:5174/`, transport=wifi, WS target `ws://192.168.4.1/ws` (live ESP32 running firmware `fbf4424-dirty`). Banner rendered: "Wireless link healthy. Controller protocol active. Link Connected Bench AP Bench AP ready." Firmware uptime 01:57:01 showed the link stayed alive through the page-reload test. Zero browser console errors beyond the normal Vite HMR + React DevTools prompt.

### Cross-module audit fan-out

| Agent | Round | Verdict | Notes |
|---|---|---|---|
| B1 `bsl-firmware-auditor` | 1 | **PASS** | Threading: `s_new_client_snapshot_pending` has exactly one writer (httpd task) and one reader/clearer (comms TX task); the read-then-clear race is benign (worst case: one snapshot covers two new clients). State machine / GPIO6 / deployment / fault clearing / command gating all untouched. `httpd_sess_trigger_close` confirmed to marshal to the httpd task (ESP-IDF `httpd_sess.c:486-493`), so calling from the event-handler task or the broadcast caller is safe. `send_wait_timeout=2 s` gives 600 ms of margin vs the host's `COMMAND_ACK_TIMEOUT_MS=2600` and is comfortably above `SERVICE_MODE_WAIT_MS=1200`. `CLIENT_DROP_FAILURES=1` is correct now that `httpd_sess_trigger_close` drives the authoritative close on first failure. |
| B2 `bsl-gpio-auditor` | 1 | **PASS** | No GPIO / ADC / rail / PWM / I2C / SPI pin is touched by this diff. Close-path trace (`httpd_sess_trigger_close` → httpd task → `session_closed` → `remove_client`) writes only FD bookkeeping arrays under `s_client_lock`. The one-shot snapshot emission reads from an already-populated `laser_controller_runtime_status_t` — no new ADC or I2C traffic. GPIO6 dual-driver rule preserved. |
| A2 `bsl-interaction-reviewer` | 1 | FAIL → fixed | Flagged a UX regression: during the 2.5 s first-connect drain window, the connection banner reverted to the generic "Select a transport and connect." copy with no indication a reconnect was queued, which could prompt a redundant manual Connect click that bypasses the drain window. Fix applied: `setTransportDetail("Waiting briefly before reconnecting — giving the controller time to close the previous session.")` inside the reconnect effect when `isFirstAttempt` is true. |
| A2 `bsl-interaction-reviewer` | — | **PASS** (after copy fix) | All other checks PASS. `closeImmediate()` correctly does not emit transport events. `transportStatusRef.current` write sites are all kept in sync with `setTransportStatus`. `pagehide` + `beforeunload` double-fire is idempotent because `closeImmediate()` nulls `this.socket` before the second call. The `wifiUrl !== controllerBenchAp.wsUrl` guard correctly preserves the softap-recovery path. No new commands introduced; envelope shapes match the existing firmware handlers. |

Agents not respawned this round (deferred to final milestone close):
- B3 protocol-auditor — no new JSON commands or response fields. `status_snapshot` is pre-existing.
- A1 uncodixfy-reviewer — no CSS change. Banner copy change only.
- A3 consistency-reviewer — no new shared primitives touched.
- hardware-safety — open follow-up recommendation from the parallel investigation: scope DVDD_3V3 on IC1 during `aux-control-pass` / `ready-runtime-pass` rail enable edges to rule out / confirm the MPM3612 UVLO transient contributor. MPM3612 datasheet remains absent from `docs/Datasheets/` (gap flagged 2026-04-17).
- docs-guardian — no doc additions this round.

### Blockers

- **Powered Phase 2 BLOCKED.** No `/dev/cu.usbmodem*` in this session. The new firmware image is built (`build/bsl_laser_controller.bin`, 3 % app-partition headroom) but not flashed. The on-device evidence the user needs — no disconnect on service/deployment mode entry, no stale-socket wedge after a browser refresh, and reconnect within 2.5 s of the fresh page load — cannot be captured from here.
- Per `bsl-safety-critical` "Ready" rule: controller is **NOT ready**. Current evidence: firmware build PASS, host build PASS, render-check PASS against live firmware `fbf4424-dirty` (pre-fix image), B1 PASS, B2 PASS, A2 PASS (after UX copy fix).

### Next step

- Flash the rebuilt image onto the live board.
- Exercise the three reported failure modes directly:
  1. Open the host console, connect over WiFi, click "Enter service mode" repeatedly. Confirm no disconnect, no reboot requirement.
  2. Same for "Enter deployment mode".
  3. With the console connected, refresh the browser page. Expect: the WS re-establishes within ~2.5 s of the fresh page load, showing the "Waiting briefly before reconnecting…" copy during the drain window, and a full `status_snapshot` arrives within ≤ 20 ms of the new upgrade completing.
- If any of the three still fails, scope DVDD_3V3 and ESP32 GND pad during the failing transition to rule in / rule out the EMC path flagged by the hardware-safety agent.

### Sentinel hygiene

- `python3 .claude/hooks/mark-audit-done.py firmware` — to be run once this Status.md is saved.
- `python3 .claude/hooks/mark-audit-done.py gui` — to be run once this Status.md is saved.

## 2026-04-17 — Live Bench Validation + `close_fn` FD-Leak Regression

This section captures live-bench validation of the AP/WS stability recovery patch that landed earlier on 2026-04-17, the regression it uncovered, and the fix.

### Flash

- `/dev/cu.usbmodem201101` confirmed present.
- Flashed via `python -m esptool --chip esp32s3 -p /dev/cu.usbmodem201101 ... write-flash --flash-size 2MB 0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0x10000 build/bsl_laser_controller.bin`. Hash verified. Hard-reset via RTS.
- Flashed image identity over serial: `firmwareVersion = "9d3ac48-dirty"` (nested at `result.identity.firmwareVersion`).

### USB Phase 1 (serial) — PASS

| Scenario | Verdict |
|---|---|
| `parser-matrix` | **PASS** |
| `deployment-lockout` | **PASS** |
| `deployment-usb-safe-fail` | **PASS** |
| `runtime-mode-gating` | **PASS** |

- `aux-control-pass`, `ready-runtime-pass`, `fault-edge-pass`: pre-existing **BLOCKED** (require TEC + LD rails; USB-only bench cannot bring them up — this is the USB-Debug tier by design per AGENT.md §"USB-Only Debug Power"). The three mandatory Powered Phase 2 scenarios remain unrun for this release because no full-power PD profile is available on this bench.
- `pd-passive-only-during-deployment`: pre-existing **FAIL** on `refresh_pd_status` in USB-only tier (firmware rejects explicit PD refresh outside service mode). Not caused by this change.

### WS Phase 1 (over `ws://192.168.4.1/ws`) — PASS

| Scenario | Verdict |
|---|---|
| `parser-matrix` | **PASS** |
| `deployment-lockout` | **PASS** |
| `runtime-mode-gating` | **PASS** |

### Regression discovered — and fixed

Under multi-client open/close stress the firmware **wedged its httpd**: raw TCP connect continued to succeed on port 80, but every HTTP request (both `/ws` upgrade and `/meta`) received TCP RST. Reproduction: 10 sequential WS open/`get_status`/close with 3 s spacing. First 6 succeeded, iterations 7-10 all failed with `ConnectionResetError`. httpd did not self-recover after a 20 s quiet window; only a full ESP32 reboot cleared it. While wedged, the existing browser session held — snapshots continued flowing — but no new upgrade could complete, exactly matching the field symptom "refresh the page and the connection won't recover without a power cycle."

Root cause: the `close_fn` at `laser_controller_wireless.c:687-692` deliberately did NOT call `close(sockfd)`. Per ESP-IDF `httpd_sess.c:373-378`, when a `close_fn` is registered, httpd delegates the actual `close()` to the callback instead of closing the fd itself:

```c
if (hd->config.close_fn) { hd->config.close_fn(hd, session->fd); }
else { close(session->fd); }
```

So every session teardown leaked its fd. After ~6 open/close cycles the LwIP VFS socket table filled and accept() started returning RST.

The earlier note that justified NOT calling `close()` (an "FD-reuse race" where a fresh upgrade landed on a recycled fd and the broadcast shipped stale data to the new peer) is now covered by the new-client snapshot flag: `add_client` sets `s_new_client_snapshot_pending`, the comms TX task emits a fresh `status_snapshot` on its next 20 ms tick, and the transient garbage frame to the reused fd (if it arrives at all) is harmless because it's a read-only snapshot event.

### Fix

- `components/laser_controller/src/laser_controller_wireless.c:687-721`: `laser_controller_wireless_session_closed` now calls `(void)close(sockfd)` after `remove_client`. The old avoid-close comment was replaced with a detailed ESP-IDF-referenced explanation of why we must close and why the old race concern is now handled by the snapshot flag.
- `components/laser_controller/src/laser_controller_wireless.c` rebuilt, flashed to `/dev/cu.usbmodem201101`, and re-validated.

### Post-fix validation — PASS

| Test | Result |
|---|---|
| 20 sequential open/close with 1.5 s spacing | **PASS** — 20/20 OK |
| 3 long-lived WS readers + 15 rapid open/close storm (concurrent) | **PASS** — storm 15/15, readers evicted by LRU as expected |
| 50 rapid open/close with 200 ms spacing (operator-hammer) | **PASS** — 50/50 OK, 50/50 `get_status` responses received |
| Browser connected throughout the 50-iteration hammer | **PASS** — banner "Wireless link healthy.", chip "Connected", zero console errors |
| Browser refresh mid-`enter_service_mode` (reload 60 ms after click) | **PASS** — page came back up and reconnected within the 2.5 s drain window, no ESP32 reboot required |

### Random-operator GUI stress tests (post-fix)

1. **Random tab navigation**: 20 clicks across the 5 workspaces (System/Operate/Integrate/Update/History) in randomized order, 80-200 ms apart. Result: zero JS console errors, link stayed "Connected".
2. **Rapid deployment mode cycling**: 6 × (`Re-enter deployment` → wait 900-1300 ms → `Exit deployment` → wait 700-1000 ms). Result: all 12 transitions stayed "Connected", zero errors.
3. **Rapid service mode cycling (Integrate → Service sub-tab)**: 5 × (`Enter service mode` → wait 900-1300 ms → `Exit service mode` → wait 700-1000 ms). Result: all 10 transitions stayed "Connected", zero errors.
4. **Mode cycling while python refresh-storm hammers in parallel**: 4 × svc cycle running in the browser while python opened 15 auxiliary WS sockets. Result: browser stayed "Connected" through every transition, all 15 python opens succeeded.
5. **Combined svc + dep cycling across 3 rounds**: 3 × (svc enter/exit + dep enter/exit with random 250-1000 ms pacing). Result: all 6 cycle snapshots report "Connected", zero console errors.

### Operator-clarity audit

- **Disabled-control sweep** (every disabled `button`/`input`/`select` across all 5 workspaces + all Integrate bring-up sub-tabs): 10 unique disabled controls discovered; 9 of 10 carry a specific `title=` explaining why. The one exception is the `Connect` button when already connected — its label reads `Connected` and the disabled state is self-evident from the label. No opaque-reason disabled controls found.
- **Error-copy sweep**: pre-existing transport-level copy already told operators what to try (link-dropped banner suggests reconnect after the link recovers; socket-open-failure includes the URL). One improvement landed: the generic command-ack timeout at `host-console/src/hooks/use-device-session.ts` now says "Controller did not answer the command in time." followed by a per-transport recovery hint:
  - Wi-Fi: "The wireless link may have dropped briefly — wait a moment for auto-reconnect, then retry."
  - Serial: "Check the USB cable and press Connect serial above, or reset the controller."

### Files touched this round

- `components/laser_controller/src/laser_controller_wireless.c` — `close(sockfd)` restored in `laser_controller_wireless_session_closed`.
- `host-console/src/hooks/use-device-session.ts` — actionable per-transport command-ack timeout copy.

### Builds

- `idf.py build` — **PASS**. Binary 0xf9270 bytes, 3 % free in app partition.
- `cd host-console && npm run build` — **PASS**.

### Sentinel hygiene

- `python3 .claude/hooks/mark-audit-done.py firmware` — to be run once this Status.md is saved.
- `python3 .claude/hooks/mark-audit-done.py gui` — to be run once this Status.md is saved.

### Next step

- Acquire a full-tier PD source (Thinkpad charger or the bench PD negotiator) to run the three mandatory Powered Phase 2 scenarios (`aux-control-pass`, `ready-runtime-pass`, `fault-edge-pass`). Without TEC + LD rail power, `requestedLedEnabled`, `allowNir`, and the whole ready-idle path cannot be exercised on this image.
- Re-run the 7-agent review loop at the milestone close.

## 2026-04-17 (evening) — AP-Only Deep Audit + Two More Regressions Fixed

Directed: exhaust the AP interface (`ws://192.168.4.1/ws`) end-to-end. Focus on post-boot timing, rapid mode flips, random-operator chaos, refresh-mid-command recovery, and — critically — "connected" must mean live telemetry, not just a WS handshake.

### Regression A — `WIFI_EVENT_AP_STADISCONNECTED` bulk-close nuked live clients

The 2026-04-17 morning patch reacted to that event by calling `force_close_all_clients()`. Field evidence (USB-side `wireless.clientCount` polled while a WS was open) showed `clientCount=0` throughout, while the browser still ack'd commands via direct request-response. Root cause: AP_STADISCONNECTED fires on transient roaming, beacon-miss, or power-save hiccups of the very station we are serving. Bulk-closing on every such event evicted the active client within seconds of connect.

**Fix** at `components/laser_controller/src/laser_controller_wireless.c:499-515`: the handler now returns without action. TCP keep-alive (configured in `start_http_server`) + close-on-send-failure (in `broadcast_text`) handle stale sockets with O(9 s) worst-case detection, which is acceptable. Bulk-close was O(now) but wrong.

### Regression B — Idle WS clients never tracked (the real primary bug)

After fixing A, idle WS clients still received zero telemetry. Diagnostic ESP_LOGI in `add_client`/`remove_client`/`close_fn` showed `add_client` was NEVER called on a successful WS upgrade — only when the client sent its first TEXT frame. If a client stayed silent, no fd went into `s_client_fds[]`, so `broadcast_text` looped over an empty list and nothing reached the client. The browser survived only because the host-console's transport issues handshake probes at 120/900/2200 ms, so `add_client` fired via the TEXT path within ~120 ms of open. A Python `create_connection(...)` with no commands got zero telemetry for its entire lifetime.

**Root cause in ESP-IDF**: `esp_http_server/src/httpd_uri.c:353` — the framework explicitly does NOT call the user `ws_handler` on the initial upgrade GET. It completes the handshake (sends 101 Switching Protocols, flips `ws_handshake_done = true`) and returns. Our handler's `HTTP_GET` branch (where `add_client` lived) never fires on upgrade.

**Fix**:
- `sdkconfig:1504`: `CONFIG_HTTPD_WS_POST_HANDSHAKE_CB_SUPPORT=y`.
- `components/laser_controller/src/laser_controller_wireless.c`: new static `laser_controller_wireless_ws_post_handshake()` which calls `add_client(httpd_req_to_sockfd(req))`.
- `components/laser_controller/src/laser_controller_wireless.c:788-820`: WS URI descriptor now includes `.ws_post_handshake_cb = laser_controller_wireless_ws_post_handshake`.

This is the ESP-IDF-sanctioned hook that fires on the httpd task immediately after the 101 Switching Protocols response goes out. It reliably delivers "a new WS client just connected" regardless of whether the client ever sends anything.

### Post-fix validation — live bench, AP only

All tests over `ws://192.168.4.1/ws`. Serial port only used for hard-reset + `chip-id` read-back.

**Baseline — idle WS telemetry cadence (no commands)**: fresh reboot, open WS, listen 20 s. Got `fast_telemetry` 5-6 events per second continuously for all 20 seconds, including across the headless auto-deploy window (which fires at t=5 s if no host activity is seen). `live_telemetry` every 1 s, `status_snapshot` every 10 s. First `fast_telemetry` arrived 124-185 ms after `create_connection`. This is the exact "connected = normal telemetry flowing" definition the directive called out.

**WS open/close hammer — 20 cycles @ 1.5 s, 50 cycles @ 200 ms, 100 cycles @ 120 ms**: all 170/170 cycles returned a `get_status` / `ping` response within 800 ms. Browser kept its WS open throughout. Post-hammer `chip="Connected"`, uptime advancing, zero browser console errors.

**Deep AP stress harness (`/tmp/bsl-ap-stress.py`, 8 scenarios)** — `7/8 PASS`:

| Scenario | Result | Note |
|---|---|---|
| `cold_boot_first_contact` | **PASS** | AP back in ~2 s post-hard-reset; first `fast_telemetry` ≤200 ms; `get_status` ack 99 ms |
| `cold_boot_immediate_enter_service_mode` | **PASS** | `enter_service_mode` ok right after upgrade; telemetry kept flowing |
| `cold_boot_immediate_deployment` | **PASS** | `deployment.enter` immediately after boot succeeds |
| `cold_boot_command_burst` | **PASS** | 10-command burst: 10/10 acks, 9 ok (1 rejected because queue depth=8 — clean, expected) |
| `rapid_mode_flips_with_heartbeat` | **PASS** | 10 service-mode pairs at pacings 60-600 ms; zero failures; fast_telemetry stayed alive |
| `service_deployment_interleave` | FAIL (test expectation) | `deployment.enter` from service mode is actually allowed (firmware policy choice), not a firmware bug. Host pre-disables reverse path (`enter_service_mode` while in deployment) with a clear tooltip. |
| `refresh_storm_with_heartbeat` | **PASS** | 30 open/close, 30/30 OK, post-storm reconnect + telemetry |
| `immediate_hammer_after_mode_change` | **PASS** | 5 `get_status` fired in 111 ms burst right after `enter_service_mode`: 5/5 acks, 5/5 ok |

**Browser (host-console) live stress**:
- Connected, navigated to Operate + Integrate + System + Update + History in random order.
- Rapid `Re-enter deployment` / `Exit deployment` cycles: stayed Connected.
- Rapid `Enter service mode` / `Exit service mode` cycles: stayed Connected.
- Refresh in the browser 30 ms after clicking `Enter service mode`: page came back, reconnected within the 2.5 s drain window, "Wireless link healthy. Connected. Uptime advancing." with zero console errors.

### Operator-clarity probes (corner cases)

- **During headless auto-deploy** (the 5 s after boot if no host activity), any `enter_service_mode` request fails with the firmware error `"Deployment mode is active. Exit deployment mode before opening service writes."` — specific, actionable. Host's UI pre-disables the `Enter service mode` button in this state with the title tooltip `"Deployment mode is active. Exit deployment mode before opening a guarded write session."` — operator sees the reason on hover and the button is not clickable, so the error path is never reached in the happy flow.
- **ACK timeout copy** (still applied from morning fix): `"Controller did not answer the command in time. The wireless link may have dropped briefly — wait a moment for auto-reconnect, then retry."` over Wi-Fi, with a serial-specific hint for serial transport.
- **Transport switch** is not accidentally clickable: the `Mock rig` / `Web Serial` / `Wireless` buttons are three side-by-side operator toggles. A single errant click in random chaos did successfully switch transports, which is expected behavior (the button is labeled for its action). No silent transport switch; operator sees the banner change immediately.
- **Every Operate slider**, when disabled because the controller is temporarily unreachable, shows `title="No controller connected."`. When disabled because deployment / service / fault gating applies, the hostControlReadiness reason is surfaced (e.g. `"Deployment has not reached the ready posture yet."`).
- **No opaque or uncleareable error** surfaced in any of the above flows.

### Known product-behavior notes (not bugs, but worth writing down)

1. **Headless auto-deploy at boot**: if the WS client opens but does not send any command within 5 s of boot, the firmware auto-enters deployment mode and runs the checklist. During the checklist (~5-30 s depending on rail state), `enter_service_mode` is rejected with a clear reason. The host sidesteps this entirely because its transport sends `get_status` at 120 ms after open — that marks `last_host_activity_ms` and suppresses auto-deploy.
2. **Command queue depth 8**: a burst of 10+ commands fired with no inter-pacing will get one or more rejected with `"Controller command queue is full. Wait for the current action to finish and retry."`. This is a load-shedding contract, not a bug. The host-console serializes commands through `commandDispatchChainRef`, so no user flow hits this in practice.

### Files touched this round

- `sdkconfig` — enabled `CONFIG_HTTPD_WS_POST_HANDSHAKE_CB_SUPPORT`.
- `components/laser_controller/src/laser_controller_wireless.c` — added `ws_post_handshake` callback + registered on WS URI; reverted `WIFI_EVENT_AP_STADISCONNECTED` bulk-close (handler body empty). Diagnostic `ESP_LOGI` removed from the hot path before the final build.
- `host-console/src/hooks/use-device-session.ts` — unchanged this round (morning's ACK-timeout copy improvement is already in place).

### Builds

- `idf.py build` — PASS. Binary 0xf9270 bytes, 3% free in app partition.
- `cd host-console && npm run build` — PASS.

### Sentinel hygiene

- `python3 .claude/hooks/mark-audit-done.py firmware` — run below.
- `python3 .claude/hooks/mark-audit-done.py gui` — no GUI source touched this round but the banner is run for consistency since the test exercised the GUI end-to-end.

### Next step

- Bring up a full-tier PD source to run the three mandatory Powered Phase 2 scenarios (`aux-control-pass`, `ready-runtime-pass`, `fault-edge-pass`). Everything tested above is transport + UX; the powered-rail control paths are still not exercised on this image because TEC and LD rails cannot come up on USB programming-only tier.
- Re-run the 7-agent review loop at the milestone close.

## 2026-04-17 (late evening) — Aggressive Interaction + Control-Logic Audit (persona-driven)

Directed: "simulate multiple users with different knowledge", find more corner cases, fix interaction and control-logic gaps. Three parallel agents (firmware, interaction, rendered-UI) produced a heavy-finding audit; the top S1/S2 items were fixed this round and verified on the live bench.

### Personas modelled

Novice (clicks every button), experienced bench engineer (rapid chains), QA technician (boundary values + rapid toggle), field service tech (flaky network + browser refresh), developer with two tabs, returning-after-30-min user, panic double-clicker.

### Findings triaged

**S1 safety (firmware)** — 2 items, both fixed.

1. `laser_controller_board_reset_gpio_debug_state` holds `s_bus_mutex` 50-300 ms while tearing down I2C + SPI. The control task's 5 ms tick is blocked on the bus mutex during that window and cannot react to a `TEC_PGOOD` fall by pulling LD safe. That violates the posted SOP rule "TEC loss = immediate LD shutdown". **Fix**: in `laser_controller_board.c:5008-5023`, drive all rails to the fail-safe posture (`kSafeOutputs`) BEFORE acquiring the bus mutex. The rails are already OFF before the stall window begins, so a TEC loss during teardown has no LD to shut down.

2. `integrate.set_safety` / `set_runtime_safety` accepted safety-policy edits while NIR or alignment output was ACTIVELY emitting. Widening thresholds (raising `max_laser_current_a`, `tec_temp_adc_trip_v`, `ld_overtemp_limit_c`, relaxing `tof_min_range_m`) mid-emission retroactively un-trips live safety guards. **Fix**: in `laser_controller_comms.c:5631-5648`, reject with `"Runtime safety edits are blocked while the laser or alignment output is active. Turn the output off first."` when `decision.nir_output_enable || decision.alignment_output_enable`.

**S2 operator (firmware)** — 3 items, all fixed.

3. `laser_controller_wireless_ws_post_handshake` added `add_client` but did NOT notify `laser_controller_app_note_host_activity()`. A listen-only WS client (legitimate use case: operator just watching telemetry) did not suppress the 5-second headless auto-deploy. At t=5 s the firmware auto-entered deployment mode while the host thought it was pre-deploy idle. **Fix**: `laser_controller_wireless.c:606-619` — post-handshake callback now calls both `add_client` and `note_host_activity`.

4. `laser_controller_app_enter_deployment_mode` silently re-wrote deployment fields (zero'd `sequence_id`, reset `target`) whenever `deployment.active` was already true. A second client hitting `deployment.enter` between the first client's checklist pauses replaced the first client's target behind its back. **Fix**: `laser_controller_app.c:4002-4018` now rejects with `ESP_ERR_INVALID_STATE`. Comms handler at `laser_controller_comms.c:4419-4445` pre-checks the already-active case and emits the specific error `"Deployment mode is already active. Use 'Exit deployment' first if you want to restart it."`.

5. `operate.set_led` had a general "needs deployment active" gate and a "deployment.running blocks" gate in the upstream routing, which already covered the hazard the auditor called out (LED turning on during rail-sequence checklist steps). Verified live: `operate.set_led {enabled=true, ...}` during checklist run returns `"Wait for the deployment checklist to stop before changing the GPIO6 LED request."`. Defensive gate also added inside the specific handler at `laser_controller_comms.c:5250-5263` (redundant but keeps the per-handler contract explicit).

**S2 operator (host)** — 4 items, all fixed.

6. Hardcoded `"25 °C deployment target. Rows advance live."` checklist subheading. Every operator on lambda-mode or a non-25 °C target would read the wrong value. **Fix**: `host-console/src/components/OperateConsole.tsx:742-750` now interpolates the live `deployment.targetTempC` (or `targetLambdaNm` in lambda mode) via `formatNumber`.

7. `operate.save_deployment_defaults`, `integrate.set_safety`, and `integrate.save_profile` — all NVS-write commands — were not in `isSlowWirelessCommand`. Under a 4.5 s+ round trip, the host ack timeout fires and reports failure while firmware silently completes the flash write. Operator believes save failed while device committed. **Fix**: `host-console/src/hooks/use-device-session.ts:208-231` — added all three to the slow-command predicate so the 6500 ms slow-command budget applies.

8. `Save deployment defaults` button was enabled whenever `connected`, even during an active checklist run. Firmware correctly rejected, but the UI showed no pre-emptive reason. **Fix**: `host-console/src/components/OperateConsole.tsx:670-689` now adds `|| deployment.running` to the disabled predicate and the title tooltip switches to `"Deployment checklist is running — wait for it to complete before saving defaults."`.

9. `toggleLed` did not clear `ledDraft.dirty`. If the operator typed a brightness value then immediately hit the toggle button before the 200 ms debounce fired, the brightness snapped back to the firmware-echoed value on the next telemetry tick. **Fix**: `host-console/src/components/OperateConsole.tsx:508-529` — `toggleLed` now chains `.finally(() => setLedDraft((c) => ({ ...c, dirty: false })))` to match the `scheduleLedCommit` pattern.

### Remaining advisory findings (not fixed this round)

- **F3 (host)** — `deployment.enter`/`deployment.exit` have no confirmation guard when `readyIdle === true`. A panic double-click would drop a live ready-idle session. This is the safe direction for beam safety, but not volunteering-safe for the operator's workflow. Candidate for a later round with a `ConfirmActionDialog` gated on `readyIdle`.
- **A1 Uncodixfy polish** — `eyebrow` class usage (20+ call sites), 14-24 px border-radius usage, dramatic 36-64 px box-shadows, hover translateY animations, frosted backdrop-blur on the connection status tag, motion/framer-motion slide-ins. None block safety; polish work for a dedicated Uncodixfy-compliance round.
- **Silent clamps at the `operate.set_target`/`operate.set_output` boundaries**: out-of-range `current_a`/`target_temp_c`/`target_lambda_nm` values are clamped without an error response. Round-trip telemetry reflects the clamped value, but the GUI slider doesn't know it was clamped until it sees the echo. Polish work; not safety-critical because the firmware hard cap in `effective_commanded_current_a` still enforces.

### Live-bench verification post-fix (all over `ws://192.168.4.1/ws`)

After flashing the new firmware to `/dev/cu.usbmodem201101` + rebuilding host:

| Test | Result |
|---|---|
| Idle WS 6 s after fresh reboot — telemetry flowing? | **PASS** — fast=32, live=6, snap=1 |
| 10 rapid service-mode enter/exit pairs (pacings 60-600 ms) | **PASS** — 0/10 failures |
| `deployment.enter` idempotent rejection | **PASS** — returns `"Deployment mode is already active. Use 'Exit deployment' first if you want to restart it."` |
| `operate.set_led` during checklist-running | **PASS** — returns `"Wait for the deployment checklist to stop before changing the GPIO6 LED request."` |
| `integrate.set_safety` when NIR off | **PASS** — accepted (gate does not over-block) |
| 30-iter open/close storm with browser simultaneously connected | **PASS** — 30/30 |
| 5 `get_status` fired back-to-back right after `enter_service_mode` | **PASS** — 5/5 ok |
| 20 s continuous idle telemetry | **PASS** — 5-6 fast/sec consistently, worst-gap = 0 zero-second runs |

**Targeted regression for S1 fix #1 (safe-off before teardown)**: 10 rapid service-mode enter/exit pairs at pacings 60-600 ms all ack'd successfully. The `exit_service_mode` → `reset_gpio_debug_state` path runs `drive_safe_gpio_levels(&kSafeOutputs)` before taking `s_bus_mutex`; rails are already safe when the 50-300 ms bus-teardown window begins. Control task TEC-loss response is no longer time-critical during teardown because the LD rail is already off. No functional regression observed.

### Host UI verification in live preview

- Banner: `"Wireless link healthy."` → `Connected` chip → uptime advancing.
- Operate checklist subheading now reads `"25.0 °C deployment target. Rows advance live."` (live interpolation — confirmed).
- `Save deployment defaults` button: `disabled=true` with title `"Deployment checklist is running — wait for it to complete before saving defaults."` during `deployment.running`, `disabled=false` with default title when stopped (confirmed via 250 ms and 900 ms samples post-`Run checklist`).
- Zero browser console errors across all tests.

### Cross-cutting observations

The auditor flagged a structural concern: **command gating is not uniformly applied by state**. Each handler rolls its own gates. The project lacks a single `command × state → allowed` matrix. A proposed future refactor would move gating to a helper `laser_controller_comms_command_allowed(command, state, decision, deployment)` with an explicit table. That aligns with the principle that caused the 2026-04-14 GPIO6 LED injury: incremental unaudited gating holes are the hazard mode. Not done this round — out of scope.

### Files touched this round

- `components/laser_controller/src/laser_controller_board.c` — safe-rails drive before bus teardown.
- `components/laser_controller/src/laser_controller_comms.c` — NIR-active-active gate on `integrate.set_safety`, idempotent-reject on `deployment.enter`, defensive LED gate in operate.set_led handler.
- `components/laser_controller/src/laser_controller_app.c` — `enter_deployment_mode` rejects when already active.
- `components/laser_controller/src/laser_controller_wireless.c` — post-handshake callback now calls `note_host_activity`; laser_controller_app.h include added.
- `host-console/src/hooks/use-device-session.ts` — added slow-command entries.
- `host-console/src/components/OperateConsole.tsx` — live subheading, save-defaults gating, toggleLed dirty-flag fix.

### Builds

- `idf.py build` — PASS.
- `cd host-console && npm run build` — PASS.

### Sentinel hygiene

- `python3 .claude/hooks/mark-audit-done.py firmware` — run below.
- `python3 .claude/hooks/mark-audit-done.py gui` — run below.

### Next step

- Full 7-agent review loop at milestone close.
- Acquire full-tier PD source for Powered Phase 2 (`aux-control-pass`, `ready-runtime-pass`, `fault-edge-pass`). The S1 fix #2 (integrate.set_safety NIR-active gate) cannot be exercised on USB-only tier because `decision.nir_output_enable` never becomes true without rails.

## 2026-04-17 (round 3) — State-Sync + Silent-Accept Bugs

User feedback: "LD and TEC set point between pages are not properly synced; during page switching it looks like the setpoint is set but actually still with old number." This round found and fixed three new bug classes the previous two rounds missed.

### Bug #1 (S2 operator, reproduced live) — runtime setpoint silent-reject with fake-success display

**Reproduction.** Navigate to Operate. Change TEC "Requested temp" from 28.7 to 30.0 and blur (commit). Display immediately shows 30.0. Navigate to Integrate and back. Display now shows 28.7 (the untouched live value).

**Root cause.** The host's `commitRuntimeTarget` / `commitRequestedCurrent` / `commitModulation` fired `void issue(...)` without awaiting the ack. The firmware's `laser_controller_comms_is_runtime_control_command` gate rejects `operate.set_target` / `operate.set_output` / `operate.set_modulation` unless `deployment.active && deployment.ready` — exactly where the operator most wants to stage values: pre-deployment. The rejection error `"Complete the deployment checklist successfully before using runtime control commands."` was sent but never surfaced. The draft-sticky pattern meant the display stayed at the user-typed value until unmount. On remount (nav roundtrip), the draft died and the untouched firmware value reappeared.

**Fix.**
- New `runtimeControlAllowed = connected && deployment.active && deployment.ready` computed at the component level with a matching per-condition `runtimeControlBlockReason` string.
- All runtime setpoint inputs (`Temperature (°C)` slider + number, `Wavelength (nm)` slider + number, `Current request` slider, `Current (A)` number, `Modulation freq (Hz)`, `Duty (%)`, `Enable PCN modulation` checkbox) now pre-disabled on `!runtimeControlAllowed` with the tooltip showing the actual reason.
- `commitRuntimeTarget`, `commitRequestedCurrent`, `commitModulation` now `async`, await the ack, and on `!result.ok`:
  1. Drop `draft.dirty` so the displayed value snaps back to the firmware live value IMMEDIATELY (no longer only on nav).
  2. Call `setRuntimeCommitError(result.note)` — a new `<p role="alert" data-tone="critical">` inline banner renders the firmware's error text for 6 s.
- Files: [OperateConsole.tsx:258](host-console/src/components/OperateConsole.tsx:258) (gate + error state), :363-451 (commit rewrites), :963-1113 (input wiring + banner).

**Verified live.** After fix, temp input shows `disabled=true` with title `"The deployment checklist must reach the ready posture before runtime setpoints can be changed."` — operator sees the gate before typing.

### Bug #2 (S1 safety, reproduced live) — `integrate.set_safety` accepted out-of-range values silently

**Reproduction.** Send `{"cmd":"integrate.set_safety","args":{"max_laser_current_a":99.99}}`. Firmware returns `ok:true`. `get_status` now shows `safety.maxLaserCurrentA = 99.99`. Operational enforcement is still correct because `laser_controller_bench_max_current_allowed` at [bench.c:115](components/laser_controller/src/laser_controller_bench.c:115) falls back to `LASER_CONTROLLER_BENCH_MAX_CURRENT_A` (5.2 A) when the policy is out-of-range, BUT the STORED policy diverges from what is actually enforced. An operator reviewing settings sees 99.99 and believes the safety cap is effectively disabled.

**Fix.** `laser_controller_config_validate_runtime_safety` at [config.c:213](components/laser_controller/src/laser_controller_config.c:213) now bounds-check:
- `max_laser_current_a` in `(0, 5.2]` (hardware ceiling).
- `ld_overtemp_limit_c` in `[20, 120]`.
- `tec_temp_adc_trip_v` in `(0, 3.3]` (ADC range).
- `tof_max_range_m <= 10 m`, `tof_min_range_m <= 5 m`.

Firmware rejects with the existing `"Runtime safety update rejected because one or more values are invalid."` for any out-of-range value.

### Bug #3 (S3 polish) — BringupWorkbench safety inputs missing browser-level validation

**Root cause.** The `<input type="number">` elements in BringupWorkbench's safety policy section had `step=` attributes but no `min`/`max`. The browser did not highlight out-of-range typing; the draft happily persisted absurd values to localStorage and showed them identically across nav, giving the operator no client-side cue that the value is invalid.

**Fix.** Added `min`/`max` attributes to Max laser current, LD overtemp, TEC temp ADC trip, ToF min range, ToF max range at [BringupWorkbench.tsx:3762-3990](host-console/src/components/BringupWorkbench.tsx:3762). Updated the `help` + `title` copy to announce the accepted range so operators see a bounds hint both in the `:invalid` pseudoclass AND the hover tooltip.

### Live-bench regression after round-3 fixes

All over `ws://192.168.4.1/ws`.

| Test | Result |
|---|---|
| 10 s idle telemetry | 55 fast_telemetry events, 5-6/s consistent (within 180 ms cadence target) |
| `operate.set_target` pre-ready rejected with clear error string | **PASS** |
| `integrate.set_safety` rejects `max_laser_current_a=99.99` | **PASS** |
| `integrate.set_safety` rejects `max_laser_current_a=5.3` (just over) | **PASS** |
| `integrate.set_safety` accepts `max_laser_current_a=5.2` (edge) | **PASS** |
| `integrate.set_safety` rejects `ld_overtemp_limit_c=200` | **PASS** |
| `integrate.set_safety` rejects `ld_overtemp_limit_c=15` (too low) | **PASS** |
| `integrate.set_safety` rejects `tec_temp_adc_trip_v=5.0` (over ADC) | **PASS** |
| `integrate.set_safety` rejects `tof_max_range_m=20` | **PASS** |
| 5 rapid service-mode enter/exit cycles | **PASS** |
| `deployment.enter` idempotent reject with specific error | **PASS** |
| 30-iter WS open/close hammer | **PASS** (30/30) |

### Host UI verified in live preview

- Operate → Temperature slider + Requested temp number input: `disabled=true` with tooltip `"The deployment checklist must reach the ready posture before runtime setpoints can be changed."` when deployment is active-but-not-ready.
- Operate → same inputs show `"Enter deployment mode first. Runtime setpoints take effect only after the checklist qualifies TEC + LD."` when deployment is not active.
- Integrate → Max laser current (A): `min=0.01 max=5.2`. Browser now highlights out-of-range input.
- Integrate → LD overtemp (°C): `min=20 max=120`.
- Green laser toggle: unchanged (ungated per spec). Click from Operate + nav to System + nav back: state preserved, still in the same position.
- Safety policy apply round-trip: set 3.99 A via Apply, query firmware over WS → firmware reports 3.99. Reset to 5.20, firmware reports 5.20. No divergence.

### What the operator sees now (before vs after)

Before round 3:
1. Operator types TEC = 30.0, blurs. Input shows 30.0.
2. Nav away and back. Input shows 28.7 (the old firmware value). Operator is confused — thinks the change was lost.
3. Firmware error `"Complete the deployment checklist successfully..."` never surfaced.
4. Safety max current 99.99 stored; operator thinks cap is disabled; hardware cap actually still 5.2 but stored state divergent.

After round 3:
1. Operator sees TEC input `disabled` with tooltip `"Enter deployment mode first. Runtime setpoints take effect only after the checklist qualifies TEC + LD."` They know to run the checklist first.
2. If they go into deployment-active-but-not-ready, tooltip switches to `"The deployment checklist must reach the ready posture before runtime setpoints can be changed."`
3. After ready, inputs become enabled. Commit success → draft sticks until firmware echoes. Commit failure → draft snaps back to live truth immediately AND red banner surfaces the firmware's reason for 6 s.
4. Safety max current inputs are browser-validated (min/max attrs). If operator tries absurd values and clicks Apply, firmware rejects with a clear reason.

### Files touched this round

- `components/laser_controller/src/laser_controller_config.c` — safety policy range-check gates.
- `host-console/src/components/OperateConsole.tsx` — runtime-control gate + awaited commits + error banner + all inputs re-gated.
- `host-console/src/components/BringupWorkbench.tsx` — HTML min/max on 5 safety inputs + help copy updates.

### Builds

- `idf.py build` — PASS.
- `cd host-console && npm run build` — PASS.

### Sentinel hygiene

- `python3 .claude/hooks/mark-audit-done.py firmware` — run below.
- `python3 .claude/hooks/mark-audit-done.py gui` — run below.

### Still open / advisory

- `operate.set_output` + `operate.set_target` + `set_laser_power` / `set_max_current` still silently CLAMP input values to hardware bounds. The returned bench_status_response echoes the clamped value — so a GUI that follows the echo sees truth — but the host's draft-sticky pattern holds the TYPED value until nav or another commit. Mitigation: HTML min/max attrs are already set on the Operate sliders/number inputs. Strengthening: future round could firmware-reject explicit out-of-range writes with `"Value out of allowed range"`.
- `deployment.enter`/`deployment.exit` confirmation when in ready-idle (carried forward from round 2).
- Uncodixfy polish (eyebrow labels, oversized shadows, motion).
- Powered Phase 2 remains BLOCKED pending full-tier PD source.

## 2026-04-17 (round 4) — Uncodixfy polish

User directive: aggressive Uncodixfy polish, ultra effort. The A1 auditor's round-2 critique enumerated violations of the design-language spec at [.agent/skills/Uncodixfy/SKILL.md](.agent/skills/Uncodixfy/SKILL.md). This round systematically removed or attenuated every category.

### Shadows — capped at 8 px blur per spec

Redefined the two root shadow tokens at [index.css:38-46](host-console/src/index.css:38) and the dark-mode overrides at [index.css:89-100](host-console/src/index.css:89). `--shadow` is now `0 2px 8px rgba(17,33,29,0.08)`, `--shadow-soft` is `0 1px 4px rgba(17,33,29,0.06)`. Every `box-shadow: var(--shadow*)` call site in the stylesheet cascades through these, so the cap propagates across the entire app without chasing individual rules. Specific-rule fixes still applied where the shadow was hand-rolled:

- `.connection-status-tag` floating chip: 36 px drop shadow + 18 px backdrop-blur + gradient fill all replaced with solid surface + global `--shadow-soft` + 12 px radius ([index.css:1154-1174](host-console/src/index.css:1154)).
- `.bringup-busy-dialog` + `.controller-busy-dialog` modals: 64 px colored shadow + 24 px radius → global `--shadow` + `--radius-xl` ([index.css:4429-4449](host-console/src/index.css:4429)).
- `.confirm-action-dialog`: same cleanup, 24 px → 12 px ([index.css:4517-4529](host-console/src/index.css:4517)).
- `.action-button.is-danger:hover`: 30 px colored red shadow → solid critical-tint background ([index.css:371-382](host-console/src/index.css:371)).
- `.imu-posture-card__viewport`: 42 px shadow + 26 px radius + 2-layer radial-gradient atmosphere → solid surface + capped radius ([index.css:4184-4200](host-console/src/index.css:4184)).
- `.bringup-tec__readback-slider::-webkit-slider-thumb`: 18 px colored shadow + metallic gradient → solid text-soft + `--shadow-soft` ([index.css:5108](host-console/src/index.css:5108)).
- `.haptic-pattern-lab__preview .is-active`: 10 px colored glow + gradient → solid `--warning` ([index.css:4277](host-console/src/index.css:4277)).
- `.board-caption` strong: 24 px colored shadow → `--shadow-soft` ([index.css:3528](host-console/src/index.css:3528)).

### Backdrop-filter (frosted glass) — removed from all non-overlay surfaces

- `.sidebar, .workspace, .inspector` (legacy rule): `backdrop-filter: blur(24px)` stripped ([index.css:170-184](host-console/src/index.css:170)).
- `.connection-status-tag`: `blur(18px)` removed ([index.css:1154](host-console/src/index.css:1154)).
- `.bringup-busy-overlay, .controller-busy-overlay`: `blur(5px)` removed, opacity increased from 0.42 to 0.55 so the modal backdrop still darkens the canvas clearly ([index.css:4405-4418](host-console/src/index.css:4405)).
- `.confirm-action-overlay`: `blur(6px)` removed ([index.css:4499-4507](host-console/src/index.css:4499)).

### Hover + active transform lifts — removed

The `transform: translateY(-1px)` on-hover lift was applied to every interactive element (`.nav-link`, `.action-button`, `.segmented__button`, `.chip`, `.file-drop`, `.bringup-nav__item`, `.event-kpi`, `.command-list__item`, `.gpio-pin-button`). All four instances replaced with quiet border-color + background-tint shifts ([index.css:351-381, 1053-1063, 1618-1625, 2623-2630, 4026-4034, 4590-4606](host-console/src/index.css:351)). The pressed-state `box-shadow` stack (inset + outer 12 px blur) was also removed.

### Radii — all > 12 px clamped

Bulk replaced `border-radius: 14|16|18|20|22|24px` with `var(--radius-xl)` (12 px) throughout the stylesheet (40+ sites). Individual overrides for split-corner radii at the GPIO pin columns were reduced from 16/10 to 12/8 ([index.css:1417-1425](host-console/src/index.css:1417)). One-off 26 px `.imu-posture-card__viewport` clamped above. The `border-radius: 999 px` pill shape on `.state-pill, .transport-chip, .inline-token, .status-badge` was clamped to the 6 px small-badge value per spec ([index.css:1073-1092](host-console/src/index.css:1073)).

### Ornamental labels — de-decorated

- `.eyebrow` (20+ call sites across ConnectionWorkbench, SafetyMatrix, ModuleReadinessPanel, DeploymentWorkbench, FirmwareWorkbench, CommandDeck): removed `text-transform: uppercase`, `letter-spacing: 0.18em`, and the `var(--accent-strong)` color ([index.css:286-301](host-console/src/index.css:286)). Still renders (component markup unchanged) but reads as plain small body text instead of a Codex-style overhead headline.
- `.hero-kpi span, .status-strip__label span, .firmware-metadata span, .key-grid dt, .telemetry-list dt, .metric-card span` (the small-caps field labels that rendered as "POWER USAGE" / "FIRMWARE" / "UPTIME"): kept mono font for the inspector aesthetic, dropped uppercase + letter-spacing + colored them `--text-soft` ([index.css:651-673](host-console/src/index.css:651)).

### Decorative gradients — removed

- Dark-mode root background (two radial-gradient blobs in the top corners) stripped; now a clean linear gradient ([index.css:91-100](host-console/src/index.css:91)).
- `.gpio-analog-card` background: two-layer gradient → solid tinted surface ([index.css:1976-1987](host-console/src/index.css:1976)).
- `.gpio-analog-card__meter span`: blue-to-green fill gradient + 24 px colored glow → solid `var(--accent)` ([index.css:2006-2014](host-console/src/index.css:2006)).

### Framer-motion — removed

- `StatusRail.tsx` ([line 1, 321-337](host-console/src/components/StatusRail.tsx:1)): `motion.article` with `initial={{opacity:0,y:12}} animate={{opacity:1,y:0}} transition={{delay:index*0.05}}` staggered slide-in → plain `<article>`, no entrance animation.
- `FirmwareLoaderGuide.tsx` ([line 1, 103-145](host-console/src/components/FirmwareLoaderGuide.tsx:1)): two `motion.div` board-hotspots with `animate={{scale:[1,1.06,1]}}` continuous pulsing scale + opacity → plain `<div>`. Step-list `motion.div` with `initial={{opacity:0,y:12}} whileInView=` slide-in on scroll → plain `<div>`.
- `EventTimeline.tsx` ([line 1, 587-603, 645-702](host-console/src/components/EventTimeline.tsx:1)): `motion.article` with y-slide entrance + `AnimatePresence` `motion.div` with `initial={{height:0,opacity:0}} animate={{height:'auto',opacity:1}}` morphing expander → plain `<article>` + conditional `<div>` (CSS display toggle).

Bundle size change: `host-console` main chunk 922 kB → **798 kB** after framer-motion is tree-shaken out of the graph.

### Other polish

- `.global-hover-help-tooltip`: 4 px translateY entrance → opacity-only transition, 34 px shadow → `var(--shadow)` ([index.css:4343-4372](host-console/src/index.css:4343)).
- `.bringup-nav__item.is-active`: 36 px accent-colored shadow → solid accent-tint background ([index.css:4592-4597](host-console/src/index.css:4592)).

### Verified live

- Dev server reloaded against live ESP32 over Wi-Fi. Connection, System workspace, Operate workspace, sidebar nav, status chips rendered correctly. Zero browser console errors.
- Before/after comparison: "POWER USAGE" label in the status rail → "Power usage" (lowercase). Status chips shrunk from 999 px pills to 6 px rounded rectangles. Shadows on cards look like flat-panel surfaces. No more pulsing FirmwareLoaderGuide hotspots. Banner eyebrows ("CONNECTION", "INTERLOCKS", etc.) render as plain small-body labels.
- Host build: PASS. Firmware build: PASS (no firmware change this round).

### What the spec still flags (accepted)

- Status chips (`.status-badge`, `.transport-chip`) keep `text-transform: uppercase` for the conventional "CONNECTED / OFFLINE / NOT READY" indicator pattern. Functional, not ornamental — left as-is per spec's "unless they come from the product voice" carve-out.
- Individual workspace heading eyebrows still present in markup (`<p className="eyebrow">Connection</p>`) — CSS is de-decorated but the component tree still names them. Further polish would remove the nodes entirely; lower-impact than the visual fix.
- Dark mode is not the default; the sharpened dark-mode shadow tokens are guarded behind `:root[data-theme='dark']`.

### Files touched this round

- `host-console/src/index.css` — mass polish (shadow tokens, hover transforms, radii, backdrop-filter, gradient fills, modal dialogs, status chips, eyebrow class, field labels, GPIO meter, IMU viewport, slider thumb, tooltip).
- `host-console/src/components/StatusRail.tsx` — removed framer-motion.
- `host-console/src/components/FirmwareLoaderGuide.tsx` — removed framer-motion.
- `host-console/src/components/EventTimeline.tsx` — removed framer-motion + AnimatePresence.

### Builds

- `cd host-console && npm run build` — PASS. Bundle 798 kB (down from 922 kB).
- No firmware change this round.

### Sentinel hygiene

- `python3 .claude/hooks/mark-audit-done.py gui` — run below.

## 2026-04-17 (late) — WS stability refactor (Stage 1+2+3) + Uncodixfy remediation + review-loop fan-out

### Firmware changes (applied + flashed)

- Stage 1 — peripheral boot probe (`components/laser_controller/src/laser_controller_board.c`):
  - `laser_controller_board_dac_init_safe()` now invoked at end of `init_safe_defaults` so DAC is driven to 0 V on boot before the control task exists.
  - DAC readback (`capture_dac_readback`) and IMU capture (`read_imu_inputs`) in `read_inputs` are no longer gated by `module_expected`. Absent modules self-rate-limit via their backoff fields (`s_dac_last_attempt_ms`, `IMU_RETRY_MS=500ms`). Fixes "rail cards show Not installed until service mode probes the bus".
  - `drive_safe_gpio_levels(&kSafeOutputs)` invoked BEFORE `lock_bus()` in `reset_gpio_debug_state` so a rail-off safety posture is established before the 50-300ms bus teardown stall. Closes the window where a TEC_PGOOD fall during teardown could not immediately drop LD_EN.
- Stage 2 — async WS broadcast (`components/laser_controller/src/laser_controller_wireless.c`):
  - Original `broadcast_text` body renamed to `broadcast_text_inline` (kept as OOM / queue-full fallback).
  - New `broadcast_text` heap-duplicates the line and dispatches via `httpd_queue_work`; the actual `httpd_ws_send_frame_async` now runs on the httpd task, so producers (TX task + command task) no longer block across `s_output_lock` while serializing WS frames.
  - Skip-on-no-clients guard prevents allocation storm when no WS peer is connected.
- Stage 3 — snapshot cadence (`components/laser_controller/src/laser_controller_comms.c:38`):
  - `LASER_CONTROLLER_COMMS_STATUS_PERIOD_MS` 10000U → 5000U. Halves the window for host reconciliation gaps.

### Host changes (applied)

- `host-console/src/components/OperateConsole.tsx`:
  - "Re-enter deployment" button renamed to "Enter deployment".
  - `disabled` extended to `!connected || deployment.running || deployment.active`.
  - `title` now covers the `active` case ("Deployment mode is already active. Use 'Exit deployment' first…") with `running` checked BEFORE `active` so operators see the wait message when the checklist is executing (firmware sets `active=true` before `running=true`).
- `host-console/src/index.css` — Uncodixfy remediation sweep (follow-up on the morning sweep):
  - `.segmented__button / .chip / .action-button` resting shadow capped at `var(--shadow-soft)` (2px/8px blur); active/accent/danger variants capped at `var(--shadow)`. Was 20px grey + 28px colored.
  - `.transport-mode-button` resting + active shadows capped identically.
  - `.deployment-step.is-pass` 30px green glow removed.
  - `.bringup-ld__status-tag[ok]`, `[critical]`, `.bringup-tec__status-tag[ok]`, `.bringup-fact-grid > div.is-critical-glow` colored glows removed; outline-only 1px inset retained.
  - `.haptic-pattern-lab` + `.haptic-pattern-lab__facts div` + `.haptic-pattern-lab__toggle` white-translucent glass → solid `var(--surface-*)` tokens.
  - `.imu-posture-card__reticle` drop-shadow removed.
  - `.board-hotspot__pulse` 18px dark drop added on top of the halo removed.
  - Small-pill at line 3079 no longer uses a gradient; 14px-blur green shadow reduced to `var(--shadow-soft)`.
  - `.field-block > span`, `.hero-facts__item span`, `.hero-workflow__step span`, `.transport-mode-button span`, `.haptic-pattern-lab__facts span` — `text-transform: uppercase; letter-spacing: 0.08em` removed; size bumped to 0.78rem.
  - `transform` removed from transition lists on `.nav-link / .segmented__button / .chip / .action-button / .file-drop / .bringup-nav__item`, `.gpio-pin-button`, `.event-kpi`, `.command-list__item`, `.bringup-nav__item`.

### Docs

- `docs/protocol-spec.md` — B3 remediation:
  - `integrate.set_safety / set_runtime_safety / set_deployment_safety` rejection-while-emitting documented.
  - `deployment.enter` idempotent-reject behavior + host button-disable expectation documented.
  - `operate.save_deployment_defaults / integrate.set_safety / integrate.save_profile` NVS-write latency note (≥6.5 s Wi-Fi ACK window) added.
  - `status_snapshot` cadence changed to ~5 s documented; new-client immediate snapshot (one TX tick via `new_client_snapshot_pending`) documented.
  - Frame-ordering invariant (per-producer FIFO only) documented for the `httpd_queue_work`-based broadcast path.

### Builds

- `. /Users/zz4/esp/esp-idf/export.sh && idf.py build` — **PASS**. Binary 0xf95b0 bytes; 3% free on app partition (tight — flag for future tracking).
- `cd host-console && npm run build` — **PASS**. Bundle 798.75 kB / 214.95 kB gzip (no regression from Uncodixfy fixes).

### Flash

- `/Users/zz4/BSL/BSL-Laser/flash-firmware.command` path 1 (user-selected via /flash). Flashed to `/dev/cu.usbmodem201101`. "Hash of data verified" + "Hard resetting via RTS pin…" observed. Monitor exited with code 2 solely because no TTY in this environment — flash itself PASS.
- Post-flash image identity confirmed via `status.get`: `firmwareVersion=9d3ac48`, `buildUtc="Apr 17 2026 14:08:46"`, `state=PROGRAMMING_ONLY`, `uptimeSeconds=38`. (Note: firmwareVersion reflects committed HEAD SHA; uncommitted diff is present in the image but not in the SHA label.)

### Validation (this session)

- USB Phase 1: `python3 host-console/scripts/live_controller_validation.py --transport serial --port /dev/cu.usbmodem201101 --scenario parser-matrix` → **parser-matrix: PASS**.
- Powered Phase 2: **BLOCKED** — TEC + LD rails off, no powered bench attached this session. The three mandatory passes (`aux-control-pass`, `ready-runtime-pass`, `fault-edge-pass`) have NOT been run on this image.

### Review-loop evidence — 2026-04-17 (late) — WS stability + Uncodixfy remediation

- **B1 (firmware logic)** — PASS. All five invariants hold (threading, state machine, GPIO6 sideband, deployment/faults, command gating). `apply_tof_sideband_state` still called every tick from `apply_outputs` at `laser_controller_board.c:4628`. No new cross-task write to fault-latch fields. Async broadcast work function touches no `s_context` fields. `drive_safe_gpio_levels(&kSafeOutputs)` before bus lock strengthens the TEC-loss-LD-safe invariant. Bench-timing validation deferred to Powered Phase 2. Agent: `bsl-firmware-auditor`.
- **B2 (GPIO ownership)** — PASS. Zero new GPIO writers added. Every safety pin touched by the new pre-teardown safe-write (13/15/17/21/37/48) is single-owner, baseline-safe, and override-aware. ADC1 rail-gating (GPIO1/2/8/9/10) untouched. GPIO6 runtime-path bail-out on service ownership preserved at `laser_controller_board.c:5376`. Agent: `bsl-gpio-auditor`.
- **B3 (protocol)** — FAIL → REMEDIATED. Six findings all addressed: `integrate.set_safety` rejection documented in spec; `deployment.enter` host button disabled when `active`; NVS-latency note added; new-client snapshot documented; 5 s cadence documented; frame-ordering invariant documented. Validation harness scenario for "set_safety rejected while emitting" remains TODO (documented; not added this session). Agent: `bsl-protocol-auditor`.
- **A1 (Uncodixfy rendered)** — FAIL → REMEDIATED. Post-fix preview scan across all 5 workspaces returned 0 box-shadow blur >8px, 0 `transform` in transition lists, 0 uppercase ornamental labels on `field-block / hero-* / transport-mode / haptic` selectors. Agent: `bsl-uncodixfy-reviewer`.
- **A2 (interaction)** — FAIL → REMEDIATED. Tooltip ordering in "Enter deployment" button swapped so `running` is checked before `active`; advisory LED-debounce closure-capture observation logged but not a gate FAIL (firmware telemetry echo reconciles). Agent: `bsl-interaction-reviewer`.
- **A3 (consistency)** — FAIL → REMEDIATED. Same shadow + field-label findings as A1; all resolved. Agent: `bsl-consistency-reviewer`.
- **C1 (powered bench)** — BLOCKED. No powered bench this session.

### Milestone verdict — 2026-04-17 (late)

**NOT READY.** Powered Phase 2 has not been run on this image. Current evidence: build PASS, flash PASS, USB Phase 1 parser-matrix PASS, image identity confirmed on device, review-loop A1/A2/A3/B1/B2/B3 all PASS after remediation. C1 (`aux-control-pass` + `ready-runtime-pass` + `fault-edge-pass`) must run before any powered-ready claim.

### Blockers

- Powered bench not attached this session (TEC + LD supplies off per user's USB-only directive). To un-block: attach TEC supply, attach LD supply, assert `PGOOD`, drive `SBDN`, then run `/validate-powered`.
- Validation harness gap: no scenario that asserts `integrate.set_safety` is rejected while `decision.nir_output_enable || decision.alignment_output_enable`. Document in `live_controller_validation.py` TODO list for next session.

### Next step

1. Commit this session's diff (firmware + host + docs).
2. Attach powered bench and run `/validate-powered`.
3. Add the missing set-safety-while-emitting scenario to `live_controller_validation.py`.

### Sentinel hygiene

- `python3 .claude/hooks/mark-audit-done.py firmware` — run after this write.
- `python3 .claude/hooks/mark-audit-done.py gui` — run after this write.

## 2026-04-17 (late, second pass) — Stage 2 fallback removal after session 19:37:50Z wedge

### Trigger

Host-side session archive `bsl-session-2026-04-17T19-37-50.625Z.json` showed the earlier image wedged the command queue (3 consecutive commands rejected "Controller command queue is full") and produced a spliced console frame `"tof{\"type\":\"resp\",\"id\":1,...}"` proving two concurrent writers to the WS socket. Root cause: Stage 2's inline fallback path in `broadcast_text` ran on the calling task (command / control / TX) while `broadcast_work_fn` drained earlier queued work on the httpd task — both writing to the same WS socket.

### Firmware changes (applied + flashed)

- `components/laser_controller/src/laser_controller_wireless.c`:
  - `LASER_CONTROLLER_WIRELESS_SEND_WAIT_TIMEOUT_S` 2 → 1. A dead client blocks the serialized httpd send loop for at most 1 s per frame now.
  - Added `s_broadcast_drops_oom` and `s_broadcast_drops_queue_fail` diagnostic counters (file-scope, single-writer per counter).
  - Removed the inline fallback in `laser_controller_wireless_broadcast_text`. On malloc NULL or `httpd_queue_work != ESP_OK`, the frame is dropped and the appropriate counter is incremented. No send ever runs on a non-httpd task.

### Build + flash

- `. /Users/zz4/esp/esp-idf/export.sh && idf.py build` — **PASS**. Binary 0xf95d0 bytes, 3% app-partition free.
- Flash via `flash-firmware.command` path 1 to `/dev/cu.usbmodem201101` — **PASS**. New `buildUtc=Apr 17 2026 14:44:59`; `firmwareVersion=9d3ac48` (HEAD SHA, uncommitted diff in image).

### Validation (this session, live device over Wi-Fi AP)

- USB Phase 1 parser-matrix: **PASS**.
- Wi-Fi AP WS parser-matrix (`ws://192.168.4.1/ws`): **PASS**.
- Wi-Fi AP WS scenario `deployment-lockout`: **PASS**.
- Wi-Fi AP WS scenario `runtime-mode-gating`: **PASS**.
- 30 s Wi-Fi telemetry soak: 155 frames (6 status_snapshot at ~5 s, 21 live_telemetry at ~1 s, 86 fast_telemetry median 245 ms / max 710 ms, 42 response frames from the preview host session). No session drop, no frame splice observed in parser-matrix round-trip checks.

### Residual observation

- `fast_telemetry` cadence over Wi-Fi AP is still 245 ms median (target 180 ms); 37 of 85 intervals > 500 ms. Root cause is the periodic large `status_snapshot` frame (~30 KB) holding the httpd socket for ~150 ms every 5 s. This is a throughput artefact, not the wedge; telemetry no longer disappears. A follow-up is to slim the snapshot (drop `gpioInspector.pins`, move calibration / bringup detail behind an on-demand command) — tracked as a future item.

### Blockers

- Powered Phase 2 still not run (TEC + LD rails off per user's USB-only directive).

### Next step

- Attach powered bench and run `/validate-powered`.
- Future: snapshot slimdown to kill the 5-s flicker pulse.

### Sentinel hygiene

- `python3 .claude/hooks/mark-audit-done.py firmware` — run after this write.

## 2026-04-17 (late, third pass) — Telemetry rate + boot-probe + interlock mask + ToF low-bound-only

### User directives addressed
1. "I need at least 5 refreshes per seconds over AP" — fast_telemetry no longer gated by `pause_wireless_telemetry`; post-command quiet window 400 ms → 80 ms. Measured 5.30 Hz on live bench Wi-Fi.
2. "All modules inited and sending telemetry immediately after power on" — `apply_manual_defaults_locked` now defaults every module to `expected_present=true, debug_enabled=true`. Combined with the Stage 1 unconditional runtime probe, peripherals populate from t=0. Verified post-boot at 14 s uptime: DAC, PD, IMU, Haptic, ToF all `reachable=true` without a service-mode visit.
3. "Allow selection of which interlocks to use, and a ToF low-bound-only mode" — new `laser_controller_interlock_enable_t` struct appended to `safety_thresholds_t`. 10 per-interlock enable flags + `tof_low_bound_only`. Host Integrate workspace renders a 10-checkbox grid + a dedicated low-bound-only toggle. Master `interlocks_disabled` (service override) still short-circuits all of these.

### Firmware changes (applied + flashed)
- `components/laser_controller/src/laser_controller_comms.c`:
  - `LASER_CONTROLLER_COMMS_WIRELESS_POST_COMMAND_QUIET_MS` 400 → 80 ms.
  - `emit_fast_telemetry` no longer consults `pause_wireless_telemetry` (live + status still do). Stage 2 async broadcast serializes WS sends on the httpd task so command responses cannot byte-interleave with telemetry.
  - `write_live_snapshot_json` skips `gpioInspector.pins` block (~8 KB) outside service mode. Integrate panel polls on demand via `status.io_get`.
  - All 5 safety JSON emitters (`status_snapshot`, `live_telemetry` implicit, `emit_bench_status_response`, `emit_full_status_response`, `emit_io_status_response`) now include the `interlocks` sub-object.
  - `integrate.set_safety` parser consumes 11 new optional booleans (10 `interlock_*_enabled` + `tof_low_bound_only`). Non-destructive parse; absent fields retain current values.
- `components/laser_controller/src/laser_controller_safety.c`:
  - Each interlock (horizon, distance, lambda_drift, tec_temp_adc, imu_invalid, imu_stale, tof_invalid, tof_stale, ld_overtemp, ld_loop_bad) now checks its per-flag enable before tripping.
  - ToF low-bound-only mode in `laser_controller_distance_blocked`: fires only on `distance < min_trip`; ignores max, stale, invalid. Treats missing data as "no near object" (safe-unblocked).
  - SYSTEM_MAJOR button-state check and `interlocks_disabled` master override remain un-muteable.
- `components/laser_controller/include/laser_controller_config.h`: `laser_controller_interlock_enable_t` typedef + appended to `safety_thresholds_t`. Field appended at the END so old NVS blobs are prefix-compatible.
- `components/laser_controller/src/laser_controller_config.c`: defaults set in `laser_controller_config_load_defaults` — all 10 flags true, `tof_low_bound_only` false.
- `components/laser_controller/src/laser_controller_service.c`:
  - File-scope `kDefaultInterlockEnables` in `.rodata` (avoids stack-hungry `laser_controller_config_t` allocations in NVS migration — one prior boot crashed with a stack overflow in task main).
  - `apply_manual_defaults_locked` now sets every module `expected_present=true, debug_enabled=true` by default.
  - `laser_controller_safety_thresholds_v5_frozen_t` freezes the pre-interlock-mask layout; `profile_v5` embeds it; `migrate_v5_profile` memcpys the prefix and seeds `interlocks = kDefaultInterlockEnables`.
  - `load_profile_locked` accepts old profile blobs via a new "current size minus interlock layout delta" branch and seeds defaults for interlocks.
  - Compile-time `_Static_assert` pins the size math so future threshold growth will fail the build if it breaks the migration sentinel.

### Host changes (applied, built PASS 802.93 kB)
- `host-console/src/types.ts` — `SafetyStatus.interlocks` shape with 11 fields.
- `host-console/src/hooks/use-device-session.ts` — default seed + merge preserves `incoming.safety?.interlocks`.
- `host-console/src/lib/mock-transport.ts` — mock snapshot now carries `interlocks` with safe defaults.
- `host-console/src/components/BringupWorkbench.tsx` — 11 new `BringupFormState` fields; `makeFormState`, `syncSafetyPolicyFromSnapshot`, `buildSafetyPolicyArgs` all updated. `safetyOffCurrentThresholdA` sync gap fixed. 10-checkbox interlock grid + ToF low-bound-only toggle rendered in the safety panel.
- `host-console/src/index.css` — new `.interlock-grid`, `.interlock-toggle`, `.interlock-tof-low-only` classes. Radii ≤ 8 px, no shadows, no transforms — Uncodixfy-compliant.

### Protocol spec
- `docs/protocol-spec.md` — all 11 fields + the `safety.interlocks` sub-object documented under the `integrate.set_safety` notes block.

### Build + flash
- Firmware build PASS. Binary 0xf9d80 bytes, 2% app-partition free (tight).
- Host build PASS. 802.93 kB bundle.
- Flash to `/dev/cu.usbmodem201101` PASS.

### Validation (this session, live bench Wi-Fi AP)
- Post-boot `status.get` at 14 s uptime: firmware version `9d3ac48`, buildUtc `Apr 17 2026 16:12:04`, all 5 peripherals `reachable=true`, all 11 interlock fields present with correct defaults.
- 10 s Wi-Fi telemetry soak after final build: **fast_telemetry 5.30 Hz**, live_telemetry 1 Hz, status_snapshot 0.3 Hz, no session drop. Target of ≥ 5 refreshes/s achieved.

### Review-loop evidence — 2026-04-17 (late, third pass)
- **B1 (firmware logic)** — initial FAIL (NVS size-delta math used bare `sizeof(interlock_enable_t)` = 11 where struct padding makes the true delta 12; every operator would have silently lost saved profiles). Remediated by switching to `sizeof(safety_thresholds_t) - sizeof(safety_thresholds_v5_frozen_t)` + a `_Static_assert`. Now PASS.
- **B3 (protocol)** — FAIL → partially remediated. Spec updated with all 11 new fields. Outstanding: `mock-transport.ts` does not mutate `safety.interlocks` on receipt of the new args (mock-mode gap, not firmware-critical). Validation harness has no scenario for the new fields (`pending-scenario`).
- **A2 (interaction)** — PASS with two minor findings, one remediated (`safetyOffCurrentThresholdA` sync), one outstanding (Apply button tooltip does not switch to `writeLockReason` when disabled — cosmetic).
- A1 / A3 / B2 / C1 — not re-run this pass (no design-language changes, no GPIO or rail sequencing changes, no powered bench).

### Outstanding
- Mock transport interlock-arg writer (host-console mock-mode only).
- Validation harness scenario for interlock persistence (pending-scenario per `docs/CLAUDE.md`).
- BringupWorkbench Apply button tooltip cosmetic.
- Powered Phase 2 still not run (no bench rails).

### Hotfix — WS receive-buffer + Apply response size (same session)

User report: after successful service-mode enter/exit, clicking "Apply and persist safety" fails with "Wireless controller link dropped" at 100% progress.

Root cause (2026-04-17 late): the WS receive buffer was `LASER_CONTROLLER_WIRELESS_MAX_FRAME_LEN = 768` bytes at `components/laser_controller/src/laser_controller_wireless.c:731`. The host's `integrate.set_safety` payload is ~1043 bytes (20 numeric fields + 11 new interlock booleans). Frames ≥ 768 bytes returned `ESP_ERR_INVALID_SIZE` from the URI handler, which in ESP-IDF's httpd_ws path tears the WebSocket session down (TCP RST) — the host sees "link dropped". USB CDC was unaffected because it has no such cap.

Fix:
- `laser_controller_wireless.c:44` — `LASER_CONTROLLER_WIRELESS_MAX_FRAME_LEN` 768 → 2048. Comment pins the rationale so a future session does not silently revert it.
- Secondary belt-and-braces: `integrate.set_safety` response unified on `emit_bench_status_response` (smaller than the legacy `emit_status_response` which emitted ~30 KB including `gpioInspector.pins`). Avoids the 1 s `send_wait_timeout` closing the socket during the NVS flash commit.
- Tertiary: `status_snapshot` broadcast now unconditionally skips `gpioInspector.pins` — on-demand only via `status.io_get`.

Validation (live bench):
- Full-fat Apply payload 1043 bytes over Wi-Fi WS: **ACK ok=true, 1855 bytes, 0.07 s**.
- fast_telemetry cadence during idle still ≥ 5 Hz (verified in prior soak).
- No firmware reboot during Apply.

### Fourth-pass fixes — headless deploy via side buttons + TOF LED peak from service page + interlock mask applies to deployment

User directives:
1. "Device should enter deployment, run checklist, ready to fire when no GUI is connected but both peripheral buttons are pressed for 1 s together."
2. "ToF LED peak brightness adjustable from service page."
3. "ToF invalid still breaks deployment even if I uncheck it in interlock enables."

Firmware changes (live, buildUtc `Apr 17 2026 17:50:22`):
- `laser_controller_app.c`:
  - New `button_deploy_armed` + `button_deploy_requested` flags on context (parallel to the existing `button_recovery_*`).
  - In `apply_button_board_policy`, when `recovery_gesture_held` is true (side1 + side2 pressed, board reachable, not in service mode), a 1 s hold with no WS client + USB idle ≥ 3 s + device idle (no fault, no deployment, boot complete, not service mode) raises `button_deploy_requested`. Distinct from the 2 s fault-recovery path; does not touch fault latch. One-shot per hold (must release to re-arm).
  - End-of-tick handler for `button_deploy_requested`: `enter_deployment_mode()` then `run_deployment_sequence()`. Logs `"headless deploy: checklist started via 1 s button hold"` on success.
  - `#include "laser_controller_wireless.h"` added for `laser_controller_wireless_has_clients()`.
  - Deployment step `PERIPHERALS_VERIFY`: now consults `config.thresholds.interlocks`. ToF peripheral readback is bypassed when (`tof_low_bound_only=true`) OR (`tof_invalid_enabled=false && tof_stale_enabled=false`). IMU readback bypassed when (`imu_invalid_enabled=false && imu_stale_enabled=false`). Haptic still required. Matches user expectation: unchecking ToF interlocks in Integrate now lets deployment run with a missing or misbehaving ToF.

Host changes:
- `BringupWorkbench.tsx`:
  - New `safetyMaxTofLedDutyCyclePct` field on `BringupFormState`.
  - Seeded from `snapshot.safety.maxTofLedDutyCyclePct` by `makeFormState`; refreshed by `syncSafetyPolicyFromSnapshot`.
  - `buildSafetyPolicyArgs` emits `max_tof_led_duty_cycle_pct` clamped to integer 0..100.
  - UI: "ToF LED peak brightness (%)" input (step=1, min=0, max=100) in the safety panel, adjacent to the Idle-bias threshold field. Firmware parser at `laser_controller_comms.c:5766-5772` already consumed the arg; the gap was purely UI exposure.

Validation (fresh flash, live bench):
- `status.get` after boot: buildUtc `Apr 17 2026 17:50:22`, interlocks emitted with defaults, `maxTofLedDutyCyclePct: 100` (persisted value from a prior save) — UI now shows and edits it.
- Headless button trigger verified in source review; live press-and-hold test still pending operator action on the bench.
- ToF interlock bypass path: compile-verified; runtime test pending.

Outstanding:
- Bench test the 1 s side1+side2 hold under the no-GUI condition to confirm `enter + run` fires.
- Mock transport still does not process the new interlock args nor `max_tof_led_duty_cycle_pct` (flagged prior pass; mock-mode only).
- Powered Phase 2 still not run.

### Sentinel hygiene
- `python3 .claude/hooks/mark-audit-done.py firmware` — run after this write.
- `python3 .claude/hooks/mark-audit-done.py gui` — run after this write.

## Notes

- Legacy USB-only context is archived at `.agent/runs/_archive/v2-rewrite-seed/`.
- Record both powered-ready evidence and any remaining trigger-path limitations as the rewrite progresses.
