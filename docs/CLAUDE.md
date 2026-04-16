# Documentation Guardrails

You are editing under `docs/` — the hardware, protocol, architecture, and validation source-of-truth for the BSL project. This file applies in addition to the repo-root `CLAUDE.md` and `.agent/AGENT.md`.

## The Source-of-Truth Rule

These files ARE the authoritative reference for the rest of the project. Firmware code is audited against them. Review-loop agents cite them. Never let them drift silently.

| File | Authority |
|---|---|
| `hardware-recon.md` | Recovered hardware topology, cross-board paths, shared-net warnings |
| `firmware-pinmap.md` | Complete ESP32-S3 GPIO map; every safety audit cites this |
| `firmware-architecture.md` | State machine, interlock truth table, fault classes |
| `datasheet-programming-notes.md` | Per-peripheral firmware rules distilled from datasheets |
| `protocol-spec.md` | Host↔firmware JSON command contract; living document |
| `host-console-architecture.md` | GUI workspace layout and transport model |
| `validation-plan.md` | Bench rules, fault matrix, timing targets, exit criteria |
| `Schematics/*.pdf` + `*.NET` | Source-backed electrical reality |
| `Datasheets/*.pdf` | IC-specific programming / safe-operating rules |

## Editing Rules

1. **Citations must be accurate.** If you edit a pin / address / IC reference, verify it against the schematic PDF + netlist before committing. `firmware-pinmap.md` bugs cause firmware bugs.
2. **Protocol spec and firmware dispatcher must agree.** An entry in `protocol-spec.md` without a handler in `components/laser_controller/src/laser_controller_comms.c` (or vice versa) is a contract break. After editing either, spawn `bsl-protocol-auditor`.
3. **Validation plan cases must be achievable.** A new case here without a corresponding scenario in `host-console/scripts/live_controller_validation.py` is a hollow check. Either add the scenario or mark the case as `pending-scenario`.
4. **Datasheet notes cite the PDF.** Every claim in `datasheet-programming-notes.md` must reference the specific datasheet + page. If you cannot cite, flag as "unverified; requires datasheet confirmation".
5. **Do not delete historical validation evidence.** `Measured Result` TBDs should be filled in with evidence, not reset.

## Schematic / Datasheet Reading

Use the `pdf-viewer` MCP: `mcp__plugin_pdf-viewer_pdf__display_pdf` then `mcp__plugin_pdf-viewer_pdf__interact` for navigation / search / annotation. The hardware-safety agent (`bsl-hardware-safety`) has these tools in its allowed set.

## After Every Edit

- For a protocol change, spawn `bsl-protocol-auditor`.
- For a pinmap / hardware-recon edit, spawn `bsl-hardware-safety` to verify against schematic + datasheet.
- For a validation-plan edit, confirm the corresponding scenario exists (or add it) in `host-console/scripts/live_controller_validation.py`.
- Record meaningful doc changes in `.agent/runs/powered-ready-console/ExecPlan.md` Decision Log.
