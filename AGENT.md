# AGENT.md

This repository is for a life-critical surgical laser controller. Every agent or engineer who touches it must assume that a misunderstanding can injure a patient.

## Mission

Preserve a firmware architecture where the default answer to any ambiguity is:

- NIR off
- green alignment off
- laser driver in standby
- LD VIN off unless explicitly required
- TEC VIN off unless explicitly required

## Non-Negotiable Rules

1. Never add a path that can emit on boot, reset, panic, brownout, watchdog, task crash, config corruption, stale data, or undefined state.
2. Never bypass the centralized beam-permission gate in [laser_controller_safety.c](/Users/zz4/BSL/BSL-Laser/components/laser_controller/src/laser_controller_safety.c).
3. Never hide safety decisions in UI code, comms handlers, or peripheral drivers.
4. Never assume a rail is good because its enable is asserted. Require PGOOD plus plausibility.
5. Never treat stale or invalid IMU/ToF data as “probably fine.”
6. Never add service overrides to normal firmware paths.
7. Never land hidden debug commands that can defeat interlocks.
8. Never replace explicit state transitions with implicit behavior.
9. Never use heap allocation after startup unless the change is justified, reviewed, and documented.
10. If you are unsure whether a condition should latch, bias toward latching and forcing a deliberate recovery path.
11. Do not let the browser flasher program arbitrary `.bin` files. Web flashing must reject any image that does not carry a valid embedded BSL firmware signature block.

## Current Code Ownership Map

- [laser_controller_safety.c](/Users/zz4/BSL/BSL-Laser/components/laser_controller/src/laser_controller_safety.c)
  The only authoritative beam-permission decision point.
- [laser_controller_app.c](/Users/zz4/BSL/BSL-Laser/components/laser_controller/src/laser_controller_app.c)
  Schedules the control loop, classifies power tier, latches faults, derives outputs, and drives the top-level state machine.
- [laser_controller_bench.c](/Users/zz4/BSL/BSL-Laser/components/laser_controller/src/laser_controller_bench.c)
  Owns service-gated host bench targets for laser current, TEC target, and modulation staging.
- [laser_controller_state.c](/Users/zz4/BSL/BSL-Laser/components/laser_controller/src/laser_controller_state.c)
  Explicit state transition guard table.
- [laser_controller_config.c](/Users/zz4/BSL/BSL-Laser/components/laser_controller/src/laser_controller_config.c)
  Conservative defaults and config validation.
- [laser_controller_board.c](/Users/zz4/BSL/BSL-Laser/components/laser_controller/src/laser_controller_board.c)
  Bench-facing HAL with real GPIO, ADC, shared I2C, IMU SPI, DAC, PD polling, and service-mode bus recovery hooks. It is no longer a pure mock layer, but it is still not clinical-ready.
- [laser_controller_service.c](/Users/zz4/BSL/BSL-Laser/components/laser_controller/src/laser_controller_service.c)
  Owns protected bring-up module expectations, tuning state, and bus-tool scaffolding.
- [hardware-recon.md](/Users/zz4/BSL/BSL-Laser/docs/hardware-recon.md)
  Recovered MCU pin map, bus topology, connector notes, and hardware risks from the local schematics and datasheets.
- [firmware-pinmap.md](/Users/zz4/BSL/BSL-Laser/docs/firmware-pinmap.md)
  Final firmware-facing GPIO designation and cross-board connector map from the current schematic and netlist set.
- [datasheet-programming-notes.md](/Users/zz4/BSL/BSL-Laser/docs/datasheet-programming-notes.md)
  Datasheet-derived bring-up, mode-selection, and reset guidance for DAC80502, DRV2605, LSM6DSO, STUSB4500, ATLS6A214, and the TEC controller.
- [laser_controller_pinmap.h](/Users/zz4/BSL/BSL-Laser/components/laser_controller/include/laser_controller_pinmap.h)
  Compile-time GPIO assignments and board stuffing assumptions for the current hardware revision.
- [validation-plan.md](/Users/zz4/BSL/BSL-Laser/docs/validation-plan.md)
  Minimum verification bar before any beam enable on real hardware.
- [host-console/src/hooks/use-device-session.ts](/Users/zz4/BSL/BSL-Laser/host-console/src/hooks/use-device-session.ts)
  Owns host-side session state, transport lifecycle, command history, and session export.
