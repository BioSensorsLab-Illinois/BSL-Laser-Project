---
description: Run the mandatory 7-agent review loop via bsl-orchestrator. Any single FAIL restarts the implement/validate loop. Required at every major milestone close. This is the AGENT.md "Most Important Rule".
---

Spawn the `bsl-orchestrator` sub-agent. It will spawn the 7 independent reviewers in parallel, consolidate their verdicts, and record the Review Loop Evidence block into `.agent/runs/powered-ready-console/Status.md`.

## Preflight (before spawning the orchestrator)

Confirm the following in the current session:

1. The current diff is saved to disk. The orchestrator's 7 reviewers read the working tree, not chat context.
2. If screenshots of affected workspaces exist (e.g. from a recent `/render-check`), note their paths.
3. Whether a powered bench is available this session (affects whether C1 will PASS or BLOCKED).

## Spawn

Use the `Agent` tool with `subagent_type: "bsl-orchestrator"`. Pass it:

- Commit range or "uncommitted working tree"
- List of workspaces / subsystems affected
- Any screenshot paths
- Bench availability note

## After The Orchestrator Returns

The orchestrator returns a consolidated report and writes the Review Loop Evidence block to Status.md.

### Decision Gate

- **All 7 PASS** → the milestone can close. The orchestrator has already written the evidence block. Append a closing note to Status.md.
- **Any FAIL** → do NOT close. Act on the specific remediation from each failing reviewer. Re-run `/review-loop` after fixes.
- **C1 BLOCKED** → record the blocker under `Blockers` in Status.md exactly as C1 reported. Milestone stays `In Progress`. Compile + USB Phase 1 is NOT a substitute.

## Do Not

- Do NOT skip `/review-loop` for "small changes". Any safety-critical code change warrants it.
- Do NOT substitute any single reviewer. Seven in, seven out.
- Do NOT treat the loop as a formality. If a reviewer says no, restart.
- Do NOT let the implementation agent also be one of the reviewers. Reviewers must be independent (fresh sub-agent = fresh context).
