# Initiative Folder Guardrails

Each subfolder under `.agent/runs/<initiative>/` is durable long-horizon working memory. This file applies to anything you edit here.

## Required Files Per Initiative

Every initiative folder MUST contain:

- `Prompt.md` — frozen target: Purpose, Goals, Non-Goals, Hard Constraints, Deliverables, Done-When
- `ExecPlan.md` — milestone plan per `.agent/PLANS.md`
- `Implement.md` — execution runbook for the current session
- `Status.md` — live state: Current State, Completed, In Progress, Next Step, Blockers, Validation, Notes

## The Living-Document Rule

Per `.agent/PLANS.md`:

- `ExecPlan.md` is a living document. Maintain `Progress` / `Surprises & Discoveries` / `Decision Log` / `Outcomes & Retrospective`.
- `Status.md` is the first thing the next agent reads. Keep it accurate and concrete. Append; never overwrite history.

## The Self-Containment Rule

An initiative folder must let a fresh agent resume from disk ALONE, with no chat memory. If you added a fact to Status.md that only makes sense with additional chat context, rewrite it to stand alone.

## Handoff Discipline

When ending a session mid-initiative, run `/handoff` — it invokes the `context-handoff` skill to update Status + ExecPlan. Never panic-summarize into chat and skip the doc write. The docs are the memory.

## Archiving

When an initiative is superseded, move it under `_archive/` with a README.md entry explaining what replaced it and why. Do not delete — historical context has value.
