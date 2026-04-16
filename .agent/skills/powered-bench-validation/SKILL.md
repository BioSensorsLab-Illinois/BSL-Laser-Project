---
name: powered-bench-validation
description: Codifies the mandatory Powered Phase 2 validation passes for the laser controller. Distinguishes USB Phase 1 (bench-safe, powered-rail claims forbidden) from Powered Phase 2 (TEC/LD rails actually on, real readiness verified). Trigger on "validate", "powered-ready validation", "run bench scenarios", "is this ready", or any claim that deployment/runtime readiness has been proven. Three scenarios are mandatory for powered-ready claims.
---

# powered-bench-validation

Validation authority for this repo. Read before claiming any powered-ready milestone is complete.

## Phase Separation Is Non-Negotiable

Per `.agent/AGENT.md` and `docs/validation-plan.md`:

- **USB Phase 1** = the board is on USB power only, laser rails and TEC rails are OFF or in safe-fail. Protocol, deployment-lockout, runtime-gating, and parser tests are meaningful here. Powered-rail claims are not.
- **Powered Phase 2** = a real powered bench with TEC rail enabled, LD rail enabled, `PGOOD` asserted, `SBDN` driven, and readback instrumentation. Only results from this phase count as evidence that the controller is powered-ready.

**A USB Phase 1 `pass` is never sufficient to close a powered-ready milestone.** If the session does not have a powered bench available, the milestone stays `In Progress` and the gap is recorded as an explicit blocker in `Status.md`.

## Validation Harness

All scenarios run through:

    host-console/scripts/live_controller_validation.py

It supports two transports:

    --transport serial --port /dev/cu.usbmodem201101
    --transport ws --ws-url ws://192.168.4.1/ws

## The 11 Available Scenarios

Scenario names come from `live_controller_validation.py` (see function index there):

### USB Phase 1 (safe on USB-only)

| Scenario | What it proves |
|---|---|
| `parser-matrix` | JSON parser accepts all expected command shapes and rejects malformed input |
| `deployment-lockout` | Deployment mode correctly locks out service/bringup writes |
| `deployment-usb-safe-fail` | Deployment on USB-only fails cleanly and leaves rails OFF — cannot be confused with a powered pass |
| `runtime-mode-gating` | `binary_trigger` rejects host output commands; `modulated_host` accepts them |
| `safety-persistence-reboot` | Integrate safety policy writes to NVS and is applied automatically on reboot |
| `ready-invalidation-watch` | When ready invalidates (TEC loss), LD path is forced safe — note: on USB Phase 1 this only watches the flag; real TEC loss requires Phase 2 to induce |
| `pd-passive-only-during-deployment` | Deployment mode does NOT touch STUSB4500; PD reads stay passive |

### Powered Phase 2 (MANDATORY for powered-ready claims)

| Scenario | What it proves |
|---|---|
| **`aux-control-pass`** | Auxiliary controls (green laser / GPIO6 LED / alignment) route correctly while deployment is active |
| **`ready-runtime-pass`** | Full deployment succeeds with real rails, runtime mode engages, target setpoints honored |
| **`fault-edge-pass`** | Fault edges (overtemp, unexpected current, PGOOD loss) latch correctly and force safe posture |

All three MUST pass on the live powered bench before any powered-ready claim closes.

### Composite

`deployment-runtime-flow` runs the end-to-end happy path. Useful as a smoke test; does not replace the three mandatory passes.

## Exact Commands For The Three Mandatory Passes

From the repository root:

    python3 host-console/scripts/live_controller_validation.py \
        --transport serial --port /dev/cu.usbmodem201101 \
        --scenario aux-control-pass

    python3 host-console/scripts/live_controller_validation.py \
        --transport serial --port /dev/cu.usbmodem201101 \
        --scenario ready-runtime-pass

    python3 host-console/scripts/live_controller_validation.py \
        --transport serial --port /dev/cu.usbmodem201101 \
        --scenario fault-edge-pass

If the board is on Wi-Fi only, substitute `--transport ws --ws-url ws://192.168.4.1/ws`, but verify the board has TEC and LD rails actually up by reading `rails.tec_pgood` and `rails.ld_pgood` from `status.get` before accepting the pass.

## Required Scope / Logic-Analyzer Checks

Whenever the change touches rail enables, `SBDN`, `PCN`, or fault handling, capture instrumentation traces on:

- `SBDN` (GPIO13) — must fall first on emergency shutdown
- `PCN` (GPIO21) — must be driven low whenever LD path is safe
- `PWR_TEC_EN` (GPIO15) — must rise before `PWR_LD_EN`
- `PWR_LD_EN` (GPIO17) — must rise only after `PWR_TEC_EN` + `PWR_TEC_GOOD`

Attach scope captures or logic-analyzer screenshots to the `Status.md` evidence entry.

## What To Record In `Status.md`

Under the `Validation` section, add evidence entries in this shape:

    - aux-control-pass: PASS | FAIL — <transport, port, raw result one-line, optional scope capture path>
    - ready-runtime-pass: PASS | FAIL — <same>
    - fault-edge-pass: PASS | FAIL — <same>

If BLOCKED because no powered bench is available, add under `Blockers`:

    - Powered Phase 2 validation blocked — no powered bench accessible in this session. Rebuilt image builds cleanly but cannot be exercised on real rails. Next session with bench must run aux-control-pass / ready-runtime-pass / fault-edge-pass.

## Things Not To Do

- Do NOT paraphrase a USB Phase 1 result as "ready-runtime validated."
- Do NOT claim a scenario passed if you did not capture the exact script output.
- Do NOT skip the fault-edge-pass because the earlier two passed. Fault edges are the scariest path and most likely to hide regressions.
- Do NOT proceed to review-loop close if any one of the three mandatory passes is missing.
