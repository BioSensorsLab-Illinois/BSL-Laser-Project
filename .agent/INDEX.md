# BSL Agentic Workspace — Master Index

This is the navigation index for every agentic resource in this repo. The repo is a **Claude Code plugin package** (`.claude-plugin/plugin.json`) bundling:

- 15 repo-local skills under `.agent/skills/`
- 11 specialized sub-agents under `.claude/agents/`
- 8 slash commands under `.claude/commands/`
- 7 executable hooks under `.claude/hooks/`
- 1 output style (`bsl-safety-critical`) under `.claude/output-styles/`
- 4 nested `CLAUDE.md` guardrails (repo root, firmware, host console, docs, initiatives)
- 1 active initiative and 1 archived initiative under `.agent/runs/`

If you are a fresh agent landing in this repo, start here.

## Read-This-First Stack

1. [`.agent/AGENT.md`](./AGENT.md) — canonical operating manual. Overrides everything else.
2. [`.agent/SOP.md`](./SOP.md) — one-page cheat sheet. Task→skill map, audit rules, validation rules.
3. [`.agent/runs/powered-ready-console/Status.md`](./runs/powered-ready-console/Status.md) — live state of the active initiative.

The root `CLAUDE.md` auto-imports `SOP.md` via the `@.agent/SOP.md` syntax, so the cheat sheet loads at every session start.

## Canonical Documents

| File | Purpose |
|---|---|
| [AGENT.md](./AGENT.md) | Operating manual, mission, canonical rules, GPIO ownership protocol, update rules |
| [SOP.md](./SOP.md) | One-page cheat sheet — task→skill, audit rules, validation, handoff, slash commands |
| [PLANS.md](./PLANS.md) | ExecPlan authoring contract (used by every `.agent/runs/<initiative>/ExecPlan.md`) |
| [INDEX.md](./INDEX.md) | This file. Navigation index. |

## Nested CLAUDE.md Files

| Scope | File |
|---|---|
| Repo root | [`/CLAUDE.md`](../CLAUDE.md) |
| Firmware | [`components/laser_controller/CLAUDE.md`](../components/laser_controller/CLAUDE.md) |
| Host console | [`host-console/CLAUDE.md`](../host-console/CLAUDE.md) |
| Docs / hardware / protocol | [`docs/CLAUDE.md`](../docs/CLAUDE.md) |
| Initiative folders | [`runs/CLAUDE.md`](./runs/CLAUDE.md) |

Claude Code applies each nested CLAUDE.md to tool calls rooted under that directory, so firmware edits get firmware guardrails automatically.

## Sub-Agents (spawn via `Agent` tool, `subagent_type: "<name>"`)

All under [`.claude/agents/`](../.claude/agents/).

### Engineers

| Agent | When |
|---|---|
| [bsl-firmware-engineer](../.claude/agents/bsl-firmware-engineer.md) | Structured firmware work. Loads firmware-change + hardware-safety context. Opus model. |
| [bsl-gui-engineer](../.claude/agents/bsl-gui-engineer.md) | Structured host-console work. Loads gui-change + Uncodixfy. Sonnet model with Claude Preview MCP. |

### Read-only reasoners

| Agent | When |
|---|---|
| [bsl-hardware-safety](../.claude/agents/bsl-hardware-safety.md) | Hardware / schematic / datasheet reasoner. Uses pdf-viewer MCP. Opus model. |
| [bsl-docs-guardian](../.claude/agents/bsl-docs-guardian.md) | Doc consistency auditor. Catches stale references, missing index entries, drift. |

### The 7-Agent Review Loop

All must PASS for a milestone to close. Any FAIL restarts the loop.

| Slot | Agent | Scope |
|---|---|---|
| A1 | [bsl-uncodixfy-reviewer](../.claude/agents/bsl-uncodixfy-reviewer.md) | Design language rendered critique |
| A2 | [bsl-interaction-reviewer](../.claude/agents/bsl-interaction-reviewer.md) | Button→sendCommand→firmware traces |
| A3 | [bsl-consistency-reviewer](../.claude/agents/bsl-consistency-reviewer.md) | Cross-page consistency |
| B1 | [bsl-firmware-auditor](../.claude/agents/bsl-firmware-auditor.md) | Firmware-wide logic audit |
| B2 | [bsl-gpio-auditor](../.claude/agents/bsl-gpio-auditor.md) | GPIO ownership + safety-pin audit |
| B3 | [bsl-protocol-auditor](../.claude/agents/bsl-protocol-auditor.md) | Four-place protocol sync audit |
| C1 | [bsl-bench-validator](../.claude/agents/bsl-bench-validator.md) | Powered Phase 2 on-device validation |

