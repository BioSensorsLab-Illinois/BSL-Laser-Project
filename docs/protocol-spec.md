# Protocol Specification

## Goal

Define one command and telemetry model that can be carried over:

- USB CDC for development
- UART for bench debug
- the current desktop GUI
- Wi-Fi SoftAP + WebSocket without changing semantics

The active powered-ready host rewrite uses explicit command families:

- `status.*`
- `deployment.*`
- `operate.*`
- `integrate.*`

Legacy command names are still accepted as compatibility aliases during migration.

## Framing

Use newline-delimited JSON messages.

One message per line.

The current bench firmware accepts compact JSON emitted by the host GUI and also tolerates ordinary JSON whitespace in command envelopes. `type:"cmd"` remains the preferred explicit form, but the bench parser also accepts command lines that omit `type` during manual lab bring-up.

Examples:

```json
{"type":"cmd","id":1,"cmd":"get_status"}
{"type":"resp","id":1,"ok":true,"result":{"state":"SAFE_IDLE"}}
{"type":"event","event":"fault_latched","timestamp_ms":1234,"fault":"imu_stale"}
```

Why JSON Lines:

- easy to inspect by humans
- simple for desktop tools
- stable envelope for future transport wrappers
- no hidden binary parsing state during bring-up

## Message Types

### `cmd`

Operator or tool request.

Required fields:

- `type`
- `id`
- `cmd`

Optional:

- `args`

### `resp`

Direct reply to a `cmd`.

Required fields:

- `type`
- `id`
- `ok`

Optional:

- `result`
- `error`

### `event`

Asynchronous state/fault/log/telemetry publication.

Required fields:

- `type`
- `event`
- `timestamp_ms`

## Current Bench Image Commands

| Command | Purpose | Safety Notes |
| --- | --- | --- |
| `status.get` | full machine-readable status snapshot | safe read-only |
| `get_faults` | active and historical faults | safe read-only |
| `get_bringup_profile` | fetch service-only module expectations and tuning state | safe read-only |
| `clear_faults` | request fault clear | service-only; must not clear if recovery criteria fail |
| `deployment.enter` | enter deployment mode and reclaim ownership from bring-up service paths | deployment-only runtime gate; blocks bring-up writes while active |
| `deployment.exit` | leave deployment mode and return to non-deployed safe supervision | only allowed when the deployment checklist is not running |
| `deployment.run` | start the deployment checklist and stream progress asynchronously | deployment-only; checklist owns sequencing and safe abort |
| `deployment.set_target` | set the deployment temperature or wavelength target | deployment-only; updates the ready-posture target and checklist setpoint |
| `set_deployment_safety` | update deployment-mode safety thresholds and timeouts | deployment-only; applies live and may immediately drop active output |
| `operate.set_mode` | switch runtime control between `binary_trigger` and `modulated_host` | deployment-only mode seam; must be safe-off before switching |
| `operate.set_alignment` | set the normal green-laser request path | UNGATED at the software level (user directive 2026-04-14); accepted from any state. Hardware rail availability (TPS22918 on `VDS_TEC_5V0`) is the only remaining gate. |
| `operate.set_led` | set the normal GPIO6 LED request and brightness | aux-control; deployment must be active and the checklist must not be running; deployment still defaults GPIO6 low until requested |
| `operate.set_target` | stage the runtime TEC temperature or wavelength target | runtime-only; deployment must already be ready |
| `operate.set_output` | stage runtime output enable and current together | runtime-only; host request only, not direct output authority |
| `set_laser_power` | legacy alias for staging the bench high-state NIR current request | compatibility alias |
| `set_max_current` | alias for bench high-state NIR current staging | service-only; must never exceed provisioned safety ceiling |
| `integrate.set_safety` | update runtime safety thresholds, hysteresis, and hold windows and persist them immediately | integrate-only; reject invalid policy values and keep firmware authoritative |
| `set_runtime_safety` | legacy alias for runtime safety updates | compatibility alias |
| `pd_debug_config` | apply STUSB4500 runtime sink PDOs and update firmware power-tier thresholds | service-only; runtime-register write only, not a hidden NVM reprogramming path |
| `pd_save_firmware_plan` | validate runtime PDO readback, then save the plan into ESP32 firmware NVS for future auto-reconcile | service-only; MCU-owned persistence, separate from STUSB4500 NVM |
| `pd_burn_nvm` | validate runtime PDOs, then burn the requested STUSB4500 PDO startup defaults into NVM and verify raw NVM readback | service-only; manufacturing-only action with finite endurance, never for iterative tuning |
| `set_target_temp` | legacy alias for temperature target staging | compatibility alias |
| `set_target_lambda` | legacy alias for wavelength target staging | compatibility alias |
| `operate.set_modulation` | stage PCN PWM modulation request | runtime-only; host request only, not direct PWM authority |
| `configure_modulation` | legacy alias for modulation staging | compatibility alias |
| `laser_output_enable` | legacy alias for runtime NIR enable intent | compatibility alias |
| `laser_output_disable` | legacy alias for runtime NIR disable intent | compatibility alias |
| `enable_alignment` | legacy alias for enabling the normal green-laser request | compatibility alias; UNGATED like `operate.set_alignment` per user directive 2026-04-14 |
| `disable_alignment` | legacy alias for clearing the normal green-laser request | compatibility alias |
| `reboot` | controlled reboot | safe if outputs dropped first |
| `enter_service_mode` | protected service path | must be explicitly protected and logged |
| `exit_service_mode` | leave service mode | safe; auto-disables the USB-debug mock if it was active |
| `service.usb_debug_mock_enable` | engage the USB-Debug Mock layer (synthesized rail PGOOD + TEC/LD telemetry) | service-only; rejected unless `power_tier == programming_only` AND no fault is latched. The mock is hard-isolated and NEVER drives any GPIO. Auto-disables on real PD power, on any non-auto-clear fault, or on service-mode exit. |
| `service.usb_debug_mock_disable` | disengage the USB-Debug Mock layer | service-only; always accepted. The mock can also be disabled by the operator clicking the Integrate panel button or by any of the auto-disable triggers. |
| `apply_bringup_preset` | stage a known module-population profile such as `soc_imu_dac` | service-only; does not grant beam permission |
| `set_profile_name` | rename the active bring-up profile | safe bring-up metadata; does not require service mode |
| `set_module_state` | declare whether a module is expected present and debug-enabled | safe bring-up metadata; does not require service mode |
| `integrate.save_profile` | request device-side persistence of the bring-up profile | safe bring-up metadata write |
| `integrate.rgb_led.set` | drive the TLC59116 RGB status LED to a test color/blink state with a hold window | service-only; rejected when deployment is active or a fault is latched; hold_ms defaults to 5 000 ms, clamped to [1, 30000] |
| `integrate.rgb_led.clear` | cancel any active RGB LED test override and revert to firmware-computed state | always accepted while connected |
| `save_bringup_profile` | legacy alias for integrate profile persistence | compatibility alias |
| `scan_wireless_networks` | ask the controller to scan nearby Wi-Fi SSIDs for station-mode setup | read-only transport diagnostic; may briefly pause station reconnect attempts while the scan runs |
| `configure_wireless` | switch the controller between bench SoftAP and existing-Wi-Fi station mode | transport-management write; may intentionally drop the current wireless socket while the controller changes networks |
| `set_supply_enable` | request the LD or TEC MPM3530 VIN rail on/off during service bring-up | service-only; beam outputs stay forced safe |
| `refresh_pd_status` | force an immediate STUSB4500 contract/PDO refresh | read-only diagnostic for Integrate only; rejected while deployment mode is active |
| `dac_debug_set` | stage DAC channel shadow voltages for bench validation | service-only; actuator shadow, not direct beam authority |
| `dac_debug_config` | stage DAC reference, gain, divider, and update policy | service-only; safe shadow/config only |
| `imu_debug_config` | tune IMU ODR, full-scale, and runtime interface flags during bring-up | service-only |
| `tof_debug_config` | tune ToF thresholds and stale-data timeout during bring-up | service-only |
| `tof_illumination_set` | drive the front visible illumination path on `GPIO6` with service PWM | service-only; transient bring-up aid, never persisted |
| `haptic_debug_config` | stage DRV2605 mode, library, actuator, RTP level, and effect selection | service-only |
| `set_haptic_enable` | assert or clear the dedicated ERM driver enable pin on `GPIO48` | service-only; direct bench GPIO control for the motor path |
| `haptic_external_trigger_pattern` | fire a bounded external-trigger burst on shared `IO37 / GN_LD_EN` for ERM validation | service-only; hazardous shared net, requires explicit bench safeguards |
| `haptic_debug_fire` | fire a service-mode haptic pattern | service-only |
| `set_gpio_override` | force a selected ESP32 pad into firmware/input/output service ownership with optional pull and drive level | service-only; hazardous on USB, boot, and debug pins |
| `clear_gpio_overrides` | clear every GPIO override and return ownership to the original firmware logic | service-only; global recovery path for the GPIO inspector |
| `i2c_scan` | probe declared I2C targets | read-only diagnostic; does not require service mode |
| `i2c_read` | read an I2C register | read-only diagnostic; must be logged |
| `i2c_write` | write an I2C register | service-only; must be logged |
| `spi_read` | read an SPI register from a declared target | read-only diagnostic; must be logged |
| `spi_write` | write an SPI register to a declared target | service-only; must be logged |

