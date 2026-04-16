---
name: cross-module-audit
description: Mandatory per-change multi-agent audit fan-out. Required by AGENT.md for every firmware, host, or protocol change before the work can be declared done. Spawns the relevant subset of B1/B2/B3 (firmware-wide / GPIO / protocol) and A1/A2/A3 (Uncodixfy / interaction / consistency) plus hardware-safety and docs-guardian as the diff demands. Verdicts MUST be recorded in the active initiative Status.md before any "completed" claim. Trigger on every diff that lands or is about to land.
---

# cross-module-audit

This skill codifies the **Cross-Module Audit Fan-Out** policy in AGENT.md. It is mandatory.

## When to use

Every diff that touches:

- `components/laser_controller/src/**`
- `components/laser_controller/include/**`
- `host-console/src/**`
- `host-console/scripts/live_controller_validation.py`
- `docs/protocol-spec.md`
- `docs/firmware-pinmap.md`
- `docs/firmware-architecture.md`
- `docs/hardware-recon.md`
- `.agent/AGENT.md`, `.agent/SOP.md`, `.agent/INDEX.md`

Even a one-line change. The previous GPIO6 LED human-injury bug was a one-line incremental change that was not cross-validated.

## How to use — agent fan-out matrix

Determine the agent set from the diff. Spawn ALL applicable agents in parallel (single message, multiple `Agent` tool calls).

| Diff touches… | Required agents |
|---|---|
| Any firmware `.c`/`.h` under `components/laser_controller/src` or `include` | `bsl-firmware-auditor` (B1) |
| Any `gpio_*`, ADC, PWM, I2C, SPI, sideband, or pinmap reference | `bsl-gpio-auditor` (B2) |
| Any add/rename/deprecate of a JSON command, snapshot field, or response shape | `bsl-protocol-auditor` (B3) |
| Any hardware fact, datasheet, schematic, rail, or pin reference | `bsl-hardware-safety` |
| Any GUI component, layout, or CSS | `bsl-uncodixfy-reviewer` (A1) |
| Any GUI button, gate, commit path, or interaction logic | `bsl-interaction-reviewer` (A2) |
| Any GUI shared primitive (`index.css`, `types.ts`, `panel-section`, `field`, `status-badge`) | `bsl-consistency-reviewer` (A3) |
| Any `.agent/`, `docs/`, root `CLAUDE.md` doc | `bsl-docs-guardian` |

A single change typically fans out to 3-5 agents in parallel. A protocol change that lands new types in both firmware AND host typically hits 6-8.

## How to use — recording verdicts

Every agent verdict MUST be recorded in `.agent/runs/<active>/Status.md` under a per-change `## Cross-Module Audit Evidence — <date> <change-name>` block. Format:

```markdown
## Cross-Module Audit Evidence — 2026-04-14 — usb-debug-mock landing

| Agent | Verdict | Cite | Notes |
|---|---|---|---|
| bsl-firmware-auditor (B1) | PASS | task-id-... | No data races; mock is control-task-only |
| bsl-gpio-auditor (B2) | PASS | task-id-... | Mock writes no GPIO |
| bsl-protocol-auditor (B3) | PASS | task-id-... | New commands four-place-synced |
| bsl-hardware-safety | PASS | task-id-... | USB 5V/1.5A power limitation confirmed |
| bsl-uncodixfy-reviewer (A1) | PASS | task-id-... | Banner uses warning tokens, no glass |
| bsl-interaction-reviewer (A2) | PASS | task-id-... | Mock-enable button gated by service mode |
| bsl-consistency-reviewer (A3) | PASS | task-id-... | New banner reuses inline-alert primitive |
| bsl-docs-guardian | PASS | task-id-... | AGENT.md, protocol-spec, pinmap consistent |
```

## How to use — failure handling

If ANY agent reports FAIL: stop, fix the issue, re-spawn ONLY the agents whose verdicts could change as a result of the fix, and record the new verdict. Do not declare the change complete until every required agent reports PASS or BLOCKED with explicit acceptance written into the Decision Log.

## How to use — sentinel hygiene

After every successful audit fan-out, run `python3 .claude/hooks/mark-audit-done.py firmware` and/or `python3 .claude/hooks/mark-audit-done.py gui` so the session-start banner reflects reality. The DIRTY trees are not informational decoration — they reflect real repo state for the next agent that walks in.

## Anti-patterns

- "I already audited firmware in a previous session" — does not count for the current diff.
- "The change is too small to need an audit" — that's exactly what the GPIO6 LED commit said.
- "I'll record verdicts later" — verdicts MUST be recorded in Status.md before the change is declared done.
- Spawning agents sequentially when they could run in parallel — wastes wall-clock time without changing the outcome.
- Skipping `bsl-docs-guardian` when only "small" doc edits were made — doc drift is how stale references happen.

## Relationship to the milestone-close 7-agent review loop

The cross-module audit fan-out is **per-change**. The 7-agent review loop (`.agent/skills/review-loop/SKILL.md`) is **per-milestone**. Both are required. The per-change audit catches local regressions; the per-milestone loop catches integration drift. Skipping either is a policy violation.
