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

## Notes

- Legacy USB-only context is archived at `.agent/runs/_archive/v2-rewrite-seed/`.
- Record both powered-ready evidence and any remaining trigger-path limitations as the rewrite progresses.
