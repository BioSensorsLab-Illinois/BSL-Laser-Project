# Deployment + Button + NIR Pipeline

This document captures the end-to-end pipeline for the BSL host-controlled deployment cycle and the button-driven NIR firing path. It is the single source of truth for "why does/doesn't this fire" debugging. If you change any of these gates, update this doc in the same commit.

Last refresh: 2026-04-16 (after the post-deployment refactor / lockout fix).

---

## 1. Tick flow

The control task runs `laser_controller_run_fast_cycle` every 5 ms, priority 20. One pass through:

```
read_inputs              → context->last_inputs (buttons via MCP23017,
                            tof, imu, rails, ADCs, …)
classify_power_tier      → context->power_tier (PROGRAMMING_ONLY / 
                            INSUFFICIENT / REDUCED / FULL)
usb_debug_mock_tick      → optionally synthesizes rail PGOOD + telemetry
deployment_tick          → context->deployment.{active, ready, ready_idle,
                            running, failed, …}
build safety snapshot    → safety_snapshot_t (uses prev-tick lockout +
                            current-tick deployment / power / rails)
safety_evaluate          → decision.{request_nir, allow_nir,
                            nir_output_enable, sbdn_state, fault_*}
hold filters             → lambda_drift, tec_temp_adc auto-clear latching
SYSTEM_MAJOR overrides   → forces sbdn_state=OFF, nir_output_enable=false
apply_button_board_policy → context->button_nir_lockout, LED brightness,
                            RGB color, nir_button_block_reason string
derive_outputs           → outputs.{sbdn_state, select_driver_low_current,
                            enable_alignment_laser, …}
apply_outputs            → board GPIO drives (SBDN, PCN, GN_LD_EN, …)
publish_runtime_status   → mirrors context state to s_runtime_status for
                            comms task to read
```

Important ordering invariants:
- `apply_button_board_policy` runs **after** `safety_evaluate`. The lockout state it computes is for the **next** tick. The current tick's safety decision uses the **previous** tick's lockout.
- The SBDN_OFF override on SYSTEM_MAJOR runs before `apply_button_board_policy`, so the RGB priority logic sees the final fault state.
- `derive_outputs` runs after the policy. Mostly because the RGB target is part of the outputs struct and needs to be set before `apply_outputs` writes the I²C frame.

## 2. The NIR enable gauntlet

For PCN to go HIGH (NIR fire) on a button press, **all** of these must be true on a single tick:

| # | Field | Source | Failure mode |
|---|---|---|---|
| 1 | `deployment.active` | operator: `enter_deployment_mode` | Need to enter deployment mode |
| 2 | `deployment.ready` | checklist: all 11 steps `passed` | Run checklist; fix the failed step |
| 3 | `deployment.ready_idle` | post-checklist idle posture | Re-run checklist |
| 4 | `!deployment.running` | checklist not currently advancing | Wait for checklist to finish |
| 5 | `power_tier == FULL` | PD negotiation | Use a full-power source (≥35.1 W) |
| 6 | `!decision.fault_present` (this tick) | safety evaluator | Fix the fault condition |
| 7 | `hw->ld_rail_pgood` | board ADC + I/O | LD rail not coming up |
| 8 | `hw->tec_rail_pgood && tec_temp_good` (when `require_tec_for_nir`) | board ADC + I/O | TEC rail / temp not settled |
| 9 | `button.board_reachable` | MCP23017 I²C | Connect / fix button board |
| 10 | `button.stage1_pressed && button.stage2_pressed` | MCP23017 GPA | Press both stages |
| 11 | `!context->button_nir_lockout` | press-and-hold lockout (see §4) | Release both stages then re-press, after fixing the underlying SAFETY/SYSTEM_MAJOR fault |
| 12 | `!service_mode_requested && !service_mode_active` | service module | Exit service mode |
| 13 | host path also requires `runtime_mode == MODULATED_HOST` | bench | Toggle to host control if not using buttons |

**Diagnostic**: the firmware computes `button_runtime.nir_block_reason` every tick describing which gate is currently failing. Surfaced in the GUI Operate-page trigger card.

## 3. SBDN policy