### Orchestrator

| Agent | Responsibility |
|---|---|
| [bsl-orchestrator](../.claude/agents/bsl-orchestrator.md) | Spawns the 7 reviewers in parallel, consolidates verdicts, writes Status.md evidence block. Opus model. |

## Slash Commands

All under [`.claude/commands/`](../.claude/commands/). Each is a thin wrapper that loads the relevant skill and/or spawns the relevant sub-agent.

| Command | Wires |
|---|---|
| `/session-start` | session-start skill |
| `/audit-firmware` | bsl-firmware-auditor (+ bsl-gpio-auditor if pins touched, + bsl-protocol-auditor if dispatcher touched) |
| `/audit-gui` | bsl-uncodixfy-reviewer + bsl-interaction-reviewer + bsl-consistency-reviewer |
| `/flash` | device-flashing skill |
| `/validate-powered` | bsl-bench-validator |
| `/render-check` | Claude Preview MCP + bsl-uncodixfy-reviewer |
| `/handoff` | context-handoff skill + session-changelog read |
| `/review-loop` | bsl-orchestrator |

## Hooks (automation in `.claude/hooks/`)

All wired in `.claude/settings.json`.

| Hook | Event | What it does |
|---|---|---|
| [session-start-banner.sh](../.claude/hooks/session-start-banner.sh) | SessionStart | Prints rich banner with dirty-tree flags, last-audit timestamps, sub-agent list |
| [pre-tool-use-firmware.py](../.claude/hooks/pre-tool-use-firmware.py) | PreToolUse(Edit/Write/MultiEdit) | On firmware-file edits, reminds about the audit rule (once per session) |
| [pre-tool-use-gui.py](../.claude/hooks/pre-tool-use-gui.py) | PreToolUse(Edit/Write/MultiEdit) | On GUI-file edits, reminds about Uncodixfy + dead-code map (once per session) |
| [post-tool-use-track.py](../.claude/hooks/post-tool-use-track.py) | PostToolUse(Edit/Write/MultiEdit) | Marks firmware.dirty / gui.dirty sentinels; appends to session-changelog |
| [user-prompt-reminder.py](../.claude/hooks/user-prompt-reminder.py) | UserPromptSubmit | If dirty sentinels + no audit keyword in prompt, injects a reminder into agent context |
| [stop-validation-check.py](../.claude/hooks/stop-validation-check.py) | Stop | Warns if session ends with dirty trees and no audit since last edit |
| [mark-audit-done.py](../.claude/hooks/mark-audit-done.py) | manual | Invoked by audit commands to clear dirty sentinels and stamp last-audit |
| [statusline.sh](../.claude/hooks/statusline.sh) | statusLine | Shows branch, fw/gui dirty flags, last powered-bench timestamp |

State lives under `.claude/state/` (gitignored):

    .claude/state/firmware.dirty
    .claude/state/gui.dirty
    .claude/state/last-firmware-audit
    .claude/state/last-gui-audit
    .claude/state/last-powered-validation
    .claude/state/session-changelog.txt
    .claude/state/firmware-reminder.shown     (per-session, one-shot)
    .claude/state/gui-reminder.shown          (per-session, one-shot)

## Output Style

[`.claude/output-styles/bsl-safety-critical.md`](../.claude/output-styles/bsl-safety-critical.md) — enforces safety-critical voice:

- file:line citations required
- observed vs inferred separation
- PASS / FAIL / BLOCKED verdict taxonomy
- no "ready" without Powered Phase 2 evidence
- no marketing tone, no filler

Activated via `"outputStyle": "bsl-safety-critical"` in `.claude/settings.json`.

## Skills — Repo-Local

All under [`.agent/skills/`](./skills/). Catalog in [`skills/README.md`](./skills/README.md).

### Read before any change

- [cross-module-audit](./skills/cross-module-audit/SKILL.md) — mandatory for every firmware / host / protocol diff; spawns the per-change agent fan-out and records verdicts in Status.md before declaring the change done
- [firmware-change](./skills/firmware-change/SKILL.md) — mandatory before any firmware edit
- [gui-change](./skills/gui-change/SKILL.md) — mandatory before any host-console edit
- [Uncodixfy](./skills/Uncodixfy/SKILL.md) — mandatory GUI design language

### Session flow

