# BSL Laser Controller — Claude Code Operating Guide

**Read `.agent/AGENT.md` first.** It is the canonical operating document and overrides everything else.

**Auto-loaded context** (via `@` imports — these files are compiled into this CLAUDE.md at session start):

@.agent/SOP.md

---

## Quick Reference

For full navigation see [`.agent/INDEX.md`](./.agent/INDEX.md). For detailed rules see [`.agent/AGENT.md`](./.agent/AGENT.md).

## What This Project Is

A handheld 785nm surgical laser controller. **Life-critical.** Stability > velocity.

- **Firmware:** `components/laser_controller/` — ESP32-S3, C, ESP-IDF v6.0, 5ms deterministic control loop. See [`components/laser_controller/CLAUDE.md`](./components/laser_controller/CLAUDE.md) for firmware-specific guardrails.
- **GUI:** `host-console/` — React 19, TypeScript, Vite 8, browser-based operator console. See [`host-console/CLAUDE.md`](./host-console/CLAUDE.md) for GUI-specific guardrails.
- **Hardware:** Multi-board PCB stack (MainPCB, BMS, USB-PD, ToF-LED), 9 ICs on shared buses. See [`docs/CLAUDE.md`](./docs/CLAUDE.md) for documentation discipline.
- **Protocol:** Newline-delimited JSON over USB CDC and WiFi WebSocket.

## First Action Every Session

Run `/session-start` — it grounds you in AGENT.md + SOP.md + the active Status.md, classifies the task, and loads the right skill. Or read those three files manually.

## Specialized Sub-Agents (spawn via Agent tool with `subagent_type`)

| Agent | Use for |
|---|---|
| `bsl-orchestrator` | Runs the 7-agent review loop and consolidates verdicts |
| `bsl-firmware-engineer` | Structured firmware work with full safety context |
| `bsl-gui-engineer` | Structured host console work with Uncodixfy enforcement |
| `bsl-hardware-safety` | Hardware / schematic / datasheet reasoner (read-only) |
| `bsl-docs-guardian` | Doc consistency auditor; fixes stale references |
| `bsl-uncodixfy-reviewer` (A1) | UI/UX rendered-page critique |
| `bsl-interaction-reviewer` (A2) | Interaction logic + forbidden-path review |
| `bsl-consistency-reviewer` (A3) | Cross-page consistency |
| `bsl-firmware-auditor` (B1) | Full firmware-wide logic audit |
| `bsl-gpio-auditor` (B2) | GPIO ownership + safety-pin audit |
| `bsl-protocol-auditor` (B3) | Four-place protocol sync audit |
| `bsl-bench-validator` (C1) | Powered Phase 2 on-device validation |

## Slash Commands

| Command | Purpose |
|---|---|
| `/session-start` | Ground the agent; classify the task; load the right skill |
| `/audit-firmware` | Firmware-wide logic audit on current diff |
| `/audit-gui` | GUI logic audit + rendered critique against Uncodixfy |
| `/flash` | Preflight port check and propose a flash path |
| `/validate-powered` | Run the three mandatory Powered Phase 2 passes |
| `/render-check` | Start dev server, capture screenshots, critique |
| `/handoff` | Update Status/ExecPlan for a fresh agent |
| `/review-loop` | Spawn 7 independent reviewers and consolidate |

## Automation (active hooks)

- **SessionStart** — reads `.claude/state/` sentinels, prints a rich banner with dirty-tree flags and last-audit timestamps.
- **PreToolUse(Edit/Write/MultiEdit)** — on firmware or GUI files, surfaces an audit-rule reminder once per session.
- **PostToolUse(Edit/Write/MultiEdit)** — sets `firmware.dirty` / `gui.dirty` sentinels and appends to the session changelog.
- **UserPromptSubmit** — if dirty sentinels are present and the prompt doesn't reference an audit command, injects a reminder into context.
- **Stop** — warns if the session ends with dirty trees and no audit since the last edit.
- **Output style** — `bsl-safety-critical` enforces file:line citations, observed-vs-inferred separation, and strict PASS/FAIL/BLOCKED verdict taxonomy.

## Build & Test

```bash
# Firmware
. $IDF_PATH/export.sh && idf.py build

# Host console
cd host-console && npm ci && npm run build

# Validation (real device)
python3 host-console/scripts/live_controller_validation.py --transport serial --port /dev/cu.usbmodem201101 --scenario aux-control-pass
python3 host-console/scripts/live_controller_validation.py --transport ws --ws-url ws://192.168.4.1/ws --scenario ready-runtime-pass

# Dev server
cd host-console && npm run dev -- --host 0.0.0.0 --open
```

## Commit Style

Short imperative summaries describing the change. Example: "Refactor deployment runtime ownership and persist control targets". For firmware changes, include the audit trace in the body.
