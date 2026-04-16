# Validation Plan

## Positioning

This is the minimum validation structure needed before any real optical output testing.

The current repository does not include measured bench data. The “measured result” fields below are placeholders that must be filled from instrumented bench runs.

The active powered-ready rewrite is tracked in `.agent/runs/powered-ready-console/`. Validation for that initiative uses the new command family and requires a real powered-ready deployment bench; a USB-only safe-fail result is not enough.

The repo validation harness is `host-console/scripts/live_controller_validation.py`. It now supports both:

- `--transport serial --port /dev/...`
- `--transport ws --ws-url ws://192.168.4.1/ws`

The required powered-bench validation passes are:

1. `aux-control-pass`
2. `ready-runtime-pass`
3. `fault-edge-pass`

Those three passes are the minimum automated gate for the powered-ready recovery path.

## Bench Rules

1. First power-up must be with the NIR path physically disconnected or optically terminated.
2. Prefer an electrical dummy load before any live diode testing.
3. Use a logic analyzer or oscilloscope on:
   - `SBDN`
   - LD VIN enable
   - TEC VIN enable
   - a firmware timing marker GPIO
4. Use controlled mock inputs before trusting live sensors.
5. Never combine new firmware, new hardware, and live optical output in the same first test.

## Bring-Up Checklist

| Item | Expected Result | Status |
| --- | --- | --- |
| Boot with no PD source | MCU up, no LD VIN, no TEC VIN, no beam | pending |
| Boot with 5 V host only | `PROGRAMMING_ONLY`, no beam | pending |
| Boot with valid >35 W source | no beam until config + sensors + TEC readiness pass, then powered-ready deployment may complete | pending |
| Invalid config on boot | `FAULT_LATCHED`, no beam | implemented in scaffold |
| Mock IMU stale | fault logged, no beam | pending bench |
| Mock ToF stale | fault logged, no beam | pending bench |
| Mock above-horizon | beam permission denied immediately | pending bench |
| Mock out-of-range distance | beam permission denied immediately | pending bench |
| Unexpected current while NIR not requested | major fault, rails disabled | pending bench |

## Fault Matrix

| Stimulus | Expected Fault Class | Expected Immediate Action | Expected Recovery |
| --- | --- | --- | --- |
| Stage 2 released | auto-clear interlock behavior | NIR off immediately | stage 1 may restore green if safe |
| Horizon crossed | auto-clear interlock | NIR off immediately | clear only after hysteresis re-entry |
| Distance <20 cm or >1 m | auto-clear interlock | NIR off immediately | clear only after hysteresis re-entry |
| IMU stale | safety-latched | beam off | explicit clear after sensor healthy |
| ToF stale | safety-latched | beam off | explicit clear after sensor healthy |
| Invalid calibration | major system | beam off, rails off | provision valid config |
| PD lost during NIR | major system | beam off, rails off | re-negotiate + re-arm |
| Rail enable with no PGOOD | major system or latched, policy TBD during hardware bring-up | beam off | investigate hardware fault |
| Driver loop bad | safety-latched | beam off | explicit clear after diagnosis |
| Brownout | major system | beam off, rails off | reboot + revalidation |
| Watchdog reset | major system | beam off, rails off | reboot + root-cause analysis |

## Required Validation Cases