Future production firmware is still expected to add validated config/calibration persistence commands such as `save_config`, `dump_calibration`, and `write_calibration`, but those are not part of the current live bench image contract.

Current bring-up-specific behavior:

- `enter_service_mode` updates `serviceModeRequested` immediately and `serviceModeActive` on the next control-cycle/state-machine pass.
- `set_profile_name`, `set_module_state`, and `save_bringup_profile` are intentionally allowed outside service mode so the bench plan can be saved without opening a write session.
- `scan_wireless_networks` is intentionally allowed outside service mode so the operator can discover nearby SSIDs before switching the controller into station mode.
- `scan_wireless_networks` may temporarily pause station reconnect attempts during the scan, then resume them automatically after results are captured.
- `configure_wireless` is intentionally allowed outside service mode so an operator can move the controller between bench SoftAP and an existing Wi-Fi network without opening a hazardous write session.
- Alignment (`operate.set_alignment`, `enable_alignment`, `disable_alignment`) is UNGATED at the software level (user directive 2026-04-14). Hardware rail availability is the only gate.
- The GPIO6 LED command (`operate.set_led`) is rejected unless deployment mode is active and the checklist is not currently running; this keeps the GPIO6 runtime/service ownership protocol clean.
- Runtime control commands such as `operate.set_target`, `operate.set_output`, and `operate.set_modulation` are rejected unless deployment mode is active and the deployment checklist has completed successfully in the current session.
- `operate.set_mode` is allowed while deployment mode is active and the laser path is safe-off. It is blocked while deployment is running, while requests are still staged, while modulation is still active, or while the controller is faulted.
- In `binary_trigger`, host-owned runtime output commands are rejected. The physical trigger path (MCP23017 on the button board, source-backed 2026-04-15) is the intended owner. `operate.set_mode { mode: "binary_trigger" }` is accepted whenever `buttonBoard.mcpReachable === true`; firmware rejects it with an explicit reason otherwise. Full powered-bench validation of this path is pending (see `button-trigger-pass` in `docs/validation-plan.md`).
- In `modulated_host`, host-owned runtime output commands are allowed after deployment readiness.
- `deployment.run` acknowledges start immediately. Terminal success or failure is observed through later status polling and telemetry updates, not by blocking the command response until completion.
- `deployment` telemetry now carries deployment-v2 truth directly:
  - `phase`
  - `readyIdle`
  - `readyQualified`
  - `readyInvalidated`
  - `primaryFailureCode`
  - `primaryFailureReason`
  - `secondaryEffects[]`
  - `readyTruth`
- Each deployment step now carries `startedAtMs` and `completedAtMs`.
- `integrate.set_safety` applies runtime safety immediately and also requests device-side persistence so the same thresholds survive reboot and are reused automatically by deployment.
- `integrate.set_safety` now owns `off_current_threshold_a`; the default is `0.2 A`, and idle bias current below that threshold is treated as intentional in deployment ready-idle posture.
- STUSB4500 ownership is intentionally narrow:
  - passive/general firmware reads are allowed for status and deployment qualification
  - one boot-time firmware reconcile write window only when firmware PDO auto-load-on-mismatch is enabled
  - explicit Integrate PD actions are the only non-boot path allowed to force refreshes or write STUSB runtime/NVM state
  - deployment and Operate paths must never trigger explicit STUSB refreshes, soft resets, PDO writes, or NVM writes
