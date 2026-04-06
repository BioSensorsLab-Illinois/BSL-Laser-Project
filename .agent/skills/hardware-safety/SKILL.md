# hardware-safety

Use this skill for any hardware-facing, safety-facing, or permission-sensitive work.

Required reads before acting:

- `.agent/AGENT.md`
- `docs/hardware-recon.md`
- `docs/firmware-pinmap.md`
- `docs/datasheet-programming-notes.md`
- `docs/validation-plan.md`

What this skill requires:

- identify all affected rails, GPIOs, readback paths, and ownership boundaries
- check for permission conflicts, hidden overrides, stale-state conflicts, and telemetry validity ambiguity
- separate USB-only Phase 1 bench claims from powered-rail Phase 2 claims; do not blur them
- treat GPIO ownership as mandatory design work, not cleanup
- for LD and TEC paths, state explicitly when telemetry is valid and when it must be shown as OFF / INVALID
- if anything is uncertain after repo/doc inspection, ask rather than guessing
- write new repo-level safety/workflow truth back into `.agent/AGENT.md`

Validation expectations:

- firmware logic must be validated on real hardware whenever applicable
- do not accept “build passes” as enough for hardware-facing changes
- record observed versus inferred facts separately in the active `Status.md`
