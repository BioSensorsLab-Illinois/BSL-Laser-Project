# V2 Rewrite Seed Runbook

Use this file as the execution runbook for future work on the rewrite.

## Source Of Truth

- `.agent/AGENT.md` is the project source of truth.
- `ExecPlan.md` is the rewrite milestone source of truth.
- `Status.md` is the live status and audit log.

## How To Operate

1. Read `.agent/AGENT.md` first.
2. If the work touches GUI, read `.agent/skills/Uncodixfy/SKILL.md` first.
3. Follow the `ExecPlan.md` milestone by milestone.
4. Keep scope tight. Do not expand the milestone casually.
5. Run validation after each milestone.
6. If validation fails, fix it before moving to the next milestone.
7. Update `Status.md` continuously.
8. Update the ExecPlan living sections continuously.

## Validation Rule

- firmware work assumes real serial access
- GUI work requires rendered-page inspection
- no feature is done until it is independently validated

## Handoff Rule

If context is getting tight:

- update `Status.md`
- update the ExecPlan living sections
- update `.agent/AGENT.md` if repo-level truth changed
- only then hand off to a fresh session
