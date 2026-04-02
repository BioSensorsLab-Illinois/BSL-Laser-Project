# Protocol Specification

## Goal

Define one command and telemetry model that can be carried over:

- USB CDC for development
- UART for bench debug
- the current desktop GUI
- Wi-Fi SoftAP + WebSocket without changing semantics

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
| `get_status` | full machine-readable status snapshot | safe read-only |
| `get_faults` | active and historical faults | safe read-only |
| `get_bringup_profile` | fetch service-only module expectations and tuning state | safe read-only |
| `clear_faults` | request fault clear | service-only; must not clear if recovery criteria fail |
| `set_laser_power` | stage the bench high-state NIR current request | service-only; host request only, not direct output authority |
| `set_max_current` | alias for bench high-state NIR current staging | service-only; must never exceed provisioned safety ceiling |
| `set_runtime_safety` | update runtime safety thresholds, hysteresis, and hold windows | service-only; reject invalid policy values and keep firmware authoritative |
| `pd_debug_config` | apply STUSB4500 runtime sink PDOs and update firmware power-tier thresholds | service-only; runtime-register write only, not a hidden NVM reprogramming path |
| `pd_save_firmware_plan` | validate runtime PDO readback, then save the plan into ESP32 firmware NVS for future auto-reconcile | service-only; MCU-owned persistence, separate from STUSB4500 NVM |
| `pd_burn_nvm` | validate runtime PDOs, then burn the requested STUSB4500 PDO startup defaults into NVM and verify raw NVM readback | service-only; manufacturing-only action with finite endurance, never for iterative tuning |
| `set_target_temp` | stage TEC target temperature | service-only; clamp to safe calibrated range |
| `set_target_lambda` | stage wavelength target | service-only; reject out-of-range or uncalibrated values |
| `configure_modulation` | stage PCN high/low current modulation request | service-only; host request only, not direct PWM authority |
| `laser_output_enable` | request bench NIR output intent | service-only; still routed through interlocks and may remain off |
| `laser_output_disable` | clear bench NIR output intent | safe; immediately drops host NIR request |
| `enable_alignment` | request stage-1 style alignment intent | service-only; still routed through interlocks |
| `disable_alignment` | clear alignment intent | safe |
| `reboot` | controlled reboot | safe if outputs dropped first |
| `enter_service_mode` | protected service path | must be explicitly protected and logged |
| `exit_service_mode` | leave service mode | safe |
| `apply_bringup_preset` | stage a known module-population profile such as `soc_imu_dac` | service-only; does not grant beam permission |
| `set_profile_name` | rename the active bring-up profile | safe bring-up metadata; does not require service mode |
| `set_module_state` | declare whether a module is expected present and debug-enabled | safe bring-up metadata; does not require service mode |
| `save_bringup_profile` | request device-side persistence of the bring-up profile | safe bring-up metadata write; may be unavailable in early firmware |
| `scan_wireless_networks` | ask the controller to scan nearby Wi-Fi SSIDs for station-mode setup | read-only transport diagnostic; may briefly pause station reconnect attempts while the scan runs |
| `configure_wireless` | switch the controller between bench SoftAP and existing-Wi-Fi station mode | transport-management write; may intentionally drop the current wireless socket while the controller changes networks |
| `set_supply_enable` | request the LD or TEC MPM3530 VIN rail on/off during service bring-up | service-only; beam outputs stay forced safe |
| `refresh_pd_status` | force an immediate STUSB4500 contract/PDO refresh | read-only diagnostic; does not require service mode |
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
- `configure_wireless` accepts `mode:"softap"` or `mode:"station"`. In station mode the host may also provide `ssid` and `password`; if the password is omitted, the controller reuses the saved credential.
- `i2c_scan`, `i2c_read`, `spi_read`, and `refresh_pd_status` are intentionally allowed outside service mode because they are read-only probes.
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
- `pd_burn_nvm` is intentionally a separate command from runtime PDO apply so the host can warn about finite NVM endurance and final-provisioning intent.
- `pd_burn_nvm` first applies the requested runtime PDOs, refreshes live STUSB4500 readback, aborts if runtime verification fails, then writes the STUSB4500 raw NVM banks and compares raw NVM readback before reporting success.
- The current implementation preserves non-PDO NVM bytes by reading the full 5-bank map first and only patching the PDO-related fields before writing the full image back.

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
    "lowStateCurrentA": 0.0
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

- `live_telemetry`: the lightweight high-rate stream for UI motion and fast numeric readback. It carries the current `session`, `pd`, `rails`, `imu`, `tof`, `laser`, `tec`, `safety`, `buttons`, compact `bringup` service state, and `fault`.
- `status_snapshot`: the slower richer snapshot for periodic reconciliation. It still carries identity, GPIO inspector state, haptic peripheral readback, and the compact bring-up status block.

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
    "fault": { "latched": false, "activeCode": "none", "activeCount": 0, "tripCounter": 0 }
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