| Case | Pass Criteria | Measured Result |
| --- | --- | --- |
| Boot with no PD source | all outputs safe within first control cycle | TBD |
| Boot with 5 V host only | `PROGRAMMING_ONLY`, no rails enabled | TBD |
| Boot with valid high-power PD source | remains non-emitting until all interlocks satisfied | TBD |
| PD drop during idle | no beam, safe state transition logged | TBD |
| PD drop during NIR active | `SBDN` asserted first, rails disabled after | TBD |
| IMU stale while idle | latched fault, no arm | TBD |
| IMU stale during NIR active | NIR disabled immediately, fault latched | TBD |
| Above-horizon during NIR active | NIR disabled immediately | TBD |
| ToF invalid during NIR active | NIR disabled immediately, fault latched | TBD |
| Distance too near / too far during NIR active | NIR disabled immediately | TBD |
| LD overtemp | major fault, rails disabled | TBD |
| TEC no-settle timeout | no `READY_NIR`, fault or timeout logged | TBD |
| Deployment ready posture mismatch | deployment terminal state must fail, not report ready | TBD |
| Ready-idle low-current bias below 0.2 A | no `unexpected_current` fault; ready-idle remains valid | TBD |
| Integrate safety apply + reboot | thresholds persist across reboot and deployment uses them automatically | TBD |
| TEC loss after ready | ready clears immediately, LD rail drops safe, failure recorded | TBD |
| Contradictory TEC telemetry (`PWR_TEC_GOOD` low while `TEMPGD` stays high and analog telemetry looks plausible) | record contradiction or secondary effect; do not report terminal `tec_rail_bad` from a single contradictory sample | TBD |
| Deployment entry GPIO6 | GPIO6 stays low until the operator explicitly re-enables the LED | TBD |
| Passive PD only during deployment | deployment may consume passive PD reads, but must not trigger explicit refresh/write ownership (`pd.source` must never flip to `integrate_refresh` or `boot_reconcile` while deployment is active) | TBD |
| Rail enable asserted but no PGOOD | no beam, fault logged | TBD |
| Current mismatch | no beam or fault per final policy | TBD |
| Button stage transitions | exact state transitions with no race leakage | TBD |
| Watchdog recovery | safe boot, no auto-rearm | TBD |
| Brownout recovery | safe boot, no auto-rearm | TBD |
| NVS corruption recovery | invalid config fault, no beam | TBD |

## Timing Measurements To Capture

| Measurement | Method | Target | Result |
| --- | --- | --- | --- |
| Interlock violation to `SBDN` safe command | GPIO marker + logic analyzer | as low as achievable, documented | TBD |
| Interlock violation to software state update | timestamped event log | documented | TBD |
| Rail enable to PGOOD | oscilloscope / GPIO trace | documented per rail | TBD |
| TEC command to ready | event timestamps | documented per setpoint | TBD |
| Stage-2 press to NIR permission grant | GPIO marker + event log | documented | TBD |
| Stage-2 release to beam disable | GPIO marker + logic analyzer | immediate | TBD |

## Manufacturing / Service Hooks Needed

- mock-input injection for dry-run interlock testing
- command to dump config and CRC
- command to write calibration in protected mode
- command to read fault counters and last-fault record
- explicit service-mode entry with logging and timeout

## Exit Criteria Before Any Clinical Claims

1. All real device drivers implemented and reviewed.
2. All required validation cases executed on hardware.
3. Reaction-time data measured and recorded.
4. Calibration write/read/CRC path verified.
5. Fault recovery policy reviewed and documented.
6. Independent safety-path audit completed.

## Button Board Validation (added 2026-04-15)

Three new scenarios to be added to `host-console/scripts/live_controller_validation.py` and exercised on the live bench once the button board is mounted. Treat these as required-before-clinical for the binary-trigger path.

- **`button-trigger-pass`** — Confirm `MCP23017` probes at `0x20`, `INTA` fires on physical press of each of the four buttons (verified against `buttons.isrFireCount` and the four `*Pressed` fields), `operate.set_mode binary_trigger` is accepted, stage1 enables green + the front LED at 20 %, side1/side2 step the LED in 10 % increments, stage1+stage2 fires NIR, an interlock during NIR latches `buttonBoard.triggerLockout`, and lockout clears when both stages are released and not before.
- **`rgb-led-pass`** — Service mode + no deployment. Cycle through `integrate.rgb_led.set` with each of: solid red, solid green, solid blue, flashing red, flashing orange. Visual confirm each color and that the flash period is ~1 s with ~50 % duty. Confirm `integrate.rgb_led.clear` reverts to firmware-computed state.
- **`sbdn-lp-good-pass`** — Force the new `LD_LP_GOOD_TIMEOUT` path by physically opening LD_LPGD before deployment, then run the deployment checklist. Confirm the new `lp_good_check` step fails after ~1 s, that the primary failure code is `ld_lp_good_timeout`, that the fault is `system_major`-classed, and that the RGB status LED switches to flash-red.

The mandatory three (`aux-control-pass`, `ready-runtime-pass`, `fault-edge-pass`) are unchanged and must be re-run after the firmware image lands on the bench. None of these scenarios are exercised against the mock — they require real bench power.