- [host-console/src/lib/web-serial-transport.ts](/Users/zz4/BSL/BSL-Laser/host-console/src/lib/web-serial-transport.ts)
  Browser transport wrapper for newline-delimited JSON over USB CDC / Web Serial, plus raw app-binary flashing over the ESP ROM bootloader.
- [components/laser_controller/src/laser_controller_wireless.c](/Users/zz4/BSL/BSL-Laser/components/laser_controller/src/laser_controller_wireless.c)
  Bench-only Wi-Fi SoftAP + WebSocket bridge that carries the same newline-delimited JSON protocol as USB CDC so the host can stay connected while USB-C is occupied by PD power.
- [host-console/src/lib/websocket-transport.ts](/Users/zz4/BSL/BSL-Laser/host-console/src/lib/websocket-transport.ts)
  Browser wireless transport for the controller WebSocket bridge. It must preserve the same protocol semantics and service-mode gating as the USB path.
- [host-console/src/lib/controller-protocol.ts](/Users/zz4/BSL/BSL-Laser/host-console/src/lib/controller-protocol.ts)
  Shared host-side parser for the controller JSON line protocol. USB and wireless must stay on this one parser so transport changes do not fork protocol behavior.
- [host-console/src/components/CommandDeck.tsx](/Users/zz4/BSL/BSL-Laser/host-console/src/components/CommandDeck.tsx)
  Guarded maintenance command surface, split I2C/SPI bus lab, decoded register readback, and the host-side multi-PCB provisioning worksheet for Main / ToF / BMS / PD / Button boards. Do not turn this into a direct beam-control panel.
- [host-console/src/components/GpioWorkbench.tsx](/Users/zz4/BSL/BSL-Laser/host-console/src/components/GpioWorkbench.tsx)
  Physical ESP32-S3-WROOM GPIO visualization and service-only pin override tool. Live readback must stay visually separate from staged overrides.
- [host-console/src/components/ControlWorkbench.tsx](/Users/zz4/BSL/BSL-Laser/host-console/src/components/ControlWorkbench.tsx)
  Bench-only staging UI for laser, TEC, and modulation requests. Firmware must stay authoritative.
  PCN modulation on this board is frequency-and-duty only. `LISL` is hard grounded, `0 Hz` means DC/static PCN selection, and leaving modulation must restore GPIO21 to normal digital high/low ownership cleanly.
- [host-console/src/components/BringupWorkbench.tsx](/Users/zz4/BSL/BSL-Laser/host-console/src/components/BringupWorkbench.tsx)
  Module bring-up UI with a Service landing page for guarded policy edits, module planning, and tuning.
- [host-console/src/components/BusTrafficViewer.tsx](/Users/zz4/BSL/BSL-Laser/host-console/src/components/BusTrafficViewer.tsx)
  Dedicated decoded SPI/I2C traffic viewer. It combines host-out commands and device-in bus events so bench communication can be isolated by bus, module, device, and direction.
- [host-console/src/components/FirmwareWorkbench.tsx](/Users/zz4/BSL/BSL-Laser/host-console/src/components/FirmwareWorkbench.tsx)
  Host-side firmware staging, preflight, and browser-flash workflow UI.
- [components/laser_controller/include/laser_controller_signature.h](/Users/zz4/BSL/BSL-Laser/components/laser_controller/include/laser_controller_signature.h)
  Defines the embedded BSL firmware signature contract that browser flashing must verify before writing any image.
- [host-console/src/lib/event-decode.ts](/Users/zz4/BSL/BSL-Laser/host-console/src/lib/event-decode.ts)
  Host-side decode helpers for I2C and SPI register activity so event history and bus tools stay human-readable.
- [host-console/src/components/FirmwareLoaderGuide.tsx](/Users/zz4/BSL/BSL-Laser/host-console/src/components/FirmwareLoaderGuide.tsx)
  Visual firmware-loading guide with the physical `RST` and `BOOT` button map for this board and the manual ESP32-S3 download-mode sequence.

## Present Safety Posture

