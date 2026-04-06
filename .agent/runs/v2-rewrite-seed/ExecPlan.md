# Seed the V2 Rewrite Runbook

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and `Outcomes & Retrospective` must be kept up to date as work proceeds.

This document must be maintained in accordance with `.agent/PLANS.md`.

## Purpose / Big Picture

The next major rewrite should not begin from chat memory. After this seed exists, a fresh agent will be able to start the firmware/GUI rewrite with a durable target, milestone plan, execution instructions, and live status log already in the repository.

## Progress

- [x] (2026-04-05 08:00Z) Created the seed rewrite run folder.
- [x] (2026-04-05 08:00Z) Captured the rewrite purpose, constraints, and done-when in `Prompt.md`.
- [x] (2026-04-05 08:00Z) Added the execution runbook in `Implement.md`.
- [x] (2026-04-05 08:00Z) Added the initial live status log in `Status.md`.
- [ ] First real firmware v2 milestone planned and accepted.
- [ ] First real GUI v2 milestone planned and accepted.

## Surprises & Discoveries

- Observation: The current worktree had no canonical `.agent/` structure even though the repo already contained draft agent docs in `.agents/`.
  Evidence: manual inspection of the repo tree before this seed was created.

## Decision Log

- Decision: Use a seed run folder for the rewrite before implementation starts.
  Rationale: The rewrite is long-horizon enough that it needs durable memory and a known restart point from day one.
  Date/Author: 2026-04-05 / Codex

## Outcomes & Retrospective

- Outcome: The rewrite now has a repo-local starting point instead of relying on hidden session context.
  Gap: The actual firmware and GUI rewrite plan is still to be authored in a future milestone.

## Context and Orientation

This repository currently contains the legacy firmware in `components/laser_controller/` and the legacy browser GUI in `host-console/`. The rewrite operating rules live in `.agent/AGENT.md`. This run folder is only the seed memory stack for the future rewrite.

## Plan of Work

The next contributor should expand this seed into the real rewrite plan. The first substantive milestone should define the firmware v2 architecture and GUI v2 architecture, the protocol cutover strategy, and the validation matrix. After that, the work should proceed milestone by milestone with stop-and-fix validation.

## Concrete Steps

From the repository root:

    pwd
    sed -n '1,220p' .agent/AGENT.md
    sed -n '1,220p' .agent/runs/v2-rewrite-seed/Prompt.md
    sed -n '1,260p' .agent/runs/v2-rewrite-seed/Implement.md
    sed -n '1,260p' .agent/runs/v2-rewrite-seed/Status.md

Expected result:

    You can explain the rewrite goal, constraints, current status, and immediate next step without consulting prior chat logs.

## Validation and Acceptance

Acceptance for this seed:

- the four run files exist
- the next contributor can identify what the rewrite is, what it is not, and what to do next
- the information is sufficient without hidden prior context

## Idempotence and Recovery

These files are safe to reread and update repeatedly. If the rewrite direction changes, update the run files rather than replacing them with chat-only guidance.

## Artifacts and Notes

Important reference files:

    .agent/AGENT.md
    .agent/PLANS.md
    .agent/skills/Uncodixfy/SKILL.md

## Interfaces and Dependencies

The seed rewrite depends on:

- `.agent/AGENT.md` as canonical operating guidance
- `.agent/PLANS.md` as canonical ExecPlan guidance
- `.agent/skills/Uncodixfy/SKILL.md` for GUI-first work
