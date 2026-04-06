# BSL Project Operating Manual

This is the single canonical operating document for this repository.

Every prompt, every agent, and every operation in this repo must treat this file as the first required read. Root `AGENT.md` and root `README.md` are only entrypoint pointers. If anything here conflicts with older docs or prior session memory, this file wins.

This project controls a life-critical surgical laser controller. Stability is the top priority. If a workflow mistake, permission conflict, stale assumption, or hidden control path could plausibly create unsafe behavior, stop and resolve it before continuing.

## Canonical Rule

- Read this file first for every session.
- If the work touches GUI, read `.agent/skills/Uncodixfy/SKILL.md` before making any GUI decision.
- If the work touches firmware, read `.agent/skills/gpio-config/SKILL.md` and `.agent/skills/espdl-operator/SKILL.md` before making firmware decisions. If those two skills conflict, `espdl-operator` has higher priority.
- If the work is long-horizon or complex, create or enter an initiative folder under `.agent/runs/` and work from its docs, not from chat memory.
- If anything important is unclear, ask. Do not guess.
- If anything hardware-facing is in doubt, check schematics and datasheets first, then ask if still unclear.

# ExecPlans
When writing complex features or significant refactors, use an ExecPlan (as described in .agent/PLANS.md) from design to implementation.

## Mission

Use a long-horizon, documentation-first workflow that makes the repo restartable from disk, not from chat context.

Implementation is only the beginning. Every single feature must be independently tested and validated. Firmware work assumes real serial access and real hardware debugging. GUI work requires real rendered-page inspection before it is accepted. If a page cannot be rendered in the current session, the agent must obtain the needed tool, access, or verification path before making UI claims.

## Safety And Decision Principles

1. Stability is the most crucial property of this project. A bad workflow or hidden conflict can become a safety defect.
2. Every new feature, SoP, process, or control must be checked for:
   - permission conflicts
   - ownership conflicts
   - GPIO conflicts
   - stale-state conflicts
   - validation blind spots
3. Finishing implementation is only the beginning. A feature is not done until it is independently validated.
4. For firmware, validation must assume the agent always has serial access to the device and can use real hardware debugging.
5. For GUI, validation must include real rendered pages, interaction checks, and UI/UX inspection.
6. If rendered visibility is missing, do not guess. Solve the visibility problem first.
7. If anything is unclear, ask. Enter planning mode whenever needed.
8. If anything is in doubt, check schematic and datasheet first, then ask if still unsure.

## Long-Horizon Operating Model

This workflow adopts the strongest useful ideas from OpenAI’s long-horizon Codex guidance:

- durable project memory
- explicit spec file
- checkpointed milestones
- a runbook for execution
- continuous verification
- a live status and audit log
- externalized state so a fresh agent can continue without hidden memory