- Default config now includes a conservative bench LUT so the controller can boot for bring-up.
- That LUT is not a substitute for per-unit clinical calibration.
- Runtime NIR request plumbing exists. A host `laser_output_enable` request should stage `requestedNirEnabled`, and on a valid full-power runtime bench it should then drive the NIR path prerequisites (`TEC rail`, `LD rail`, `SBDN high`, `PCN high`) from the normal controller output path. Bench verification must be done on a board that is not stuck in `PROGRAMMING_ONLY` and does not currently have an active interlock block such as `tof_out_of_range`.
- Alignment cannot be enabled from the current repository.
- The firmware now boots bench-safe into `PROGRAMMING_ONLY` when the PD module is not declared present or no valid contract exists.
- Service-mode command/control over USB Serial/JTAG is working with the current host wire format.
- The board layer now has real bench drivers for:
  - safe GPIO outputs and rail-status inputs
  - ADC telemetry reads for LD / TEC analog monitors
  - shared I2C transactions for DAC80502, STUSB4500, and DRV2605
  - STUSB4500 runtime PDO readback and service-mode runtime PDO writes
  - LSM6DSO SPI register access and runtime accel sampling
  - NVS-backed bring-up profile persistence
- Shared-I2C faults now attempt bus recovery and report raw line levels (`SDA`, `SCL`) in bring-up diagnostics.
- The runtime snapshot now has a dedicated `peripherals` block that should be treated as peripheral truth. Those fields come from actual bench readback, not from the staged host draft:
  - `peripherals.dac`: `SYNC`, `CONFIG`, `GAIN`, `STATUS`, `DATA_A`, `DATA_B`, `REF_ALARM`, reachability, and last low-level error
  - `peripherals.pd`: raw STUSB4500 `CC_STATUS`, `DPM_PDO_NUMB`, `RDO_STATUS`, reachability, and attachment state
  - `peripherals.imu`: `WHO_AM_I`, `STATUS_REG`, key runtime `CTRL*` registers, reachability/configured state, and last low-level error
  - `peripherals.haptic`: DRV2605 `MODE`, `LIBRARY`, `GO`, `FEEDBACK`, actual `GPIO48` enable level, shared `IO37` trigger level, reachability, and last low-level error
- Host UI should distinguish carefully between:
  - staged/local bring-up settings
  - derived summaries or safety classifications
  - actual peripheral readback from the `peripherals` block
  Do not visually present staged values as if they were chip readback.
