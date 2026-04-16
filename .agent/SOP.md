# BSL Laser Controller — Standard Operating Procedures

One-page cheat sheet. The canonical authority is `.agent/AGENT.md`. This file tells you **what to do, in what order, for common tasks**. If anything here disagrees with `AGENT.md`, `AGENT.md` wins.

## The 60-Second Session Start

1. Read `.agent/AGENT.md`.
2. Read this file (`.agent/SOP.md`).
3. Read `.agent/runs/powered-ready-console/Status.md` — the active initiative.
4. Classify the task (see table below) and load the matching skill.
5. State back in <= 8 lines: active initiative, task bucket, skills loaded, open ambiguity.

Or just run `/session-start`.

## Task → Mandatory Skill Map

| If the task is | You MUST load | Also load |
|---|---|---|
| Every firmware / host / protocol diff (no exceptions) | `cross-module-audit` | spawns relevant subset of B1/B2/B3 + A1/A2/A3 + hardware-safety + docs-guardian; record verdicts in Status.md before declaring done |
| Firmware (`components/laser_controller/src/**`) | `firmware-change` | `hardware-safety` if GPIO/rails; `firmware-validation` before declaring done |
| GUI (`host-console/src/**`) | `gui-change` + `Uncodixfy` | `gui-validation` before declaring done |
| Hardware / schematic / pinmap | `hardware-safety` | consult `docs/hardware-recon.md`, `docs/firmware-pinmap.md` |
| Protocol (add/rename/deprecate JSON command) | `protocol-evolution` | `firmware-change` + `gui-change` |
| Calibration (LUT / DAC / IMU) | `calibration-provisioning` | `firmware-change` |
| Flashing the device | `device-flashing` | — |
| Validation / bench test | `powered-bench-validation` | `review-loop` at close |
| Session ending mid-work | `context-handoff` | — |
| Milestone close or PR pre-merge | `review-loop` | all above as applicable |

## The Non-Negotiable Safety Rules

1. **Firmware is the safety authority.** GUI reflects state; it does not override it.
2. **TEC before LD.** `PWR_TEC_EN` (GPIO15) must rise before `PWR_LD_EN` (GPIO17).
3. **TEC loss = immediate LD shutdown.** Pull `SBDN` (GPIO13) low, `PCN` (GPIO21) low.
4. **GPIO37 is hazardous** — shared net (ERM_TRIG + GN_LD_EN). Treat with extreme caution.
5. **GPIO4/5 is a safety-critical shared I2C bus** — 4 devices across 3 boards.
6. **ADC telemetry is invalid unless the corresponding rail is actually on.**
7. **GPIO6 LED has two drivers** — service path + runtime path. Runtime MUST bail out when service owns the sideband. `apply_tof_sideband_state` MUST be called from `apply_outputs()` every tick. Do not remove it.
8. **Never guess about hardware.** Read schematics, datasheets, `docs/hardware-recon.md`, `docs/firmware-pinmap.md`.

## The Firmware Audit Rule (Non-Negotiable)

Before committing ANY firmware change, for every line added or modified:

1. Identify every reader and writer of every field touched.
2. Identify which task/core each reader/writer runs on (control 5ms priority-20, comms, main).
3. Verify locking consistency — control-task-owned fault fields (`fault_latched`, `active_fault_*`, `latched_fault_*`) are written WITHOUT any lock. Writing from another task is a data race.
4. Verify every `derive_state()` return value has an allowed transition from the current state (see `laser_controller_state.c`).
5. Trace every path from the change to any GPIO output.

Run `/audit-firmware` after every firmware change.

## The GUI Audit Rule (Non-Negotiable)

Before declaring any GUI change done:

1. Use canonical `types.ts` (NOT `domain/*` dead code).
2. Use `index.css` (NOT legacy `styles.css`).
3. Run dev server, capture a rendered screenshot, critique against `Uncodixfy` banned patterns.
4. Verify OFF / INVALID / IN-DEPLOYMENT rendering for any hardware-backed telemetry.
5. Confirm the change does not introduce a UI path that could trigger forbidden hardware communication.

Run `/audit-gui` after every GUI change.

## The Validation Rule

- **USB Phase 1** = parser / lockout / gating tests. Rails OFF. Not a substitute for powered evidence.
- **Powered Phase 2** = real bench with TEC + LD rails up, `PGOOD` asserted, `SBDN` driven. Required for any powered-ready claim.
- **Mandatory powered passes** (in order): `aux-control-pass`, `ready-runtime-pass`, `fault-edge-pass`.
- If no bench is available, record BLOCKED under `Blockers` in the initiative `Status.md`. Do NOT paraphrase USB Phase 1 as powered evidence.