Reference:
- [Run long horizon tasks with Codex](https://developers.openai.com/blog/run-long-horizon-tasks-with-codex)

The required loop for major work is:

1. Plan
2. Implement
3. Run tools and validations
4. Observe the results
5. Repair failures immediately
6. Update docs and status
7. Repeat

Do not skip the validation-and-update half of the loop.

## `.agent` Layout

The canonical repo-local workflow layout is:

- `.agent/AGENT.md`
  Canonical operating document
- `.agent/PLANS.md`
  Canonical ExecPlan instructions
- `.agent/skills/`
  Repo-local project skills only
- `.agent/runs/<initiative>/`
  Durable long-horizon working memory for each major initiative

Every major initiative should have:

- `Prompt.md`
  Frozen target, goals, non-goals, constraints, deliverables, and done-when
- `ExecPlan.md`
  Milestone plan maintained according to `.agent/PLANS.md`
- `Implement.md`
  Execution runbook for the agent
- `Status.md`
  Live progress, blockers, validation state, decisions, discoveries, and next steps

## Start Of Every Session

Before doing real work:

1. Read `.agent/AGENT.md`.
2. If the task touches GUI, read `.agent/skills/Uncodixfy/SKILL.md`.
3. If the task touches firmware, read `.agent/skills/gpio-config/SKILL.md` and `.agent/skills/espdl-operator/SKILL.md`. If they disagree, follow `espdl-operator`.
4. If the task is part of a major initiative, read:
   - `Prompt.md`
   - `ExecPlan.md`
   - `Implement.md`
   - `Status.md`
5. Ground yourself in the repo before asking questions.
6. Resolve discoverable facts from code, docs, schematics, and datasheets first.
7. Ask only when the remaining ambiguity materially changes the work.

## Skills Policy

Only project-specific skills live under `.agent/skills/`.

External skills may also exist under `~/.agents/skills/`. Treat that directory as the approved external skill source for this project when a needed skill is not repo-local.

Required initial skills:

- `.agent/skills/Uncodixfy/SKILL.md`
- `.agent/skills/gpio-config/SKILL.md`
- `.agent/skills/espdl-operator/SKILL.md`
- `.agent/skills/hardware-safety/SKILL.md`
- `.agent/skills/firmware-validation/SKILL.md`
- `.agent/skills/gui-validation/SKILL.md`
- `.agent/skills/context-handoff/SKILL.md`
- `.agent/skills/review-loop/SKILL.md`

Rules:

- For any GUI-related work, consult `Uncodixfy` first.
- For any firmware-related work, consult `gpio-config` and `espdl-operator` first. If they conflict, `espdl-operator` takes precedence.
- Check repo-local skills in `.agent/skills/` first, then check external skills in `~/.agents/skills/` when needed.
- Do not mirror all global Codex/plugin skills into this repo.
- Each repo skill must say:
  - when it must be used
  - what docs/files must be consulted
  - what validations are mandatory
  - what must be written back into `Status.md` or `.agent/AGENT.md`

## Validation Requirements

Implementation is never enough by itself.

Every feature must be independently validated.

Firmware validation rules:

- assume serial access is available
- use real hardware debugging when applicable
- validate actual logic, not only compilation
- write the observed result back into the initiative `Status.md`

GUI validation rules:

- inspect real rendered pages
- test the actual interaction logic, not just static markup
- if render access is missing, obtain it first
- verify every corner, not just the happy path

Validation must be milestone-based. Each milestone needs:

- explicit acceptance criteria
- exact validation commands or UI checks
- a stop-and-fix rule if validation fails

## Review Loop

After each major milestone:

- run a separate review/evaluation pass
- use another dedicated agent or a fresh review-only session for that pass
- keep the review objective and explicitly independent from the implementation pass

Review must cover:

- safety and permission conflicts
- firmware logic stability
- GUI interaction logic
- rendered page quality
- validation completeness

If the review finds meaningful issues:

- restart the implementation and validation cycle
- do not treat the review as a formality
- do not close the work until the issues are addressed or explicitly deferred in writing

## Context Exhaustion And Handoff

If context is getting tight:

- do not panic
- do not rush to summarize and stop
- do not pretend the work is more complete than it is

Instead:

1. Update the initiative `Status.md` with:
   - what is done
   - what is validated
   - what is still broken
   - blockers
   - exact next steps
   - missing implementation details
2. Update the initiative `ExecPlan.md`:
   - `Progress`
   - `Decision Log`
   - `Surprises & Discoveries`
   - `Outcomes & Retrospective` if appropriate
3. Update `.agent/AGENT.md` if repo-level truth changed.
4. Start the next fresh agent/session from the docs.

The handoff docs are the memory. Hidden context is not.

## Source Of Truth For Hardware And Safety

If anything hardware-facing is uncertain, check these first:

- [docs/hardware-recon.md](/Users/zz4/BSL/BSL-Laser/docs/hardware-recon.md)
- [docs/firmware-pinmap.md](/Users/zz4/BSL/BSL-Laser/docs/firmware-pinmap.md)
- [docs/datasheet-programming-notes.md](/Users/zz4/BSL/BSL-Laser/docs/datasheet-programming-notes.md)
- [docs/validation-plan.md](/Users/zz4/BSL/BSL-Laser/docs/validation-plan.md)
- the PDFs in [docs/Datasheets](/Users/zz4/BSL/BSL-Laser/docs/Datasheets)
- the PDFs and netlists in [docs/Schematics](/Users/zz4/BSL/BSL-Laser/docs/Schematics)

## Future Implementation Requirements To Preserve

This workflow-only refactor does not implement these behaviors yet, but future rewrite work must preserve them as tracked product requirements:

- the GUI Deployment page is removed later and replaced with a concise pre-enable checklist on Control
- checklist success must mean the laser can actually turn on
- TEC power must come on before LD power
- if TEC power is lost, LD power must stop immediately and `PCN` / `SBDN` must be pulled low
- TEC and LD telemetry must only be treated as valid when their power-good and control-state prerequisites are satisfied
- full GPIO permission and compatibility issues must be reworked as a core stability protocol
- during deployment-mode-owned monitoring, bring-up submodules should not look disconnected; disabled write controls must explain why
- monitored modules with deterministic readback must enter an error state if readback disappears for too long

## Repo Orientation

Current major code areas:

- `components/laser_controller/`
  Current firmware
- `host-console/`
  Current browser GUI
- `docs/`
  Hardware, protocol, architecture, and validation references
- `.agent/`
  Canonical workflow, skills, plans, and run memory

## Update Rules

When changing workflow or SoP:

1. Update `.agent/AGENT.md` first.
2. Keep root `AGENT.md` and root `README.md` as thin pointers only.
3. Keep the repo-local skill list current.
4. Keep initiative docs self-contained enough for a fresh agent to resume from them alone.
5. If a new workflow rule affects all future work, put it here, not only in chat.