- `refresh_pd_status` is the read-only path for forcing an immediate STUSB4500 contract/PDO refresh from the host.
- The host console now exposes service-only bench and bring-up workflows, but those actions remain advisory commands routed to firmware, not direct hardware access.
- Service mode currently forces derived outputs safe while still allowing staged host requests and bus diagnostics.
- The Bring-up navigator now also includes a dedicated `Power supplies` page for the two MPM3530 rails. That page is for service-only rail sequencing and PGOOD validation, not for beam enable.
- Bring-up plan metadata (`set_profile_name`, `set_module_state`, `save_bringup_profile`) is intentionally allowed outside service mode so the bench build plan can be saved without opening a hardware write session.
- Read-only bring-up probes (`i2c_scan`, `i2c_read`, `spi_read`) are intentionally allowed outside service mode.
- `set_supply_enable` is service-only. In `SERVICE_MODE`, it may request the LD or TEC VIN rail on for bench bring-up, but alignment must remain off, the driver must remain in standby, and no NIR request should be created from that page.
- Host UI must show requested service rail overrides (`bringup.power.ldRequested`, `bringup.power.tecRequested`) separately from actual live rail status (`rails.ld.enabled/pgood`, `rails.tec.enabled/pgood`). Requested intent is not the same thing as hardware readback.
- ERM bring-up should treat `GPIO48` as the dedicated driver-enable path. Expose it as an explicit service-only control in the haptic page instead of assuming DRV2605 register writes alone are enough to energize the motor path.
- The haptic page should show both actual `GPIO48` level and the shared `IO37` trigger / green-laser sideband level, so the operator can distinguish “DRV2605 configured over I2C” from “ERM driver electrically enabled.”
- The Bring-up page now auto-syncs local module expectation/debug flags into firmware when connected. The operator should not have to click `Save module plan` before a normal per-module `Apply ...` action becomes predictable.
- That auto-sync must keep retrying until the controller plan actually matches the local host draft. It must not be a one-shot timer that gives up forever just because a background probe happened to be running at the wrong moment.
- Background bring-up probes should stay paused while local module-plan sync is still outstanding. Otherwise the UI can show `Awaiting probe` for a module that is physically present simply because the controller never received the expected-present flag yet.
- The Events page now contains a dedicated decoded bus-traffic viewer for all SPI/I2C communication. It is the preferred place to isolate communication for a single module or device before dropping into manual Bus Lab reads.
- The Tools Bus Lab must show the actual returned transfer string (`lastI2cOp`, `lastSpiOp`, or `lastI2cScan`) as the command result, not just a generic "controller acknowledged command" message.
- Repeated identical bus reads still need their own comms-log entries. Do not rely only on snapshot-diff events for SPI/I2C history, because the operator may intentionally read the same register value many times in a row.
- The Events workspace should also surface failed host commands directly from command history, not only from derived transport events. A rejected `dac_debug_config` or similar service write must remain visible even if the inspector rail is collapsed.
- The Tools workspace now also exposes a physical ESP32-S3-WROOM GPIO inspector. That tool must show actual controller readback for each pin (`gpioInspector`) separately from any staged override draft.
- Analog telemetry pads in the GPIO inspector, especially `GPIO2 / LD_LIO`, `GPIO1 / LD_TMO`, and `GPIO8 / TEC_TMO`, should not be presented as "unused" just because their digital GPIO mode looks idle. For those pins, the preferred truth is the live ADC-backed telemetry value from the controller snapshot.
- `GPIO13 / LD_SBDN` can be service-overridden independently of the normal firmware output path. Any exported `driverStandby` status must therefore reflect the effective live SBDN state after overrides, not just `last_outputs.assert_driver_standby`, otherwise the GUI will show contradictory standby truth even when the pin itself is stable.
- `set_gpio_override` and `clear_gpio_overrides` are service-only. They are for controlled bench proving only and must never be repurposed as a hidden runtime control path.
- The GPIO inspector must keep a one-step escape hatch: `Reset all to firmware` clears every override and hands ownership back to the original firmware logic.
- Treat transport, boot-strap, and debug pins as hazardous in the UI. `GPIO19/20`, `GPIO43/44`, `GPIO0`, `GPIO46`, `GPIO3`, and `GPIO45` should remain visibly risky because overriding them can drop the link or affect boot behavior.
- A GPIO override tool should never imply that an overridden pin is part of normal validated firmware behavior. The safe interpretation is always "temporary service ownership."
- Service-mode peripheral writes should only succeed when the module is armed for writes through the bring-up plan (`expected_present` or `debug_enabled`). They must not return fake success when the module plan still disallows hardware access.
- The host Bring-up `Apply ...` actions should sync that module's current plan to firmware before attempting the module-specific write, so a stale unsaved checkbox state cannot make hardware writes look dead.
- The DAC Bring-up path should explicitly refresh live module state after arming the DAC module plan, and it should retry `dac_debug_config` once after a forced module-plan resync if the controller still reports the DAC write gate as blocked.
- DAC80502 init on this board needs a short settle period after software reset before follow-on register writes. Without that delay, `dac_debug_config` can fail intermittently with `ESP_ERR_INVALID_RESPONSE` even though the DAC is physically present.
- DAC80502 on this board is powered from `3.3 V` and drives `0-2.5 V` command nets. With the internal `2.5 V` reference, `REF-DIV=1` is required on this board even at `gain x1`; otherwise `REF-ALARM` forces both outputs to `0 V` even though the DAC data registers still read back correctly.
- The LD (`LISH`) and TEC (`TMS`) setpoint editors on the Laser-driver and TEC Bring-up pages must stay tied to the exact same DAC channel A/B draft used by the DAC page. There is one DAC shadow plan in the host; changing a setpoint on any of those pages must update the others.
- The Laser-driver Bring-up page should frame DAC channel A in driver terms: staged `LISH` voltage, equivalent current, live `LIO` monitor voltage, live measured current, raw `TMO` voltage, and derived driver temperature. Show the raw analog terms directly; do not hide them behind a single generic temperature/current label.
- Laser-driver bring-up should treat the ATLS6A214 current span as `0-6 A` over `0-2.5 V`, but the host bring-up editor must clamp staged writes at `5 A` for safety. Live current meters may still use the full `0-6 A` display span.
- Laser-driver bring-up should expose service-only `SBDN` control on `GPIO13` in operator-facing terms: `Off`, `On`, `Standby`, plus an `Auto` release option back to firmware ownership. The UI may explain the electrical meaning, but it should not make operators think in raw names like `ON-PU` or `STANDBY-HiZ`. `Standby` should still use a real input / high-impedance override, not a fake digital level.
- `GPIO13` / `LD_SBDN` overrides must win over the normal actuator-target update loop. If a service override is active, the board layer must not briefly re-drive `SBDN` from the normal standby/output path on the next 5 ms control cycle, or the net will glitch even though the override looks active in software.
- Laser-driver bring-up should expose service-only `PCN` control on `GPIO21` with explicit `LISL`, `LISH`, and `Firmware` ownership states. This is a temporary bench override path and must not be implied as the normal runtime control scheme.
- Laser-driver bring-up green control should be a single service-only toggle that directly drives shared `GPIO37 / GN_LD_EN` high or low. Do not route that page through the normal `enable_alignment` runtime request path, because service mode suppresses the normal alignment-output logic.
- `GPIO21` / `LD_PCN` overrides must win over the normal actuator-target update loop. If a service override is active, the board layer must not immediately re-drive `PCN` from the normal low/high-current runtime path on the next cycle.
- On the real board, a working `PCN` override must be validated from live GPIO readback, not only from the requested override state. Host UI should show the actual live level and treat request-vs-live mismatch as a faulted condition, not as a successful `LISH` selection.
- Loop-good, rail-good, DAC reachable/configured, and `REF_ALARM` should all be visible on the Laser-driver page with strong color-coded status tags, because LD bring-up is otherwise too ambiguous when the analog path is only partially alive.
- `TMO` temperature on the Laser-driver page is only meaningful when the LD controller is not in `OFF-PD` and the LD rail `PGOOD` is high. When either condition is false, the page should show `TMO` as invalid rather than presenting the raw voltage-derived temperature as if it were trustworthy.
- The TEC Bring-up page must treat DAC channel B / `TMS` voltage as the single writable source of truth, with temperature and wavelength derived from the calibration table. Editing voltage, temperature, or wavelength there must immediately update the other two and stay in sync with the DAC page.
- The TEC Bring-up page should show a disabled live readback slider for current TEC temperature driven from realtime telemetry, not only a static numeric card. Keep that path at 5 Hz or better; the current target is the existing fast telemetry cadence rather than a slower status snapshot.
- STUSB4500 PDO writes should verify register readback and trigger PD soft-reset renegotiation before the host treats the apply as complete.
- The new `pd_save_firmware_plan` path is MCU-owned persistence, not STUSB4500 NVM. It must validate runtime PDO readback first, then save the verified plan into ESP32 NVS.
- If MCU-owned PDO auto-reconcile is enabled, firmware must compare the saved plan against live STUSB4500 runtime PDO readback and only write the chip when the live table does not already match.
- MCU-owned PDO auto-reconcile must stay throttled and conservative so a bad bench setup cannot create a rapid renegotiation/reboot loop.
- STUSB4500 NVM burn must stay separate from runtime PDO apply, must remain manufacturing/service only, and must warn explicitly that NVM endurance is finite.
- `pd_burn_nvm` now means: validate runtime PDO readback, burn the PDO-related bytes into the raw STUSB4500 NVM map, then compare raw NVM readback before reporting success.
- STUSB4500 NVM burn must preserve non-PDO bytes. Read the full 5-bank image first and only patch the PDO-related fields before writing it back.
- The per-PDO NVM LUT current set is: `0.50, 0.75, 1.00, 1.25, 1.50, 1.75, 2.00, 2.25, 2.50, 2.75, 3.00, 3.50, 4.00, 4.50, 5.00 A`. If a custom current is used, it must share the one common flexible-current field.
- The host-side PDO editor should stay constrained to auditable, selectable voltage/current choices instead of arbitrary free-form numbers, and firmware must still reject invalid combinations.
- Browser flashing now depends on an embedded `BSLFWS1` firmware signature block inside the ESP-IDF app image. The host must reject images with a missing, malformed, or mismatched signature block.
- The current signature block is a compatibility and provenance contract for bench flashing, not a tamper-proof PKI release-signing system. Do not describe it as cryptographic release security.
- The bench image now also brings up a dedicated Wi-Fi SoftAP bridge:
  - SSID: `BSL-HTLS-Bench`
  - password: `bslbench2026`
  - WebSocket URL: `ws://192.168.4.1/ws`