- `enable_alignment` and `disable_alignment` are intentionally rejected in v2 runtime flow. The old host alignment path is no longer the normal runtime authority.
- While deployment mode is active, bring-up and GPIO-mutating commands stay rejected even if the controller is otherwise online. Read-only status, fault, telemetry, and wireless-management commands remain available.
- `configure_wireless` accepts `mode:"softap"` or `mode:"station"`. In station mode the host may also provide `ssid` and `password`; if the password is omitted, the controller reuses the saved credential.
- `i2c_scan`, `i2c_read`, and `spi_read` are intentionally allowed outside service mode because they are read-only probes.
- `refresh_pd_status` remains an explicit Integrate PD action and is rejected when deployment mode is active.
- `set_supply_enable` is intentionally separate from beam-control commands. In `SERVICE_MODE`, it only requests the LD or TEC MPM3530 VIN rail while alignment stays off and the laser driver stays in standby.
- `set_haptic_enable` is intentionally separate from `haptic_debug_config`. It directly controls the dedicated ERM enable line on `GPIO48` during service bring-up so the operator can prove the motor power path independently from DRV2605 register writes.
- `haptic_external_trigger_pattern` is intentionally hazardous and transient. It is only for bench validation of DRV2605 external-trigger modes and must never hide the fact that `IO37` is a shared `ERM_TRIG / GN_LD_EN` net.
- `tof_illumination_set` is intentionally transient. It drives the TPS61169 `CTRL` pin on `GPIO6` with a service-only PWM dimming signal and must drop low again when service mode closes, the ToF write gate closes, or the host explicitly disables it.
- `set_gpio_override` is intentionally separate from all module tools. It is the service-owned escape hatch for direct ESP32 pad control during controlled bench work.
- `clear_gpio_overrides` is the required reset path for the GPIO inspector. It must clear every override, rebuild the board GPIO state, and return ownership to the original firmware logic.
- `i2c_scan` now reports raw shared-bus line levels in failure text when the bus is unavailable, for example `Shared I2C unavailable (ESP_ERR_TIMEOUT, SDA=0, SCL=1).`
- `pd_debug_config` now writes the runtime PDO registers, verifies the STUSB4500 readback, and sends a PD soft reset so the source renegotiates against the new PDO set.
- `pd_save_firmware_plan` first applies the same runtime PDO update, then refreshes live STUSB4500 readback, and only saves the plan into MCU NVS if the live PDO table matches the requested plan.
- `pd_save_firmware_plan` also stores whether firmware auto-reconcile is enabled. When enabled, the ESP32 compares the saved PDO plan against live STUSB4500 runtime PDO readback when the controller is online and only re-applies it if the chip does not already match.
- Firmware PDO auto-reconcile is boot-time only. After the boot reconcile window closes, firmware does not revisit STUSB4500 automatically.
- `pd_burn_nvm` is intentionally a separate command from runtime PDO apply so the host can warn about finite NVM endurance and final-provisioning intent.
- `pd_burn_nvm` first applies the requested runtime PDOs, refreshes live STUSB4500 readback, aborts if runtime verification fails, then writes the STUSB4500 raw NVM banks and compares raw NVM readback before reporting success.
- The current implementation preserves non-PDO NVM bytes by reading the full 5-bank map first and only patching the PDO-related fields before writing the full image back.
- The bench block now carries `hostControlReadiness` with three reason tokens and the three-state SBDN posture:
  - `nirBlockedReason` ∈ {`none`, `not-connected`, `fault-latched`, `deployment-off`, `checklist-running`, `checklist-not-ready`, `ready-not-idle`, `not-modulated-host`, `power-not-full`, `rail-not-good`, `tec-not-settled`}. The host uses this token to pre-disable the NIR controls with a precise tooltip reason; no client-side gate calculation is needed.
  - `alignmentBlockedReason` is always `none` — green alignment is ungated at the software level.
  - `ledBlockedReason` ∈ {`none`, `not-connected`, `deployment-off`, `checklist-running`}.
  - `sbdnState` ∈ {`off`, `on`, `standby`} — ON = GPIO drive HIGH, OFF = GPIO drive LOW (fast 20 us shutdown), STANDBY = GPIO in input/Hi-Z mode (external R27/R28 divider holds ~2.25 V, in the datasheet 2.1-2.4 V standby band). See `components/laser_controller/include/laser_controller_board.h` for the enum and `docs/Datasheets/ATLS6A214D-3.pdf` for the datasheet reference.
- The legacy `laser.driverStandby` bool is retained for backward compatibility. It returns TRUE whenever `sbdnState != "on"` (i.e. driver is OFF or STANDBY). New consumers should read `bench.hostControlReadiness.sbdnState` directly.
- The bench block also carries `usbDebugMock` for operator visibility into the USB-Debug Mock Layer:
  - `active` — true when the firmware is currently substituting synthesized TEC/LD rail PGOOD and telemetry. The host MUST render an app-wide banner whenever this is true (failing to surface this is a safety-visibility regression — see `.agent/AGENT.md` "USB-Only Debug Power").
  - `pdConflictLatched` — true when real PD power was detected during mock activity and the firmware latched the SYSTEM_MAJOR fault `usb_debug_mock_pd_conflict`. The mock cannot be re-enabled until the fault is cleared.
  - `enablePending` — true when an enable request has been queued for the control task to process on its next tick but has not yet been consumed. Emitted every status frame by `laser_controller_comms.c` alongside `active`; `types.ts` `UsbDebugMockStatus.enablePending`.
  - `activatedAtMs` / `deactivatedAtMs` — uptime timestamps for diagnostics.
  - `lastDisableReason` — short string describing why the mock was last disabled (e.g. "real PD power detected", "service mode exited", "operator request", "fault transition").
- New fault code `usb_debug_mock_pd_conflict` (class `system_major`) is latched when the USB-debug mock auto-disables because real PD power arrived. Cleared by the standard `clear_faults` flow.

## Minimum Status Payload

`get_status` should include the normal controller snapshot plus staged bench-control state and the service-only bring-up section:

