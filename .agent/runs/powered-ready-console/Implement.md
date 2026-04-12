# Powered-Ready Console Rewrite Runbook

## Source Of Truth

- `.agent/AGENT.md`
- `.agent/skills/Uncodixfy/SKILL.md`
- `.agent/runs/powered-ready-console/ExecPlan.md`
- `.agent/runs/powered-ready-console/Status.md`

## Execution Loop

1. Keep the host, firmware, mock, and docs aligned as one change.
2. Prefer additive aliases when changing the live command contract so validation can move in lockstep.
3. Re-run host build and rendered verification after each host milestone.
4. Re-run firmware build and protocol validation after each firmware milestone.
5. Record every meaningful result or blocker in `Status.md`.

## Host Rule

- The active UI path must move onto the new five-workspace model.
- The current glossy dark dashboard composition is not acceptable.
- `Operate` owns deployment and runtime control.
- `Integrate` owns persistent safety editing and bring-up tuning.

## Firmware Rule

- Deployment success must equal actual ready posture, not a nominal checklist end.
- If TEC readiness or rail validity is lost after ready, clear ready immediately and force the LD path safe.
- Integrate safety persistence must be device-side, not browser-local only.

## Validation Rule

- Host:
  - `npm run build`
  - Vite render verification with screenshots for `System`, `Operate`, and `Integrate`
- Firmware:
  - `idf.py build`
  - `host-console/scripts/live_controller_validation.py` scenarios updated for the new contract

## Handoff Rule

If work stops midstream, update:

- `ExecPlan.md`
- `Status.md`
- repo docs touched by the rewrite

The next session should be able to restart from disk without hidden context.