- Wireless is for monitoring, logs, bring-up, and bench control only. Browser flashing remains USB / Web Serial only.
- Station mode should preserve the bench AP management path. Do not tear down `BSL-HTLS-Bench` just because the controller also joins an external 2.4 GHz network; operators need a stable recovery/configuration channel for future SSID changes.
- The host Wi-Fi management controls must stay usable even while the controller is retrying a saved station connection. `stationConnecting` is status, not a UI lock; scan, save-and-join, and restore-bench-AP actions must remain operator-accessible.
- Host wireless configuration must not implicitly reuse a saved station password when the operator selects a different SSID. Password reuse must stay explicit and auditable so changing networks does not silently keep old credentials alive.
- The host must not auto-switch itself off the bench AP just because the controller reports a station IP. Moving from `ws://192.168.4.1/ws` to the station endpoint should stay an explicit operator choice.
- Wireless stability is more important than maximum telemetry cadence. If Wi-Fi/WebSocket starts churning, reduce wireless-side telemetry pressure and avoid dropping clients on the first async send failure.
- During short wireless reconnects, the host should preserve the last proven live snapshot and keep Bring-up/Control in a recovery posture instead of dropping every module back to `Awaiting probe`. Command gating may pause while reconnecting, but visible module truth should not thrash.
- Prefer Wi-Fi SoftAP + WebSocket over BLE for the bench host link. BLE is not the primary transport here because browser compatibility and sustained telemetry stability are weaker.
- Stability priority for wireless is: one dedicated SoftAP, one protocol, one operator laptop. Do not add a second wireless control surface with different semantics unless it is reviewed explicitly.
- The wireless bridge must always prefer the newest WebSocket client. If the ESP32 client table is full, evict the oldest slot instead of silently ignoring the new browser session.
- The host must serialize controller commands on `serial` and `wifi`. Background probes, liveness checks, and operator actions must not pile multiple in-flight commands onto the ESP32 command queue.
- WebSocket handshake probes should stop as soon as the controller sends any valid protocol traffic. Do not keep a startup ping flood running after protocol-ready.
- The Control page must render from the live merged snapshot, not only the base status snapshot, or staged bench changes will look one-shot and stale. In particular, NIR control should show `requested` vs `active` separately and surface the live prerequisites (`LD/TEC rail`, `SBDN`, `PCN`, `loop good`) instead of looking like a dead button when firmware is still gating output.
- Runtime bench output requests (`enable_alignment`, `disable_alignment`, `laser_output_enable`, `laser_output_disable`, `set_laser_power`, `set_target_temp`, `set_target_lambda`, `configure_modulation`) must not be trapped behind service mode. Service mode is for bring-up/config/override work; active output requests need to function in normal runtime states because service mode itself suppresses beam output.
- The Control page must use the acknowledgement-based command path when it first exits bring-up service mode and then applies a runtime command. Fire-and-forget ordering is not reliable enough for `exit_service_mode -> set_target_temp / set_target_lambda / laser_output_enable / laser_output_disable / configure_modulation`.
- Control-page runtime writes should reclaim ownership from bring-up shadows automatically. If service mode is still active, the host should exit service mode first so later runtime commands really take over DAC, GPIO, rail, and laser-path ownership instead of only updating staged text.
- The controller snapshot should publish both `requestedAlignmentEnabled` and `requestedNirEnabled`. Without the explicit staged-request fields, the host cannot distinguish `request accepted but still gated` from `button did nothing`.
- IMU and ToF invalid or stale conditions currently auto-clear in the bench image, but they still disable NIR immediately and remain logged.
- The bench IMU snapshot now publishes beam-frame pitch, roll, and a relative gyro-integrated yaw for host rendering. Only pitch is currently part of the safety interlock path; roll and yaw are telemetry only until mechanically validated.
- Host horizon UI must follow controller truth, not a host-only re-derivation. Because the firmware uses hysteresis, a slightly negative pitch can still remain blocked until it clears the lower reset threshold; the GUI should show both trip and clear thresholds instead of implying any negative pitch is automatically safe.
- Lambda drift and TEC temperature ADC trips currently auto-clear after their configured 2-second default hold windows; they should stay adjustable and auditable.
- If the host exposes a `disable all interlocks` control, it must stay overt, destructive-styled, confirmation-gated, and routed through the centralized safety path. It must never be hidden in a module page or implemented as an untracked side-channel bypass.
- The TEC bring-up page should expose the raw analog supervision names directly: `TMS` set voltage, `TMO` thermistor voltage, current temperature, `ITEC`, `VTEC`, `TEMPGD`, and TEC rail `PGOOD`. Do not make operators infer these from renamed generic labels when the controller already has the raw values.
- Firmware log mirroring to plain `ESP_LOGI` is disabled by default so the JSON host link stays deterministic.