```json
{
  "identity": {
    "firmwareVersion": "0.2.0",
    "hardwareRevision": "rev-A",
    "serialNumber": "BSL-000123"
  },
  "session": {
    "state": "SERVICE_MODE",
    "powerTier": "full"
  },
  "wireless": {
    "started": true,
    "mode": "station",
    "apReady": true,
    "stationConfigured": true,
    "stationConnecting": false,
    "stationConnected": true,
    "clientCount": 1,
    "ssid": "LabNet-5G",
    "stationSsid": "LabNet-5G",
    "stationRssiDbm": -54,
    "stationChannel": 149,
    "scanInProgress": false,
    "scannedNetworks": [
      { "ssid": "LabNet-5G", "rssiDbm": -54, "channel": 149, "secure": true },
      { "ssid": "LabNet-Guest", "rssiDbm": -70, "channel": 11, "secure": false }
    ],
    "ipAddress": "192.168.1.42",
    "wsUrl": "ws://192.168.1.42/ws",
    "lastError": ""
  },
  "pd": {
    "contractValid": true,
    "negotiatedPowerW": 45.0,
    "sourceVoltageV": 20.0,
    "sourceCurrentA": 2.25,
    "operatingCurrentA": 2.25,
    "contractObjectPosition": 3,
    "sinkProfileCount": 3,
    "sinkProfiles": [
      { "enabled": true, "voltageV": 5.0, "currentA": 3.0 },
      { "enabled": true, "voltageV": 15.0, "currentA": 2.0 },
      { "enabled": true, "voltageV": 20.0, "currentA": 2.25 }
    ]
  },
  "rails": {
    "ld": { "enabled": false, "pgood": false },
    "tec": { "enabled": false, "pgood": false }
  },
  "imu": {
    "valid": true,
    "fresh": true,
    "beamPitchDeg": -12.5,
    "beamRollDeg": 3.4,
    "beamYawDeg": 18.9,
    "beamYawRelative": true,
    "beamPitchLimitDeg": 0.0
  },
  "tof": {
    "valid": false,
    "fresh": false,
    "distanceM": 0.0
  },
  "laser": { "nirEnabled": false, "driverStandby": true },
  "tec": {
    "targetTempC": 25.0,
    "targetLambdaNm": 785.0,
    "actualLambdaNm": 784.7,
    "tempGood": false,
    "tempC": 24.8,
    "tempAdcVoltageV": 1.115,
    "currentA": 0.3,
    "voltageV": 2.1
  },
  "gpioInspector": {
    "anyOverrideActive": true,
    "activeOverrideCount": 1,
    "pins": [
      [48, 197, 6]
    ]
  },
  "bench": {
    "targetMode": "lambda",
    "requestedNirEnabled": false,
    "modulationEnabled": false,
    "modulationFrequencyHz": 1000,
    "modulationDutyCyclePct": 50,
    "lowStateCurrentA": 0.0,
    "hostControlReadiness": {
      "nirBlockedReason": "deployment-off",
      "alignmentBlockedReason": "none",
      "ledBlockedReason": "deployment-off",
      "sbdnState": "off"
    }
  },
  "safety": {
    "allowAlignment": true,
    "allowNir": false,
    "horizonBlocked": false,
    "distanceBlocked": false,
    "lambdaDriftBlocked": false,
    "tecTempAdcBlocked": false,
    "horizonThresholdDeg": 0.0,
    "horizonHysteresisDeg": 3.0,
    "tofMinRangeM": 0.2,
    "tofMaxRangeM": 1.0,
    "tofHysteresisM": 0.02,
    "imuStaleMs": 50,
    "tofStaleMs": 100,
    "railGoodTimeoutMs": 250,
    "lambdaDriftLimitNm": 5.0,
    "lambdaDriftHysteresisNm": 0.5,
    "lambdaDriftHoldMs": 2000,
    "ldOvertempLimitC": 55.0,
    "tecTempAdcTripV": 2.45,
    "tecTempAdcHysteresisV": 0.05,
    "tecTempAdcHoldMs": 2000,
    "tecMinCommandC": 15.0,
    "tecMaxCommandC": 35.0,
    "tecReadyToleranceC": 0.25,
    "maxLaserCurrentA": 5.0,
    "actualLambdaNm": 784.7,
    "targetLambdaNm": 785.0,
    "lambdaDriftNm": 0.3,
    "tempAdcVoltageV": 1.115
  },
  "bringup": {
    "serviceModeRequested": true,
    "serviceModeActive": true,
    "profileName": "soc-imu-dac",
    "profileRevision": 4,
    "persistenceDirty": true,
    "persistenceAvailable": false,
    "power": {
      "ldRequested": false,
      "tecRequested": true
    },
    "illumination": {
      "tof": {
        "enabled": true,
        "dutyCyclePct": 35,
        "frequencyHz": 5000
      }
    },
    "modules": {
      "imu": { "expectedPresent": true, "debugEnabled": true, "detected": true, "healthy": true },
      "tof": { "expectedPresent": true, "debugEnabled": true, "detected": true, "healthy": true }
    },
    "tuning": {
      "dacLdChannelV": 0.0,
      "dacTecChannelV": 0.0,
      "dacReferenceMode": "internal",
      "dacGain2x": true,
      "dacRefDiv": false,
      "dacSyncMode": "async",
      "imuOdrHz": 208,
      "imuAccelRangeG": 4,
      "imuGyroRangeDps": 500,
      "imuGyroEnabled": true,
      "imuLpf2Enabled": false,
      "imuTimestampEnabled": true,
      "imuBduEnabled": true,
      "imuIfIncEnabled": true,
      "imuI2cDisabled": true,
      "tofMinRangeM": 0.2,
      "tofMaxRangeM": 1.0,
      "tofStaleTimeoutMs": 150,
      "pdProfiles": [
        { "enabled": true, "voltageV": 5.0, "currentA": 3.0 },
        { "enabled": true, "voltageV": 15.0, "currentA": 2.0 },
        { "enabled": true, "voltageV": 20.0, "currentA": 2.25 }
      ],
      "pdProgrammingOnlyMaxW": 30.0,
      "pdReducedModeMinW": 30.0,
      "pdReducedModeMaxW": 35.0,
      "pdFullModeMinW": 35.1,
      "hapticEffectId": 47,
      "hapticMode": "internal_trigger",
      "hapticLibrary": 1,
      "hapticActuator": "erm",
      "hapticRtpLevel": 96
    },
    "tools": {
      "lastI2cScan": "0x28 0x48",
      "lastI2cOp": "read 0x48 reg 0x00 -> 0x5A",
      "lastSpiOp": "imu reg 0x0F -> 0x6C",
      "lastAction": "IMU bring-up configuration staged."
    }
  },
  "fault": {
    "latched": true,
    "activeCode": "invalid_config"
  }
}
```

