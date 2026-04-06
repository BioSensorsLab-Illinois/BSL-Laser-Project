# firmware-validation

Use this skill whenever firmware behavior, firmware logic, or firmware-facing protocol changes are being validated.

Default assumptions:

- serial access to the controller is available
- real hardware debugging is preferred over mock-only validation

Validation rules:

- validate actual logic, not only compilation
- capture the exact command used, the observed outcome, and any mismatch against expected behavior
- if a feature cannot be validated on the current bench setup, say exactly why and what hardware condition is missing
- do not mark firmware work done until the validation result is written into the active `Status.md`

Write back:

- observed bench facts to `.agent/AGENT.md` when they change repo-level truth
- milestone-level evidence to the active `Status.md`