Current bench validation on the attached board:

- `PROGRAMMING_ONLY` boot on plain USB host power: verified
- `enter_service_mode` / `get_status` over USB Serial/JTAG: verified
- IMU SPI `WHO_AM_I = 0x6C`: verified on hardware
- Bring-up profile save to NVS: verified
- Shared I2C bus recovery + scan diagnostics: verified, but current board shows `SDA=0, SCL=1` and `ESP_ERR_TIMEOUT`, so DAC / STUSB are not yet communicating on this hardware stack
- Structured peripheral snapshot fields and decoded bus-traffic view: build-verified; they still need fresh bench verification on the attached board after the updated image is flashed
- The uploaded ToF daughterboard is now source-backed: it uses `VL53L1X` on the shared I2C bus, exports `GPIO1` as `LD_GPIO`, and uses a separate `LD_INT` line to control the onboard LED boost driver. `XSHUT` is pulled up locally and is not exported to the MCU.
- The board layer now aliases that ToF wiring explicitly:
  - `GPIO4/GPIO5` -> `VL53L1X` I2C
  - `GPIO7` -> optional `GPIO1` interrupt input
  - `GPIO6` -> LED-control sideband, driven low only when the ToF module is explicitly declared present
- The current ToF bench path now includes a minimal `VL53L1X` ranging sequence derived from ST's published init/start/read/clear flow: boot-state readback, sensor-ID readback, default config load, long-distance mode, timing budget, intermeasurement setup, data-ready polling, range-status decode, and distance-mm readback.
- Raw `VL53L1X` distance register readback and the controller-promoted ToF distance are intentionally different now. The raw register is still shown for debugging, but the controller distance only promotes `range_status == 0` samples and smooths them with a short rolling filter before exposing them to the rest of the GUI.
- The ToF-board front illumination path on `GPIO6` / `TPS61169 CTRL` now uses a fixed `20 kHz` service PWM carrier by default. Duty may vary for bring-up, but the default carrier should stay consistent between firmware, protocol readback, and host copy.
- The host ToF page and protocol now expose actual low-level ToF peripheral truth:
  - `reachable`
  - `configured`
  - `bootState`
  - `sensorId`
  - `dataReady`
  - `rangeStatus`
  - `distanceMm`