- [session-start](./skills/session-start/SKILL.md) — startup ritual
- [context-handoff](./skills/context-handoff/SKILL.md) — disk-based handoff

### Validation & review

- [firmware-validation](./skills/firmware-validation/SKILL.md)
- [gui-validation](./skills/gui-validation/SKILL.md)
- [powered-bench-validation](./skills/powered-bench-validation/SKILL.md)
- [review-loop](./skills/review-loop/SKILL.md)

### Hardware, protocol, device

- [hardware-safety](./skills/hardware-safety/SKILL.md)
- [device-flashing](./skills/device-flashing/SKILL.md)
- [protocol-evolution](./skills/protocol-evolution/SKILL.md)
- [calibration-provisioning](./skills/calibration-provisioning/SKILL.md)

### Reference only

- [gpio-config](./skills/gpio-config/SKILL.md) — generic GPIO reference; not BSL-authoritative

## External Plugin Skills

[`.agent/skills/EXTERNAL.md`](./skills/EXTERNAL.md) — catalog of globally-available plugin skills (engineering/design/data/productivity) and when to reach for them in BSL context. External skills never replace BSL-local skills for safety-critical work.

## Initiatives

Under [`.agent/runs/`](./runs/).

| Status | Folder | What |
|---|---|---|
| Active | [powered-ready-console](./runs/powered-ready-console/) | Host console + firmware contract + deployment runtime rewrite for real powered-bench operation |
| Archived | [_archive/v2-rewrite-seed](./runs/_archive/v2-rewrite-seed/) | Superseded USB-only rewrite seed (historical only) |

Per-initiative required files: `Prompt.md`, `ExecPlan.md`, `Implement.md`, `Status.md`. See [`runs/CLAUDE.md`](./runs/CLAUDE.md) for the hygiene rules.

## Plugin Manifest

[`.claude-plugin/plugin.json`](../.claude-plugin/plugin.json) — packages this workflow as a Claude Code plugin (v2.0.0).

## Hardware & Protocol Source-Of-Truth Docs

Under [`docs/`](../docs/):

- [hardware-recon.md](../docs/hardware-recon.md)
- [firmware-pinmap.md](../docs/firmware-pinmap.md)
- [firmware-architecture.md](../docs/firmware-architecture.md)
- [datasheet-programming-notes.md](../docs/datasheet-programming-notes.md)
- [protocol-spec.md](../docs/protocol-spec.md)
- [host-console-architecture.md](../docs/host-console-architecture.md)
- [validation-plan.md](../docs/validation-plan.md)
- [Schematics/](../docs/Schematics/) — board-level electrical truth
- [Datasheets/](../docs/Datasheets/) — per-IC programming / safe-operating rules

## Validation Harness

[`host-console/scripts/live_controller_validation.py`](../host-console/scripts/live_controller_validation.py) — two transports (serial + ws), 11 scenarios. The three mandatory powered passes: `aux-control-pass`, `ready-runtime-pass`, `fault-edge-pass`.

## Environment / Harness Config

[`.claude/settings.json`](../.claude/settings.json):

- `env.MAX_THINKING_TOKENS=63999` — max extended-thinking budget per turn
- `env.BSL_REPO_ROOT` — used by hooks and statusline
- `outputStyle: "bsl-safety-critical"` — the safety-critical voice
- `statusLine` — live fw/gui/bench state
- Five hook event wirings
- 34 allow-permissions, 11 deny-permissions

## MCP Integrations Worth Knowing

- **Claude Preview** — used by `bsl-gui-engineer`, `bsl-uncodixfy-reviewer`, `/render-check` for real page rendering + inspection
- **pdf-viewer** — used by `bsl-hardware-safety` for schematic + datasheet reading/annotation
- **scheduled-tasks** — use to schedule weekly doc-consistency audits via `bsl-docs-guardian`, or recurring validation-reminder checks

## When Things Change

Per `AGENT.md` update rules:

1. Update `.agent/AGENT.md` first when workflow or SoP changes.
2. Keep root `AGENT.md` / `README.md` / `CLAUDE.md` as thin pointers.
3. Keep this `INDEX.md` current as new skills / sub-agents / commands / hooks / styles are added.
4. Keep `.agent/SOP.md` current.
5. Keep `.agent/skills/README.md` in sync.
6. Initiative docs must remain self-contained.

## Quick-Start For A Fresh Agent

    Run /session-start

That is all. The slash command loads AGENT.md + SOP.md + active Status.md, classifies your task, and loads the right skill or spawns the right sub-agent. If you are ever unsure what to run, run /session-start first.
