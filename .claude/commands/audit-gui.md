---
description: Run the full GUI audit on the current diff. Delegates to bsl-uncodixfy-reviewer + bsl-interaction-reviewer + bsl-consistency-reviewer sub-agents in parallel. Records completion via mark-audit-done.py.
---

## Step 1 — Preflight

Show scope and capture screenshots first:

    git diff --stat host-console/src/
    # If a rendered screenshot path already exists (e.g. from /render-check), note it.
    # Otherwise, run /render-check first to generate them, OR include a screenshot
    # capture task in the Agent prompts below.

## Step 2 — Spawn The Three A-Group Reviewers In Parallel

In a single message, use the `Agent` tool three times in parallel with these subagent_types:

1. `bsl-uncodixfy-reviewer` (A1) — rendered-page critique against the design language
2. `bsl-interaction-reviewer` (A2) — interaction logic + forbidden-path review
3. `bsl-consistency-reviewer` (A3) — cross-page consistency

Pass each agent the screenshot path(s) (if captured) and the affected workspace names.

## Step 3 — If A Protocol Command Was Touched

If the diff touches `host-console/src/types.ts`, `use-device-session.ts`, `mock-transport.ts`, or `live_controller_validation.py`, also spawn `bsl-protocol-auditor` to verify the four-place sync.

## Step 4 — Consolidate And Record

Consolidate A1 / A2 / A3 (+ B3 if spawned) into a single report. If all PASS, run:

    .claude/hooks/mark-audit-done.py gui

to clear the `gui.dirty` sentinel and stamp the last-audit timestamp.

## Step 5 — Update Status.md

Write a summary under `Validation → GUI Audit` in `.agent/runs/powered-ready-console/Status.md` with:

- Screenshot paths
- Per-agent verdict
- Any Uncodixfy violations / interaction gaps / consistency drift flagged

## Do Not

- Do NOT accept GUI claims from static code review alone. Either render the page via `/render-check` first, or include the screenshot step in the Agent prompt.
- Do NOT close a milestone on A1+A2+A3 alone — GUI audit is three of seven reviewers. Use `/review-loop` for full close.
- Do NOT mark the audit done if any A-group agent FAILs.