- Host overview/status components may show raw `VL53L1X` distance readback when the peripheral is alive, but they must still keep the safety posture in `hold` until the controller marks the ToF sample `valid` and `fresh`. Do not visually turn a raw peripheral sample into a passed interlock.
- All safety thresholds, hysteresis, and stale-timeout edits belong only in `Bring-up -> Service`. Module pages may display those live values for context, but they must not own safety writes or shadow copies of the same policy.
- The Tools/Event decoder should treat `VL53L1X` at `0x29` as a first-class known I2C target. If it does not appear in the dropdown, the operator is almost certainly running a stale browser bundle and needs a hard refresh.

Remaining blockers before claiming hardware-test readiness:

- Shared I2C bus is physically or electrically stuck low on `SDA` on the current bench setup; DAC / STUSB / DRV2605 are not firmware-verified yet
- ToF runtime path is still unresolved at the driver/policy level, but the hardware model is now known (`VL53L1X` over I2C, not SPI)
- Two-stage trigger / button runtime path is still unresolved
- PD contract classification is still bench-oriented; it is not yet a finished production PD supervisor
- IMU runtime path is good enough for bench bring-up, but beam-axis sign/orientation still needs real mechanical validation on the assembled instrument

That is the correct current state for an unprovisioned, unvalidated codebase.

## What The Next Agent Should Do First

1. Diagnose and clear the shared-I2C `SDA` low condition on the real bench hardware before assuming any DAC / STUSB / DRV2605 firmware bug.
2. Once the bus is electrically healthy, verify DAC80502 read/write and STUSB4500 probe results with the existing service bus tools.
3. Validate LSM6DSO beam-axis sign and the mechanical IMU-to-beam transform on the assembled handheld, not just on the PCB.

## Firmware Signature Contract

