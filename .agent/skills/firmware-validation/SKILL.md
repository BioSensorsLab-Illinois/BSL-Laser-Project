---
name: firmware-validation
description: Required when validating firmware behavior, logic, or protocol changes. Separates compile success from logic validation; assumes serial access via /dev/cu.usbmodem*; enforces bench-posture labeling (USB Phase 1 is NOT powered-rail evidence). Trigger on firmware validation requests, running live_controller_validation.py, scope/logic-analyzer checks on SBDN/PCN/PWR_TEC_EN/PWR_LD_EN, or interpreting firmware build/flash/run results. Write observed vs inferred separately to Status.md.
---

# firmware-validation

Use this skill whenever firmware behavior, firmware logic, or firmware-facing protocol changes are being validated.

Default assumptions:

- serial access to the controller is available
- real hardware debugging is preferred over mock-only validation

Validation rules:

- validate actual logic, not only compilation
- capture the exact command used, the observed outcome, and any mismatch against expected behavior
- record whether the observed result came from the current USB-only Phase 1 bench or from a powered Phase 2 bench
- do not claim PD, TEC-rail, LD-rail, deployment-ready, or actual laser-enable validation from the USB-only bench
- include scope or logic-analyzer checks for `SBDN`, `PCN`, `PWR_TEC_EN`, and `PWR_LD_EN` whenever those paths change
- if a feature cannot be validated on the current bench setup, say exactly why and what hardware condition is missing
- do not mark firmware work done until the validation result is written into the active `Status.md`

Write back:

- observed bench facts to `.agent/AGENT.md` when they change repo-level truth
- milestone-level evidence to the active `Status.md`