Three states (`laser_controller_sbdn_state_t`):

| State | GPIO13 | When |
|---|---|---|
| `ON` | drive HIGH | `deployment.active && deployment.ready_idle && !decision.fault_present` |
| `STANDBY` | input / Hi-Z (≈2.25 V via R27/R28) | Outside deployment, or pre-boot (no power) |
| `OFF` | drive LOW (fast 20 µs shutdown) | Any SAFETY_LATCHED / SYSTEM_MAJOR fault, service mode, fault_latched, lambda_drift, tec_temp_adc_high |

Per the 2026-04-17 user directive: SBDN stays HIGH throughout deployment (driver always armed). PCN selects LISL (low current, idle bias) vs LISH (high current, NIR fire). NIR firing flips PCN HIGH; no SBDN re-arming penalty between fires.

## 4. Press-and-hold lockout (revised 2026-04-16)

Latched in `apply_button_board_policy`:

```c
const bool fault_locks_trigger =
    decision->fault_present &&
    decision->fault_class != LASER_CONTROLLER_FAULT_CLASS_INTERLOCK_AUTO_CLEAR;

if (fault_locks_trigger && (stage1_pressed || stage2_pressed)) {
    context->button_nir_lockout = true;
}
if (!stage1_pressed && !stage2_pressed) {
    context->button_nir_lockout = false;
}
```

Latches **only** for `SAFETY_LATCHED` / `SYSTEM_MAJOR` faults (the ones that require explicit `clear_faults`). `INTERLOCK_AUTO_CLEAR` faults — `tof_out_of_range`, `tof_invalid`, `imu_*`, `lambda_drift`, `tec_temp_adc_high`, `ld_loop_bad` — block NIR for the tick(s) they fire (via `allow_nir = false`) but do NOT latch the lockout. The same in-progress press fires NIR the moment the underlying condition resolves.

This was the root cause of "stage 2 doesn't fire NIR on the bench" — the ToF sensor at <200 mm fired `tof_out_of_range` every tick, latching the lockout permanently for that press cycle.

Cleared on full release (both stages up).

## 5. Fault classes (`laser_controller_fault_class_t`)

| Class | Latches `fault_latched`? | Latches lockout? | Example faults |
|---|---|---|---|
| `INTERLOCK_AUTO_CLEAR` | No | No | tof_*, imu_*, lambda_drift, tec_temp_adc_high, ld_loop_bad, horizon_crossed |
| `SAFETY_LATCHED` | Yes | Yes | illegal_button_state, unexpected_state |
| `SYSTEM_MAJOR` | Yes | Yes | invalid_config, comms_timeout, watchdog, brownout, pd_lost, ld_overtemp, *_rail_bad, button_board_i2c_lost, usb_debug_mock_pd_conflict |

`SYSTEM_MAJOR` additionally forces SBDN_OFF and rails-down via the override block in `run_fast_cycle`. `SAFETY_LATCHED` blocks NIR until cleared. `INTERLOCK_AUTO_CLEAR` blocks NIR for the firing tick only.

## 6. RGB status LED priority

Computed in `apply_button_board_policy` after the safety decision is final:

| Priority | Color | Condition |
|---|---|---|
| 1 | (test override) | `rgb_test_until_ms` window from `integrate.rgb_led.set` |
| 2 | Solid yellow `(255, 200, 0)` | Service mode active |
| 3 | Flashing red `(255, 0, 0)` | Unrecoverable: SYSTEM_MAJOR latched or present |
| 4 | Flashing orange `(255, 140, 0)` | Recoverable: any fault latched, fault present this tick, or button lockout |
| 5 | Off | Pre-deployment / pre-ready |
| 6 | Solid red `(255, 0, 0)` | NIR currently emitting |
| 7 | Solid green `(0, 255, 0)` | Stage 1 held + LD_GOOD + TEC_TEMP_GOOD ("armed") |
| 8 | Solid blue `(0, 0, 255)` | Default ready/awaiting trigger |

Per-channel scaling in `rgb_led.c apply` (red 100%, green 45%, blue 35%) compensates for the unequal native efficiency of the TLC59116-driven RGB die.