## Periodic Telemetry Events

The bench image now uses two async telemetry tiers:

- `live_telemetry`: the lightweight high-rate stream for UI motion and fast numeric readback. It carries the current `session`, `pd`, `bench`, `rails`, `imu`, `tof`, `laser`, `tec`, `safety`, `buttons`, compact `bringup` service state, deployment state, and split `fault` summary.
- `status_snapshot`: the slower richer snapshot for periodic reconciliation. It now also carries `bench`, `buttons`, identity, GPIO inspector state, haptic peripheral readback, and the compact bring-up status block.

Representative `live_telemetry` event:

```json
{
  "type": "event",
  "event": "live_telemetry",
  "timestamp_ms": 123456,
  "detail": "High-rate controller telemetry.",
  "payload": {
    "session": { "uptimeSeconds": 123, "state": "SERVICE_MODE", "powerTier": "full" },
    "imu": {
      "valid": true,
      "fresh": true,
      "beamPitchDeg": -12.5,
      "beamRollDeg": 3.4,
      "beamYawDeg": 18.9,
      "beamYawRelative": true,
      "beamPitchLimitDeg": 0.0
    },
    "tof": { "valid": true, "fresh": true, "distanceM": 0.42, "minRangeM": 0.2, "maxRangeM": 1.0 },
    "laser": { "alignmentEnabled": false, "nirEnabled": false, "driverStandby": true, "measuredCurrentA": 0.0, "commandedCurrentA": 0.0, "loopGood": true, "driverTempC": 29.4 },
    "tec": { "targetTempC": 25.0, "targetLambdaNm": 785.0, "actualLambdaNm": 784.7, "tempGood": false, "tempC": 24.8, "tempAdcVoltageV": 1.115, "currentA": 0.3, "voltageV": 2.1, "settlingSecondsRemaining": 1 },
    "buttons": { "stage1Pressed": false, "stage2Pressed": false, "stage1Edge": false, "stage2Edge": false },
    "bringup": {
      "serviceModeRequested": true,
      "serviceModeActive": true,
      "illumination": {
        "tof": { "enabled": true, "dutyCyclePct": 35, "frequencyHz": 5000 }
      }
    },
    "bench": {
      "targetMode": "lambda",
      "runtimeMode": "modulated_host",
      "requestedAlignmentEnabled": false,
      "requestedNirEnabled": false,
      "requestedCurrentA": 0.0,
      "requestedLedEnabled": false,
      "requestedLedDutyCyclePct": 0
    },
    "fault": {
      "latched": false,
      "activeCode": "none",
      "activeClass": "none",
      "latchedCode": "none",
      "latchedClass": "none",
      "activeCount": 0,
      "tripCounter": 0
    }
  }
}
```

`gpioInspector` is live SoC pad truth from the board layer:

- `pins` is now compact on the wire: each entry is `[gpioNum, liveFlags, overrideFlags]`.
- `liveFlags` bit layout is:
  - bit 0: `outputCapable`
  - bit 1: `inputEnabled`
  - bit 2: `outputEnabled`
  - bit 3: `openDrainEnabled`
  - bit 4: `pullupEnabled`
  - bit 5: `pulldownEnabled`
  - bit 6: `levelHigh`
  - bit 7: `overrideActive`
- `overrideFlags` bit layout is:
  - bits 0-1: `overrideMode` (`0 = firmware`, `1 = input`, `2 = output`)
  - bit 2: `overrideLevelHigh`
  - bit 3: `overridePullupEnabled`
  - bit 4: `overridePulldownEnabled`
- `inputEnabled`, `outputEnabled`, `openDrainEnabled`, `pullupEnabled`, `pulldownEnabled`, and `levelHigh` remain actual controller readback after the host expands the compact tuple.
- `overrideActive`, `overrideMode`, and the `override*` fields still describe only the service override path.
- Host UI should always distinguish live readback from staged override draft values.

Wireless transport notes:

- The current bench image brings up a dedicated SoftAP:
  - SSID: `BSL-HTLS-Bench`
  - password: `bslbench2026`
  - WebSocket endpoint: `ws://192.168.4.1/ws`
- Wireless carries the same newline-delimited JSON protocol as USB CDC.
- Firmware flashing remains USB / Web Serial only. Wireless is for monitoring, logs, service-mode bring-up, and bench control.

## Logging and Event Rules

## Bring-Up Tuning Notes

- `dac_debug_config` currently mirrors:
  - `reference_mode`: `internal` or `external`
  - `gain_2x`: boolean
  - `ref_div`: boolean
  - `sync_mode`: `async` or `sync`
- `imu_debug_config` currently mirrors:
  - `odr_hz`
  - `accel_range_g`
  - `gyro_range_dps`
  - `gyro_enabled`
  - `lpf2_enabled`
  - `timestamp_enabled`
  - `bdu_enabled`
  - `if_inc_enabled`
  - `i2c_disabled`
- `pd_debug_config` currently mirrors:
  - `pdo1_enabled` / `pdo1_voltage_v` / `pdo1_current_a`
  - `pdo2_enabled` / `pdo2_voltage_v` / `pdo2_current_a`
  - `pdo3_enabled` / `pdo3_voltage_v` / `pdo3_current_a`
  - `programming_only_max_w`
  - `reduced_mode_min_w`
  - `reduced_mode_max_w`
  - `full_mode_min_w`
  - The bench image writes STUSB4500 runtime PDO registers plus the live firmware power-tier classifier.
  - PDO1 is normalized to the mandatory 5 V fallback.
  - PDO3 is only kept active when PDO2 is also enabled.
- `pd_save_firmware_plan` mirrors the same fields plus:
  - `firmware_plan_enabled`
  - The controller only saves the plan if runtime readback verification succeeds.
  - The saved firmware plan is MCU-owned NVS state, not STUSB4500 NVM.
- `tof_debug_config` currently mirrors:
  - `min_range_m`
  - `max_range_m`
  - `stale_timeout_ms`
- `tof_illumination_set` currently mirrors:
  - `enabled`
  - `duty_cycle_pct`
  - `frequency_hz`
  - Bench default and host bring-up carrier are now `20 kHz` unless an explicit override is sent.
  - This command is transient service state only; it does not change the saved bring-up profile.
