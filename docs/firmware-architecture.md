# Firmware Architecture

## Scope

This document defines the intended safety architecture for the ESP32-S3 handheld 785 nm surgical laser controller firmware.

The current repository implements the architecture skeleton only. Real silicon drivers and calibration persistence are still pending.

## Layering

### 1. Board Layer

Files:

- [laser_controller_board.h](/Users/zz4/BSL/BSL-Laser/components/laser_controller/include/laser_controller_board.h)
- [laser_controller_board.c](/Users/zz4/BSL/BSL-Laser/components/laser_controller/src/laser_controller_board.c)

Responsibilities:

- own GPIO polarity and reset-state truth
- own the physical read/write boundary
- expose rail PGOOD and raw telemetries as explicit fields
- never make policy decisions

### 2. Config Layer

Files:

- [laser_controller_config.h](/Users/zz4/BSL/BSL-Laser/components/laser_controller/include/laser_controller_config.h)
- [laser_controller_config.c](/Users/zz4/BSL/BSL-Laser/components/laser_controller/src/laser_controller_config.c)

Responsibilities:

- define versioned persistent configuration
- validate safety thresholds and calibration presence
- reject invalid or partial manufacturing data

### 3. Safety Layer

Files:

- [laser_controller_safety.h](/Users/zz4/BSL/BSL-Laser/components/laser_controller/include/laser_controller_safety.h)
- [laser_controller_safety.c](/Users/zz4/BSL/BSL-Laser/components/laser_controller/src/laser_controller_safety.c)

Responsibilities:

- own the beam permission truth table
- evaluate interlocks
- classify newly observed faults
- decide whether alignment or NIR is allowed

### 4. App / Supervision Layer

Files:

- [laser_controller_app.h](/Users/zz4/BSL/BSL-Laser/components/laser_controller/include/laser_controller_app.h)
- [laser_controller_app.c](/Users/zz4/BSL/BSL-Laser/components/laser_controller/src/laser_controller_app.c)

Responsibilities:

- run the deterministic control loop
- classify USB-PD power tier
- latch non-auto-clear faults
- derive output enables from safety decisions
- manage explicit state transitions
- emit logs

## Deterministic Scheduling

Current control-loop design:

- 5 ms fast cycle
  - read inputs
  - classify power tier
  - run safety evaluation
  - latch fault if needed
  - derive outputs
  - apply outputs
  - update top-level state
- 50 ms slow cycle
  - periodic summary logging today
  - future home for telemetry aggregation, comms publication, and slower supervision

Why this shape:

- beam-permission logic runs on a fixed cadence
- the fast path is explicit in one place
- slower work is separated structurally even before it becomes a separate task

## Top-Level State Meanings

| State | Meaning | Output Expectation |
| --- | --- | --- |
| `BOOT_INIT` | startup and safe GPIO assertion | all outputs safe |
| `PROGRAMMING_ONLY` | 5 V host or inadequate PD power | no LD, no TEC, no beam |
| `SAFE_IDLE` | powered but not ready for operation | no beam |
| `POWER_NEGOTIATION` | waiting for stable PD information | no LD, no TEC, no beam |
| `LIMITED_POWER_IDLE` | reduced-power tier, alignment-only policy | no NIR |
| `TEC_WARMUP` | TEC rail commanded, waiting for rail-good | no NIR |
| `TEC_SETTLING` | TEC running, target not yet settled | no NIR |
| `READY_ALIGNMENT` | alignment can be enabled if stage 1 is pressed | green allowed only if safe |
| `READY_NIR` | all conditions except operator stage-2 request are satisfied | NIR can arm |
| `ALIGNMENT_ACTIVE` | stage 1 active, green on, NIR off | green only |
| `NIR_ACTIVE` | stage 2 active and fully permitted | green off, NIR on |
| `FAULT_LATCHED` | latched safety or system fault | all emission paths off |
| `SERVICE_MODE` | protected bring-up and bench-debug mode | derived outputs forced safe; service-only staging and diagnostics allowed |