## 7. ToF front LED ownership (GPIO6)

Highest priority wins:

| Owner | Brightness source | Condition |
|---|---|---|
| Service | bringup `tof_illumination_enabled` + `_duty_cycle_pct` | `service_mode_requested` |
| Deployment-armed | `context->button_led_brightness_pct` | `deployment.active && deployment_led_armed` (sticky once first ready_idle) |
| Bench | `bench.illumination_enabled` + `illumination_duty_cycle_pct` | otherwise |

`deployment_led_armed` is a sticky flag set on first transition to ready_idle, cleared **only** on `exit_deployment_mode`. The LED stays on at `button_led_brightness_pct` through any fault / interlock / lockout for the duration of deployment.

`button_led_brightness_pct` is set from three sources:
- One-time default 20% when `button_led_initialized` is first true (first tick of ready_idle).
- Side buttons: side1 = +5%, side2 = -5% (clamped 0..100).
- GUI Operate slider via `app_set_button_led_brightness()`. Clamped additionally to `max_tof_led_duty_cycle_pct` (default 50%).

Stage 1 / Stage 2 do NOT modify brightness. Earlier code reset to 20% on stage1_edge — removed 2026-04-17.

## 8. Deployment checklist (in order)

1. `ownership_reclaim` — clear bring-up overrides, force safe-off
2. `pd_inspect` — verify negotiated PD ≥ 9 V
3. `power_cap` — derive runtime current cap from PD budget
4. `outputs_off` — confirm all controlled outputs are off
5. `stabilize_3v3` — settle delay
6. `dac_safe_zero` — DAC init + LD/TEC channels to safe 0 V
7. `peripherals_verify` — IMU / ToF / haptic readback
8. `rail_sequence` — TEC then LD enable in order
9. `tec_settle` — hold target until TEC good + analog plausible
10. `lp_good_check` — drive PCN high briefly, verify LPGD ≤ 1 s
11. `ready_posture` — return PCN low (LISL idle), declare ready_idle

Failure at any step records `deployment.failure_code` and stops the sequence. Operator runs the checklist again.

## 9. Diagnostic hooks

- `bench.hostControlReadiness.nirBlockedReason` — host-path reason (operates `operate.set_output`).
- `buttonBoard.nirButtonBlockReason` — button-path reason (this doc, §2). Surfaced in the Operate trigger card.
- `fault.activeReason` / `fault.latchedReason` — firmware-supplied detail string for the current/latched fault.
- `fault.triggerDiag` — frozen-at-trip ADC values for LD_OVERTEMP (other faults could populate this in the future via the same struct).
- `deployment.readyTruth.*` — live raw signals (rail PGOOD, SBDN observed, PCN observed, idle bias current) for verifying deployment readiness against hardware.
- `deployment.secondaryEffects` — list of side-effects observed during deployment (e.g. TEC contradiction).
- `deployment.primaryFailureReason` — operator-readable reason a checklist step failed.

## 10. Where each rule lives

| Concern | File / function |
|---|---|
| Button hardware read | `laser_controller_buttons.c laser_controller_buttons_refresh` |
| Button policy / lockout / RGB / block reason | `laser_controller_app.c apply_button_board_policy` |
| NIR enable gauntlet | `laser_controller_safety.c laser_controller_safety_evaluate` |
| SBDN state mapping | `laser_controller_safety.c` (lines 479-500) + run_fast_cycle SYSTEM_MAJOR override |
| GPIO drive (SBDN/PCN/Green) | `laser_controller_board.c laser_controller_board_apply_outputs` |
| Deployment state machine | `laser_controller_deployment.c laser_controller_deployment_tick` |
| ToF front LED ownership | `laser_controller_app.c run_fast_cycle` (post-apply_outputs block) |
| LD LIO calibration offset | `laser_controller_board.c` LIO ADC read path; `safety.lio_voltage_offset_v` config field |
| Fault class semantics | `laser_controller_app.c laser_controller_record_fault` |
| Block-reason JSON emit | `laser_controller_comms.c laser_controller_comms_write_button_board_json` |
| Block-reason GUI render | `host-console/src/components/OperateConsole.tsx TriggerControlCard` |