- `set_runtime_safety` currently mirrors:
  - `horizon_threshold_deg`
  - `horizon_hysteresis_deg`
  - `tof_min_range_m`
  - `tof_max_range_m`
  - `tof_hysteresis_m`
  - `imu_stale_ms`
  - `tof_stale_ms`
  - `rail_good_timeout_ms`
  - `lambda_drift_limit_nm`
  - `lambda_drift_hysteresis_nm`
  - `lambda_drift_hold_ms`
  - `ld_overtemp_limit_c`
  - `tec_temp_adc_trip_v`
  - `tec_temp_adc_hysteresis_v`
  - `tec_temp_adc_hold_ms`
  - `tec_min_command_c`
  - `tec_max_command_c`
  - `tec_ready_tolerance_c`
  - `max_laser_current_a`
- `haptic_debug_config` currently mirrors:
  - `effect_id`
  - `mode`
  - `library`
  - `actuator`
  - `rtp_level`

The bench firmware still treats these as staged service-profile values. They are not direct hardware authority.

Publish `event` messages for:

- `status_snapshot` whenever the host asks for status or after a mutating command changes staged state
- `log` for human-readable structured event history
- state transitions
- PD contract changes
- rail enables
- rail-good changes
- IMU stale/invalid
- ToF stale/invalid/out-of-range
- horizon crossed
- lambda drift high / lambda drift recovered
- TEC temperature ADC high / recovered
- TEC settled / TEC timeout
- NIR armed
- NIR enabled
- NIR disabled
- fault latched
- fault cleared

## Security And Safety Constraints

- No normal command may bypass the safety gate.
- IMU and ToF invalid or stale conditions are currently modeled as immediate auto-clear interlocks in the bench image; they still disable NIR and remain logged.
- Lambda drift and TEC temperature ADC trips are currently modeled as immediate auto-clear interlocks after their configured hold windows expire.
- Bench and service-only mutating commands must only be accepted while service mode is requested or active.
- Bench commands stage host intent only. They must never directly toggle a beam-enable GPIO, DAC output, or rail.
- Service-only commands must never appear in standard operator UI.
- Service mode may relax expected-absence faults for declared missing modules only while beam permission remains disabled.
- Service mode must keep alignment, NIR, LD VIN, and TEC VIN derived outputs forced safe unless a future audited policy says otherwise.
- Every mutating command affecting beam permission must be logged.
- Invalid arguments must fail closed and return an explicit error object.

## Button board (2026-04-15)

### Snapshot — extended `buttons` block

The existing `buttons` snapshot block now publishes side-button state and ISR-counter telemetry alongside the original two-stage main-button fields:

```json
{
  "buttons": {
    "stage1Pressed": false,
    "stage2Pressed": false,
    "stage1Edge": false,
    "stage2Edge": false,
    "side1Pressed": false,
    "side2Pressed": false,
    "side1Edge": false,
    "side2Edge": false,
    "boardReachable": true,
    "isrFireCount": 0
  }
}
```

`boardReachable` is `false` whenever the MCP23017 has been declared unreachable by the firmware after the consecutive-failure threshold. While `false`, every press field reads `false` regardless of physical state — fail-safe posture.

### Snapshot — new `buttonBoard` block

```json
{
  "buttonBoard": {
    "mcpAddr": "0x20",
    "tlcAddr": "0x60",
    "mcpReachable": true,
    "mcpConfigured": true,
    "mcpLastError": 0,
    "mcpConsecFailures": 0,
    "tlcReachable": true,
    "tlcConfigured": true,
    "tlcLastError": 0,
    "isrFireCount": 0,
    "rgb": {
      "r": 0,
      "g": 0,
      "b": 255,
      "blink": false,
      "enabled": true,
      "testActive": false
    },
    "ledBrightnessPct": 20,
    "ledOwned": false,
    "triggerLockout": false,
    "triggerPhase": "ready"
  }
}
```

`triggerPhase` is one of `"off"`, `"ready"`, `"armed"`, `"firing"`, `"interlock"`, `"lockout"`, `"unrecoverable"`. The firmware computes this from the current decision so the host doesn't have to mirror gate ordering. See `laser_controller_comms_trigger_phase` in `components/laser_controller/src/laser_controller_comms.c` for the canonical computation.

`rgb` mirrors `laser_controller_rgb_led_state_t` plus `testActive`, which is `true` whenever an integrate-test override is currently driving the LED.

`ledOwned` is `true` when the binary-trigger button policy is the active source for the GPIO6 front LED brightness.

### Commands — added

- `integrate.rgb_led.set { r: 0..255, g: 0..255, b: 0..255, blink: bool, hold_ms?: number }`
  - Service-mode-only. Rejected when deployment is active or a fault is latched.
  - `hold_ms` defaults to 5 000 ms; clamped to `[1, 30000]`.
  - Drives the TLC59116 directly; firmware reverts to the computed RGB state at the end of the hold window.
  - Mirrored in mock at `host-console/src/lib/mock-transport.ts`.
- `integrate.rgb_led.clear`
  - Cancels any active RGB test override immediately and reverts to firmware-computed state. Always accepted while connected.

### Commands — semantics changed

- `operate.set_mode { mode: "binary_trigger" }` is now allowed whenever `buttonBoard.mcpReachable === true`. Previously this mode was blocked at the firmware dispatcher. The other host-imposed gates (deployment running, fault latched, bench requests pending, etc.) are unchanged.

### Faults — added

- `ld_lp_good_timeout` (class `system_major`) — fired by the deployment checklist when SBDN HIGH does not produce LD_LPGD HIGH within 1 s. The deployment fails with this as the primary failure code.
- `button_board_i2c_lost` (class `system_major`) — fired by the control task when binary_trigger mode is active and the MCP23017 has been declared unreachable. The fault collapses rails safe via the standard SYSTEM_MAJOR override path; the RGB status LED switches to flash-red.

### Deployment checklist — added

- New step `lp_good_check`, inserted between `tec_settle` and `ready_posture`.
  - Drives SBDN to ON with `select_driver_low_current=true` (PCN low) and waits up to 1 s for LD_LPGD to assert. Failure raises `ld_lp_good_timeout` and aborts the deployment.

### `safety.maxTofLedDutyCyclePct` — new safety field (2026-04-15 later)

Hard safety cap on the GPIO6 ToF-board front LED duty-cycle percent (0..100). Enforced at EVERY illumination entry point in the board layer:

- `laser_controller_board_set_tof_illumination` (service / bringup path)
- `laser_controller_board_set_runtime_tof_illumination` (runtime / operate / button-policy path)

A caller requesting duty above the cap is silently clamped. Default is 50 % (user directive 2026-04-15 after an unexpected LED-on event at full brightness). The cap is pushed from `config.thresholds.max_tof_led_duty_cycle_pct` into the board layer every control tick by `app.c`.

Editable via `integrate.set_safety { max_tof_led_duty_cycle_pct: N }` while in service mode. Values > 100 are rejected by `laser_controller_config_validate_runtime_safety`. Persisted via the service profile blob.

Device-side config version bumped to 2 to reject stale NVS blobs from pre-cap firmware. On upgrade, defaults are re-loaded (cap = 50).

Four-place sync target: `laser_controller_config.h` thresholds struct, `laser_controller_comms.c` safety JSON writer + `integrate.set_safety` parser, `host-console/src/types.ts SafetyStatus`, this doc.

### `buttons.*Pressed` fast-telemetry wire-format extension (2026-04-15 later)

`encode_button_flags` now packs side buttons into bits 2/3 of the binary fast-telemetry payload:

- bit 0 = `stage1Pressed`
- bit 1 = `stage2Pressed`
- bit 2 = `side1Pressed` (added 2026-04-15)
- bit 3 = `side2Pressed` (added 2026-04-15)

Host decoder in `host-console/src/lib/controller-protocol.ts` emits only the four press fields as a partial patch so other `buttons.*` fields (edges, `boardReachable`, `isrFireCount`) from the 1 s live_telemetry / periodic status snapshot are not clobbered every 60 ms. Bugfix for a regression that caused side-button state to revert to idle in under 100 ms even while held.

### `bench.appliedLedOwner` — telemetry field documented (2026-04-15)

Pre-existing field on the `bench` snapshot block; first documented here as part of the button-board landing because the firmware now emits a new value on this enum.

```
bench.appliedLedOwner: "none" | "integrate_service" | "operate_runtime"
                                                    | "button_trigger" | "deployment"
```

Reports which control source last drove the GPIO6 front LED. Computed by `laser_controller_comms_led_owner_name` in firmware — keep the priority order in lockstep when adding new sources:

| Priority | Value | Conditions |
|---|---|---|
| 1 | `integrate_service` | service mode requested AND `bringup.illumination.tof.enabled` |
| 2 | `operate_runtime` | `bench.illumination_enabled` |
| 3 | `button_trigger` | `button_runtime.led_owned` (binary_trigger + deployment ready_idle + stage1 pressed) — added 2026-04-15 |
| 4 | `deployment` | deployment.active AND GPIO6 reads low (deployment-entry forced-off path) |
| 5 | `none` | otherwise |

Mock transport mirrors this priority order in `mock-transport.ts` (bench snapshot construction). Host UI uses the value to label the LED-owner chip in the Operate GPIO6 LED card.

### Press-and-hold lockout (binary_trigger)

When an interlock fires while the operator is holding either trigger stage, the firmware latches `buttonBoard.triggerLockout = true`. The lockout forces NIR off for the rest of that press, even if the underlying interlock auto-clears. Released only when **both** stage1 and stage2 read inactive on the same control-task tick. Release semantics per user directive 2026-04-15.

## ToF calibration (2026-04-15 later)

### Snapshot — `bringup.tuning.tofCalibration`

The VL53L1X runtime calibration is published in the bring-up tuning block:

```json
{
  "bringup": {
    "tuning": {
      "tofCalibration": {
        "distanceMode": "long",
        "timingBudgetMs": 50,
        "roiWidthSpads": 16,
        "roiHeightSpads": 16,
        "roiCenterSpad": 199,
        "offsetMm": 0,
        "xtalkCps": 0,
        "xtalkEnabled": false
      }
    }
  }
}
```

Fields:

- `distanceMode` — one of `"short" | "medium" | "long"`. Short ≤ 1.3 m best ambient immunity; medium ≤ 3.0 m; long ≤ 4.0 m (default). Maps to VL53L1X VCSEL period + valid-phase-high + WOI / initial-phase registers per ST ULD reference driver.
- `timingBudgetMs` — one of `20 | 33 | 50 | 100 | 200`. Lower = faster refresh, higher = lower noise. Must be ≤ the VL53L1X inter-measurement period.
- `roiWidthSpads` / `roiHeightSpads` — ROI footprint on the 16×16 SPAD array, 4..16 each. Smaller ROI narrows the effective FoV — 4×4 ≈ 7.5° cone, 8×8 ≈ 15°, 16×16 ≈ 27° default.
- `roiCenterSpad` — packed SPAD index for ROI centre. Default 199 = grid centre. Host UI exposes this as a row/col pair computed via the ST ULD formula.
- `offsetMm` — signed distance offset correction (mm). Calibrated against an 18 % grey target at a known distance: `offset_mm = known_mm − raw_mm`. Written to VL53L1X register 0x001E as `offset_mm * 4` (1/4 mm units) per the ULD driver.
- `xtalkCps` — crosstalk-compensation counts-per-second. Measured with no target in front to capture cover-glass leakage.
- `xtalkEnabled` — gate for applying the crosstalk register. When `false`, register 0x0016 is written 0.

### Command — `integrate.tof.set_calibration`

Gated on service mode + no deployment + no fault latched. All fields optional — unspecified fields retain the current persisted value.

```json
{ "type": "cmd", "cmd": "integrate.tof.set_calibration", "args": {
  "distance_mode": "short",
  "timing_budget_ms": 33,
  "roi_width_spads": 12,
  "roi_height_spads": 12,
  "roi_center_spad": 145,
  "offset_mm": 25,
  "xtalk_cps": 75,
  "xtalk_enabled": false
}}
```

Behavior:

1. Validates each field against its allowed range (distance mode enum, timing budget one of 5 presets, ROI 4..16, values sane).
2. Writes to the **`tof_cal` NVS blob** (namespace `laser_ctrl`, separate from the main service-profile blob) immediately — no secondary "save" command needed.
3. Applies to the VL53L1X live: stops ranging, writes distance-mode / timing-budget / ROI / offset / xtalk, restarts ranging. Typical end-to-end latency < 80 ms.
4. The firmware ToF init path also reads this calibration on every init (boot, service-mode exit, fault recovery) so the operator's calibration survives reboot automatically.

