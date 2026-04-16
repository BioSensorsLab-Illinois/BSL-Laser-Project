# External Plugin Skills — BSL Context Map

Claude Code sessions in this repo have access to a large catalogue of globally-installed plugin skills (under `~/.claude/plugins/`). This file maps the ones worth reaching for in BSL-specific work to the tasks where they help. If a BSL-local skill exists for the same purpose, the BSL-local skill is authoritative.

Repo-local skills live under `.agent/skills/` and override everything below when they overlap.

## When To Use External Skills

Reach for an external skill when:

1. No BSL-local skill exists for the task.
2. The external skill's scope is clearly distinct from the BSL-local one (e.g. `engineering:debug` for structured debugging flow complements, but does not replace, `firmware-change`).
3. You need a structured scaffolding (ADR, postmortem, test-strategy doc) that the external skill is designed to produce.

Do NOT use an external skill when:

- A BSL-local skill already covers the task. The local skill encodes hard-won project-specific lessons the external skill will not know about.
- The task involves firmware safety logic, GPIO ownership, or hardware power sequencing. These ALWAYS route through `firmware-change` / `hardware-safety` first, not a generic external skill.

## Engineering-Category (Highest-Value For BSL)

| Skill | When to use in BSL |
|---|---|
| [`engineering:code-review`](#) | Use for host-console / Python validation-harness code review. For firmware, ALWAYS run `/audit-firmware` first — it encodes the GPIO6 / state-machine / data-race rules a generic reviewer will miss. |
| [`engineering:debug`](#) | Use for structured debug sessions on host console or tooling. For firmware bug chasing, complement with `firmware-change` knowledge — the generic skill does not know about the dual-driver GPIO6 bug. |
| [`engineering:architecture`](#) | Use when creating a new ADR for a significant design decision (e.g. authentication model, TLS adoption, button-board topology). Place the ADR under `docs/adr/` (new directory) and reference it from `.agent/AGENT.md`. |
| [`engineering:documentation`](#) | Use when producing technical docs intended for operators, service technicians, or future maintainers. Place under `docs/`. |
| [`engineering:system-design`](#) | Use for medium-to-large design work (e.g. WiFi fallback model, OTA update path, calibration provisioning server). Always follow with an ExecPlan per `.agent/PLANS.md`. |
| [`engineering:testing-strategy`](#) | Use when designing a NEW validation scenario family or a test plan for a subsystem. The existing 11 scenarios in `live_controller_validation.py` are the floor; use this skill to design additions. |
| [`engineering:deploy-checklist`](#) | Use when preparing a firmware release (tagging, artifacts, rollback plan). The BSL-specific flashing steps still come from `device-flashing`. |
| [`engineering:incident-response`](#) | Use if a powered-bench run produces unsafe behavior. Triage + postmortem + fix ExecPlan. Route through `.agent/runs/<incident-date>/` as a dedicated initiative. |
| [`engineering:tech-debt`](#) | Use to categorize the existing "Known Gaps & Blockers" memory into an actionable backlog. |
| [`engineering:standup`](#) | Use for quick session-end summaries, but do NOT substitute for `context-handoff` skill which updates `Status.md` properly. |

## Design-Category

All of these are secondary to `Uncodixfy` for this project. The `Uncodixfy` design language is deliberately restrictive — do NOT adopt a more decorative external design system on top of it.

| Skill | When to use in BSL |
|---|---|
| [`design:accessibility-review`](#) | Use to add WCAG 2.1 AA review on top of `Uncodixfy`. The instrument-console aesthetic is not a substitute for a11y — contrast, focus states, keyboard nav still matter. |
| [`design:design-critique`](#) | Use for cross-page consistency checks, but final compliance verdict is Uncodixfy + the 7-agent `review-loop` Agent A1. |
| [`design:ux-copy`](#) | Use to audit / improve operator-facing copy (error strings, disabled-state hover help, empty states). Keep copy functional — no marketing voice. |
| [`design:design-system`](#) | Use if formalizing `index.css` tokens into a documented design system. Do not introduce new colors / gradients / shadows not already in `index.css`. |

Skipping: `design:user-research`, `design:research-synthesis`, `design:design-handoff` — not applicable to a single-operator safety-critical instrument.

## Data-Category

Useful primarily for analyzing bench-run output, fault-log collections, and timing data.

| Skill | When to use in BSL |
|---|---|
| [`data:analyze`](#) | Use when processing raw validation-harness output across many runs (e.g. timing spreads of SBDN-to-safe, PGOOD rise-times). |
| [`data:explore-data`](#) | Use when profiling a new telemetry log dump to understand what fields are populated and at what rates. |
| [`data:statistical-analysis`](#) | Use when quantifying fault-rate distributions, calibration-variance, or sensor-noise floors. |
| [`data:data-visualization`](#) / `data:create-viz` | Use to generate timing-plot images for the validation evidence attached to `Status.md`. |
| [`data:build-dashboard`](#) | Use for a one-off HTML dashboard summarizing a powered-bench run series. Not for the live operator UI — that stays in `host-console/`. |
| [`data:sql-queries`](#) / `data:write-query` | Not directly relevant. Skip unless you're building a telemetry warehouse downstream. |

## Productivity-Category

Useful for session bookkeeping, not for code changes.

| Skill | When to use in BSL |
|---|---|
| [`productivity:task-management`](#) | Optional. The primary task system is `.agent/runs/<initiative>/Status.md`. Use this only if you also want a cross-session `TASKS.md`. |
| [`productivity:memory-management`](#) | The BSL project already has a curated memory set at `/Users/zz4/.claude/projects/-Users-zz4-BSL-BSL-Laser/memory/`. Use this skill only to add new cross-project decoder entries (acronyms, nicknames). |

## Other Categories

| Category | Why it matters for BSL | Typical use |
|---|---|---|
| `pdf-viewer:*` | Datasheets and schematics are PDFs under `docs/Datasheets/` and `docs/Schematics/`. | Open a datasheet and annotate programming notes. Use `pdf-viewer:open`, then `/annotate`, to collaborate on a PDF with the user. |
| `anthropic-skills:docx` / `:pptx` / `:xlsx` | Not common in BSL work. | Only if an external stakeholder asks for a Word / Excel / PowerPoint export. |
| `claude-api` | Relevant if writing auxiliary tooling that calls the Anthropic API (e.g. a test-report generator). | Not for the controller firmware or host console. |
| `enterprise-search:*` | Not typically relevant — BSL is a local repo project without enterprise data sources. | Use only if the user connects Notion / Confluence for external notes. |
| `legal:*`, `marketing:*`, `customer-support:*`, `brand-voice:*`, `product-management:*` | Not applicable to firmware / GUI work. | Skip unless explicitly invoked by the user. |

## Skills That Should Never Be Used For BSL Safety-Critical Work

- Any external code-review or debug skill as a **substitute** for `firmware-change` / `hardware-safety` / the 7-agent `review-loop`. External skills do not know about the GPIO6 data race, the state-machine gaps, the TEC-before-LD rule, or the fault-latch threading model.
- Any external design skill that adds glass / gradients / hero sections / decorative copy. `Uncodixfy` is deliberately stricter than any generic design system.
- Any external deploy / release skill for firmware without routing through `device-flashing` and the `powered-bench-validation` three passes. "Deploy" in BSL means landing firmware on the controller — the generic skill does not know about the Wi-Fi-no-serial friction or the mandatory powered passes.

## Rule

Check repo-local skills first. Route hardware / firmware / safety work through BSL-local skills. Use external skills only to complement — never to replace — the BSL-local ones for anything safety-critical.
