---
name: protocol-evolution
description: Required whenever a JSON command is added, renamed, deprecated, or aliased in the host⇄firmware protocol. Enforces the four-place sync rule — firmware dispatcher, host type surface, host transport/parser, and validation harness must all move together with legacy aliases preserved during migration. Trigger on changes to any status.*, deployment.*, operate.*, integrate.*, service.* command, on docs/protocol-spec.md edits, or on "add a new command" / "deprecate this command" / "rename the command" requests.
---

# protocol-evolution

The host and firmware agree on a newline-delimited JSON protocol (see `docs/protocol-spec.md`). Every protocol change must land in four coordinated places in the same pass. Missing any one leaves the contract skewed and is a common regression source during the active `powered-ready-console` migration.

## Current Command Families (v2)

| Family | Purpose |
|---|---|
| `status.*` | Read-only status and telemetry |
| `deployment.*` | Deployment mode lifecycle — enter / exit / run / set_target |
| `operate.*` | Runtime control — set_mode / set_output / set_alignment / set_led / set_target / set_modulation |
| `integrate.*` | Persistent safety config and profile persistence |

Legacy aliases still active during the migration (see `.agent/runs/powered-ready-console/ExecPlan.md`); the discipline in this skill assumes you will keep them working until everyone has migrated.

## The Four-Place Sync Rule

When adding, renaming, or deprecating a command, every one of these must change in the same commit:

1. **Firmware dispatcher** — `components/laser_controller/src/laser_controller_comms.c`
   - Add / rename the command handler.
   - For a rename, keep the old string as an alias that calls into the same handler — do NOT just delete it.
   - Follow the existing dispatcher pattern; don't invent a new one.

2. **Host canonical type surface** — `host-console/src/types.ts`
   - Add / rename the `CommandEnvelope` entry.
   - Add / rename any response type and any snapshot field the command populates.

3. **Host transport/parser/session** — `host-console/src/hooks/use-device-session.ts` and the transport classes in `host-console/src/lib/`
   - Update the `sendCommand` call sites that would previously use the old name.
   - Update `MockTransport` in `host-console/src/lib/mock-transport.ts` to honor the new or aliased command.

4. **Validation harness** — `host-console/scripts/live_controller_validation.py`
   - Update any scenario that emits the command.
   - For a rename, update the expected response payload shape if it changed.

Plus the living contract:

5. **Protocol spec** — `docs/protocol-spec.md`
   - Add the new command to the command table with args and response shape.
   - For a deprecation, mark the old name deprecated with the replacement.

## Aliasing During Migration

The `powered-ready-console` initiative added the v2 families (`status.*`, `deployment.*`, `operate.*`, `integrate.*`) while keeping legacy names temporarily. Use the same discipline for any new renames:

- Add the new name as the canonical handler.
- Keep the old name as an alias that forwards to the same handler.
- Do NOT delete the old name in the same PR that introduces the new one.
- Remove the old name only in a later, explicit "drop legacy alias" PR, after at least one fully-validated powered-bench run confirms the new name works end-to-end.

## Gating Rules (Don't Accidentally Loosen)

Every command must honor the existing gate hierarchy. Before adding a new command:

- Does it require deployment active? → enforce in the handler, not just in the UI.
- Does it require deployment ready? → same.
- Does it require service mode? → same; and it must be rejected during deployment mode.
- Is it rate-limited or time-budgeted? → keep the existing budget; do not let a new command blow the 5ms control loop.

See `project_protocol_summary.md` (memory) for the current gate map. Any new command that changes gating is an AGENT.md-level policy change, not just a dispatcher edit — call it out in `Status.md` and ExecPlan `Decision Log`.

## Breaking Changes

If an existing command's response shape changes in an incompatible way:

1. Introduce the new shape under a new command name, not by silently changing the old one.
2. Version the response if needed — add a `schemaVersion` field rather than breaking old clients.
3. Record the breaking change in the ExecPlan `Decision Log` with a migration path.

## Validation After A Protocol Change

- Host build: `cd host-console && npm run build`
- Firmware build: `. "$IDF_PATH/export.sh" && idf.py build`
- USB Phase 1 regression: at minimum `parser-matrix` and `deployment-lockout`.
- Phase 2 regression: re-run the three mandatory powered passes (`aux-control-pass`, `ready-runtime-pass`, `fault-edge-pass`) — protocol changes can silently break runtime flow.
- Rendered check: verify any UI field that reads a renamed snapshot path still populates; use `gui-validation` skill.

## Things Not To Do

- Do NOT rename a command in firmware without updating `host-console/src/types.ts`. The host will silently stop populating a snapshot field.
- Do NOT delete a legacy command name in the same commit that adds its replacement. Always alias first, remove later.
- Do NOT add a new command that bypasses existing gates (deployment.active, service_mode, fault_latched) for convenience.
- Do NOT add a new command without updating `docs/protocol-spec.md`. The spec is the contract.