### Four-place sync targets

- Firmware struct: `laser_controller_tof_calibration_t` in `laser_controller_config.h`.
- Firmware dispatcher + JSON emit: `laser_controller_comms.c` (`integrate.tof.set_calibration` handler + `tofCalibration` JSON block in the bring-up tuning writer).
- Firmware persistence: `laser_controller_service.c` — dedicated `tof_cal` NVS blob with a 1-byte version prefix; `laser_controller_service_set_tof_calibration` + `laser_controller_service_get_tof_calibration`.
- Firmware application: `laser_controller_board.c` — `laser_controller_board_tof_set_distance_mode`, `set_roi`, `set_offset_mm`, `set_xtalk` primitives + `apply_calibration` entry point called from the ToF init sequence.
- Host types: `host-console/src/types.ts` — `TofCalibration`, `TofDistanceMode`, `BringupTuning.tofCalibration`.
- Host defaults + mock: `host-console/src/lib/bringup.ts` default + `host-console/src/lib/mock-transport.ts` command handler.
- Host UI: `host-console/src/components/TofCalibrationPanel.tsx` — mounted in the Integrate bring-up tab next to `UsbDebugMockPanel` and `ButtonBoardPanel`.
- This doc.

### Validation harness

A new scenario `tof-calibration-persistence-pass` is recommended but not yet authored — tracked as a deferred next-step. The manual verification performed 2026-04-15 on live bench confirmed end-to-end persistence across a reboot (`medium/100/8×8/-15/xtalk-on` → set `short/33/12×12/center=145/+25/xtalk-off` → reboot via RTS → post-reboot snapshot matches the new values exactly).

## Fault trigger diagnostics (2026-04-15 later)

### Motivation

Operators reported spurious `LD_OVERTEMP` latches on cold boots and during SBDN transitions, with no visibility into the ADC reading, the active threshold, or the gate state. Two changes were introduced:

1. **Settle gate** — `LD_OVERTEMP` detection now requires both `ld_rail_pgood` and `sbdn_state != OFF` to be stable for ≥ 2000 ms, measured from the later of the two anchors. See `LASER_CONTROLLER_LD_TEMP_SETTLE_MS` in `components/laser_controller/src/laser_controller_safety.c`. This eliminates first-sample ADC garbage on rail rise and tri-state SBDN transitions.
2. **Trigger diagnostic frame** — when the overtemp check actually trips, the firmware now captures the measured temperature, the raw ADC voltage, the active limit, both settle timers, and a pre-formatted `expr` string. The frame is frozen at the rising edge of the fault so the captured values reflect trip time, not a rolling reading that drifts while the fault stays latched.

### JSON schema

Extends the `fault` block in `status.get` / live telemetry:

```json
"fault": {
  "latched": true,
  "activeCode": "ld_overtemp",
  "activeClass": "system_major",
  "latchedCode": "ld_overtemp",
  "latchedClass": "system_major",
  "triggerDiag": {
    "code": "ld_overtemp",
    "measuredC": 68.3,
    "measuredVoltageV": 1.3776,
    "limitC": 55.0,
    "ldPgoodForMs": 4200,
    "sbdnNotOffForMs": 3800,
    "expr": "ld_temp_c > 55.0 C @ 68.3 C, 1.378 V"
  },
  "activeCount": 1,
  "tripCounter": 1,
  "lastFaultAtIso": "..."
}
```

- `triggerDiag` is `null` when no diag frame is captured.
- The shape is general so future faults (loop-bad, rail-bad, unexpected-current) can reuse it without a schema migration. The `code` field inside the diag identifies which fault captured the frame.
- `ldPgoodForMs` and `sbdnNotOffForMs` are the gate timers observed at the exact tick the fault tripped. They distinguish a real trip (gates past 2 s) from a hypothetical close-to-gate-edge trip.

### Firmware contract

- Capture site: `laser_controller_safety.c` populates the decision-side diag fields inside the gated overtemp check. `laser_controller_app.c` copies the decision into `context->active_fault_diag` on the rising edge of the fault (gated on `!context->active_fault_diag.valid`). Subsequent ticks do not overwrite.
- Clear site: `laser_controller_app_clear_fault_latch` resets the diag to `valid=false` under `s_runtime_status_lock`.
- Publish site: `laser_controller_publish_runtime_status` mirrors `context->active_fault_diag` into `s_runtime_status.active_fault_diag`. `laser_controller_comms_write_fault_json` emits the `triggerDiag` sub-block (or `null`) in the `fault` JSON payload.

### Four-place sync targets

- Firmware decision struct: `laser_controller_safety_decision_t` fields `ld_overtemp_*` in `components/laser_controller/include/laser_controller_safety.h`.
- Firmware snapshot: `laser_controller_safety_snapshot_t.ld_rail_pgood_for_ms` / `.sbdn_not_off_for_ms` in the same header, populated by `laser_controller_app.c::run_fast_cycle`.
- Firmware runtime status: `laser_controller_runtime_status_t.active_fault_diag` in `laser_controller_app.h`.
- Firmware comms: `laser_controller_comms_write_fault_json` in `laser_controller_comms.c`.
- Host types: `FaultTriggerDiag` + `FaultSummary.triggerDiag` in `host-console/src/types.ts`. Also added to `RealtimeFaultSummary`.
- Host defaults: `triggerDiag: null` in `host-console/src/hooks/use-device-session.ts` default snapshot.
- Host merge: `host-console/src/lib/live-telemetry.ts` passes the field through both the extractor and the realtime→snapshot merger.
- Host mock: `host-console/src/lib/mock-transport.ts` synthesizes a representative diag when `activeFaultCode === 'ld_overtemp' && faultLatched`.
- Host UI: `InspectorRail.tsx` renders a "Trip cause" sub-card inside the Fault summary block; `OperateConsole.tsx` appends the `expr` + timers to the in-page fault banner.
- This doc.

### Validation harness

No new validation scenario has been authored yet. Recommended: an `ld-overtemp-gated-pass` scenario that (a) asserts no `LD_OVERTEMP` trip during the first 2 s after rail rise even with a forced high ADC reading, (b) asserts a real trip does fire after the settle window with a valid `triggerDiag` block, and (c) asserts `clear_faults` drops the diag. Tracked as a deferred next-step. Manual bench validation is BLOCKED until a bench is available.
