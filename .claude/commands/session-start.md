---
description: Ground the agent in this repo — read AGENT.md, the active initiative Status.md, classify the task, and load the matching change-type skill (or spawn the matching BSL sub-agent).
---

Load the `session-start` skill at `.agent/skills/session-start/SKILL.md`.

## Required Reads (in order)

1. `.agent/AGENT.md` — canonical operating document.
2. `.agent/SOP.md` — one-page cheat sheet (task→skill map, audit rules, slash commands).
3. `.agent/runs/powered-ready-console/Status.md` — live state of the active initiative.

## Classify The Task

Use the table in `.agent/SOP.md`. Buckets: firmware / GUI / hardware / protocol / calibration / flashing / validation / handoff / docs-only.

## Load The Matching Resource

For each bucket, you have two options:

| Bucket | Skill path (for manual context load) | Sub-agent (for delegated execution) |
|---|---|---|
| firmware | `.agent/skills/firmware-change/SKILL.md` + `hardware-safety` | `bsl-firmware-engineer` |
| GUI | `.agent/skills/gui-change/SKILL.md` + `Uncodixfy` | `bsl-gui-engineer` |
| hardware | `.agent/skills/hardware-safety/SKILL.md` | `bsl-hardware-safety` |
| protocol | `.agent/skills/protocol-evolution/SKILL.md` | after edit, spawn `bsl-protocol-auditor` |
| calibration | `.agent/skills/calibration-provisioning/SKILL.md` | — |
| flashing | `.agent/skills/device-flashing/SKILL.md` | — |
| validation | `.agent/skills/powered-bench-validation/SKILL.md` | `bsl-bench-validator` |
| handoff | `.agent/skills/context-handoff/SKILL.md` | — |
| milestone close | `.agent/skills/review-loop/SKILL.md` | `bsl-orchestrator` (runs the 7-agent loop) |
| docs | none specific | `bsl-docs-guardian` for a consistency audit |

## Output

State in ≤ 8 lines:
- Active initiative
- Task bucket
- Skills / sub-agents loaded
- Any ambiguity that blocks progress (raise now, not later)

Do NOT paraphrase AGENT.md. Confirm you read it and identify what matters for THIS task.