## Interlock Truth Table

| Condition | Alignment Requires It | NIR Requires It | Current Policy |
| --- | --- | --- | --- |
| Boot complete | yes | yes | enforced |
| Valid config and calibration | yes | yes | enforced |
| No latched fault | yes | yes | enforced |
| PD tier operational | yes | yes | enforced |
| Full-power PD tier | no | yes | enforced |
| IMU valid and fresh | yes | yes | enforced |
| Beam below horizon threshold | yes | yes | enforced |
| ToF valid and fresh | yes | yes | enforced |
| ToF distance in safe window | yes | yes | enforced |
| TEC rail good | no | yes if TEC required | enforced |
| TEC settled / temp-good | no | yes if TEC required | enforced |
| LD rail good | no | yes | enforced |
| Driver loop good | no | yes | enforced |
| LD overtemp absent | yes | yes | enforced |
| Unexpected current absent | yes | yes | enforced |
| Stage 1 pressed | yes | yes | enforced |
| Stage 2 pressed | no | yes | enforced |
| Green output off | no | yes | enforced by derived outputs |

## Fault Classes

### Auto-Clear Interlock Faults

Examples:

- horizon crossed
- distance outside safe range

Behavior:

- force beam off immediately
- do not clear output until the safe band is re-entered with hysteresis
- do not auto-promote to normal operation without a fresh full evaluation

### Safety-Latched Faults

Examples:

- stale IMU
- stale ToF
- illegal button sequence
- driver loop bad
- unexpected state transition

Behavior:

- force beam off immediately
- latch fault in supervisor state
- require explicit recovery policy later

### Major System Faults

Examples:

- invalid configuration
- brownout
- watchdog
- comms heartbeat failure
- unexpected current while NIR is not requested

Behavior:

- force beam off immediately
- disable high-power rails
- latch fault

## Driver Bring-Up Table

| IC | Safe Init Requirement | Must Be Verified Before Clinical Use |
| --- | --- | --- |
| ATLS6A214 | `SBDN` asserted, `PCN` low/current-limited, LD DAC command = 0 A, LD VIN disabled until PD + config + TEC readiness permit | loop-good semantics, current monitor calibration, standby latency |
| TEC14M5V3R5AS | TEC VIN disabled at boot, TMS command clamped to safe placeholder, no settling claim until telemetry proves it | target mapping, settle timing, fault outputs, thermal runaways |
| DAC80502 | both channels initialize to zero-safe codes before rail enables | no startup glitch, channel ownership, code-to-voltage calibration |
| LSM6DSO | SPI mode only, block data update, timestamps, explicit stale timeout | frame alignment to beam axis, horizon latency, plausibility filters |
| STUSB4500 | read negotiated contract, classify tier conservatively | exact PDO decoding, host-only detection, contract-change timing |
| ToF sensor | invalid/saturated/stale states explicit, no “best guess” range | distance update rate, timeout budget, sunlight/target edge cases |
| DRV2605 | start in standby; no haptic critical dependency | deterministic pattern timing only |

## Calibration Structure

The config blob defined in [laser_controller_config.h](/Users/zz4/BSL/BSL-Laser/components/laser_controller/include/laser_controller_config.h) already reserves space for:

- board hardware revision
- serial number
- power thresholds
- safety thresholds
- stale-data and settling timeouts
- IMU-to-beam transform matrix
- wavelength-to-temperature LUT
- analog scaling coefficients
- service flags
- CRC field

Current repo behavior:

- invalid or missing wavelength calibration makes config validation fail
- that failure latches a major fault at startup
- no beam path can be enabled until a valid config is provisioned

## Known Gaps

- No real NVS backend exists yet.
- No explicit fault-counter array exists yet.
- No persistent last-fault record exists yet.
- No current-command path or DAC write API exists yet.
- No measured reaction-time results exist yet.

Those gaps must close before the firmware can claim “production quality” beyond architecture.