Run `/validate-powered` to execute the three mandatory passes.

## The 7-Agent Review Loop (Non-Negotiable)

Before closing ANY major milestone or powered-ready recovery pass:

- **3 UI/UX reviewers**: Uncodixfy compliance, interaction logic, cross-page consistency
- **3 code/hardware reviewers**: firmware logic audit, GPIO/safety-pin audit, protocol contract audit
- **1 on-device validator**: the three powered Phase 2 passes

Any NO → restart. No exceptions. Run `/review-loop`.

## Context Handoff Protocol

When context tightens or a session must end:

1. Update `.agent/runs/<active>/Status.md` — done / validated / broken / blockers / next.
2. Update `.agent/runs/<active>/ExecPlan.md` — Progress, Decision Log, Surprises, Outcomes.
3. Update `.agent/AGENT.md` only if repo-level truth changed.
4. The docs are the memory. Never panic-summarize into chat.

Run `/handoff`.

## Where To Find Things

| Kind | Where |
|---|---|
| Canonical operating manual | `.agent/AGENT.md` |
| ExecPlan format | `.agent/PLANS.md` |
| Master navigation index | `.agent/INDEX.md` |
| This cheat sheet | `.agent/SOP.md` |
| Repo-local skills | `.agent/skills/` (see `README.md` there) |
| External (plugin) skills catalog | `.agent/skills/EXTERNAL.md` |
| Active initiative | `.agent/runs/powered-ready-console/` |
| Slash commands | `.claude/commands/` |
| Harness settings | `.claude/settings.json` |
| Hardware truth | `docs/hardware-recon.md`, `docs/firmware-pinmap.md`, `docs/Datasheets/`, `docs/Schematics/` |
| Protocol truth | `docs/protocol-spec.md` |
| Validation truth | `docs/validation-plan.md` |
| Validation harness | `host-console/scripts/live_controller_validation.py` |

## Slash Commands

| Command | What it does |
|---|---|
| `/session-start` | Ground the agent in AGENT.md + active Status.md, classify the task, load the right skill |
| `/audit-firmware` | Full firmware-wide logic audit on the current diff |
| `/audit-gui` | GUI logic audit + rendered-page critique against Uncodixfy |
| `/flash` | Preflight port check then propose a flash path; surfaces Wi-Fi-no-serial friction |
| `/validate-powered` | Run the three mandatory Powered Phase 2 passes |
| `/render-check` | Start dev server, capture screenshots of affected workspaces, critique |
| `/handoff` | Update Status.md / ExecPlan.md for a fresh agent to resume |
| `/review-loop` | Spawn the 7 independent review agents and consolidate their verdicts |

## Things You Will Regret Doing

- Closing work after only a build pass. Build success is not logic success.
- Claiming "ready-runtime validated" from a USB Phase 1 result.
- Writing control-task-owned fault fields from another task.
- Removing `apply_tof_sideband_state` from `apply_outputs()`.
- Skipping the state-machine transition check for your `derive_state()` return value.
- Letting the runtime path overwrite `s_tof_illumination` when service mode owns it.
- Adding CSS to `styles.css` (legacy) instead of `index.css`.
- Using the dead `domain/*` types instead of `types.ts`.
- Flashing during deployment mode.
- Renaming a protocol command without preserving an alias in the same commit.
- Closing a milestone without running the 7-agent review loop.
- Skipping the per-change cross-module audit fan-out (`.agent/skills/cross-module-audit/SKILL.md`) "because the diff is small". The previous GPIO6 LED human-injury bug was a one-line incremental change without cross-module audit. Every firmware / host / protocol diff requires the fan-out — no exceptions.
- Forgetting to run `python3 .claude/hooks/mark-audit-done.py firmware` (or `gui`) after a successful audit fan-out. The session-start banner DIRTY-tree flags only clear when the script runs; the next agent walks in blind otherwise.

## When In Doubt

1. Check `docs/hardware-recon.md` and `docs/firmware-pinmap.md` first.
2. Check the relevant datasheet in `docs/Datasheets/`.
3. Check `.agent/runs/powered-ready-console/Status.md` for the current state of play.
4. Ask. Do not guess. Hardware guesses have caused real injury on this project.