- Browser flashing is only allowed for raw app binaries that embed a valid `laser_controller_firmware_signature_t` block in the `.rodata.bsl_fw_signature` section.
- The block currently binds:
  - product: `BSL-HTLS Gen2`
  - project: `bsl_laser_controller`
  - board: `esp32s3`
  - protocol: `host-v1`
  - scope: `main-controller`
- The host verifies the block by scanning the image for the `BSLFWS1` / `BSLEND1` sentinels and recomputing the embedded metadata SHA-256 payload digest.
- If the image lacks this block, or any field mismatches the expected contract, Web Serial flashing must stay blocked and explain why.
- If a future production signing chain is added, it must extend this contract instead of bypassing it.
- Do not extend browser flashing onto the wireless path unless it is separately bench-verified. Right now wireless is intentionally non-flashing.
4. Add the ToF driver with explicit invalid, saturated, stale, and timeout handling.
5. Add the two-stage trigger / button runtime path after the missing button-board details are resolved.
6. Tighten PD supervision into a finished production policy once STUSB4500 communication is stable.
7. Keep host compatibility by updating [protocol-spec.md](/Users/zz4/BSL/BSL-Laser/docs/protocol-spec.md) in the same patch whenever bench or bring-up fields change.
8. Keep firmware-update work behind service/programming state checks and never let the host bypass controller interlocks.

Before writing any new hardware driver, read these first:

- [hardware-recon.md](/Users/zz4/BSL/BSL-Laser/docs/hardware-recon.md)
- [datasheet-programming-notes.md](/Users/zz4/BSL/BSL-Laser/docs/datasheet-programming-notes.md)

Pay special attention to:

- `GPIO4/GPIO5` being the default shared I2C bus for `STUSB4500`, `DAC80502`, and `DRV2605`
- `GPIO4/GPIO5` also leaving the board through both the BMS and Sensor & LED connectors
- `GPIO48` being the dedicated `ERM_EN` line for the DRV2605 / ERM path
- `IO37` appearing to drive both `ERM_TRIG` and `GN_LD_EN`
- `GPIO6/GPIO7` being shared with the BMS battery-toggle connector and the Sensor & LED board
- the resolved ToF board facts:
  - `VL53L1X` on `GPIO4/GPIO5`
  - optional interrupt output on `GPIO7`
  - optional LED-control sideband on `GPIO6`
  - no exported `XSHUT` on this board revision
- the still-missing button-board files, which continue to block trigger confirmation
- the fact that STUSB4500 has no alert/status GPIOs wired back to the MCU
- the module-variant caveat around `GPIO35/36/37` and `GPIO47/48`

## Review Checklist For Any Change

1. Does this change preserve “laser off” on every new failure mode?
2. Does it create any hidden emission path outside the safety gate?
3. Are stale-data and comms-timeout cases explicit?
4. Are thresholds, hysteresis, and timeouts configurable rather than magic constants?
5. Are logs added for every beam-permission-affecting transition?
6. Does the change preserve deterministic startup and bounded runtime behavior?
7. Does the validation plan need to be updated?

## Do Not Merge These Kinds Of Changes

- “Temporary” interlock bypasses
- hidden service commands
- host GUI controls that imply the PC is the safety authority
- auto-retry logic that briefly re-enables output without a fresh safety evaluation
- silently accepting invalid calibration
- enabling a rail before power tier is known
- using IMU embedded features as the only shutdown authority
- clearing latched faults without an intentional operator or service action

## Expected Documentation Discipline

Whenever you change any of these, update the docs in the same patch:

- state machine
- fault classes or latching policy
- protocol surface
- calibration structure
- validation procedure
- driver bring-up sequence

If there is a conflict between convenience and auditability, choose auditability.

## CI/CD Expectations

- GitHub Actions in [ci-cd.yml](/Users/zz4/BSL/BSL-Laser/.github/workflows/ci-cd.yml) is now the expected baseline pipeline.
- Every push and pull request should build:
  - the ESP32-S3 firmware with ESP-IDF `v6.0`
  - the host console with `npm ci`, `npm run lint`, and `npm run build`
- The workflow should always publish build artifacts for bench use:
  - `bsl_laser_controller.bin`
  - `bootloader.bin`
  - `partition-table.bin`
  - `flasher_args.json`
  - built host-console `dist/`
- Tag pushes that start with `v` should publish those artifacts as a GitHub release.
- Do not weaken the pipeline by skipping lint or build steps just to get a green check. If a step is too flaky or slow, fix it explicitly.
