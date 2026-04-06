# V2 Rewrite Runbook

Use this file as the execution runbook for the active rewrite.

## Source Of Truth

- `.agent/AGENT.md` is the project source of truth.
- `ExecPlan.md` is the milestone-by-milestone implementation and validation source of truth.
- `Status.md` is the live audit log.

## Operating Loop

1. Read `.agent/AGENT.md`.
2. Read `.agent/skills/Uncodixfy/SKILL.md` for GUI work.
3. Read the firmware/safety skills before firmware work.
4. Follow `ExecPlan.md` in milestone order.
5. Implement one bounded milestone at a time.
6. Run build, serial, and rendered-page validation for that milestone.
7. Record observed evidence in `Status.md`.
8. Run an independent review pass after major milestones.
9. Fix findings before advancing.

## Phase Rule

- Phase 1 is the USB-only bench.
- Phase 1 may validate:
  - serial/protocol
  - GPIO ownership and inspector behavior
  - shared I2C stability
  - IMU
  - DAC
  - ToF
  - DRV2605 register-path and `ERM_EN`
  - safe pin-level behavior for `SBDN`, `PCN`, `PWR_TEC_EN`, and `PWR_LD_EN`
  - GUI rendering and interaction logic
- Phase 1 may not claim:
  - PD runtime validation
  - TEC rail validation
  - LD rail validation
  - deployment-ready completion
  - actual laser ON validation

## Validation Rule

- firmware work assumes real serial access
- GUI work requires rendered-page inspection
- no feature is done until it is independently validated
- separate observed facts from inferred facts in `Status.md`

## Handoff Rule

If context gets tight:

- update `Status.md`
- update the ExecPlan living sections
- update `.agent/AGENT.md` if repo-level truth changed
- hand off only after the docs can restart the work without hidden context
