# V2 Rewrite Prompt

## Purpose

Refactor the firmware and GUI around a single firmware-owned runtime model with explicit GPIO ownership, explicit telemetry validity, and an inline Control-page deployment workflow. This run folder is now the durable working memory for the real rewrite, not a seed placeholder.

## Goals

- complete the USB-only Phase 1 rewrite and validation loop
- move deployment controls into the Control page and remove the dedicated Deployment page
- harden GPIO ownership and telemetry-validity rules in firmware and GUI
- preserve firmware-owned safety authority while separating:
  - safe-off
  - deployment active but not ready
  - deployment ready
  - runtime mode selection
  - service overrides
- introduce runtime mode selection:
  - `binary_trigger`
  - `modulated_host`

## Non-Goals

- do not claim Phase 2 powered-hardware validation while still on the USB-only bench
- do not claim final physical-trigger completion before the missing trigger wiring is source-backed
- do not use DRV2605 external-trigger behavior on the shared `IO37 / GN_LD_EN` net

## Hard Constraints

- stability first
- ask rather than guess on high-impact ambiguity
- firmware validation assumes real serial access
- GUI validation requires rendered-page inspection
- every major task must use and update the ExecPlan
- GUI work must consult `.agent/skills/Uncodixfy/SKILL.md` first
- USB-only Phase 1 cannot validate PD negotiation, TEC rail bring-up, LD rail bring-up, deployment-ready completion, or actual laser enable

## Deliverables

- a maintained `ExecPlan.md`
- an execution runbook in `Implement.md`
- a living audit/status record in `Status.md`
- firmware, protocol, and GUI changes for Phase 1
- build, serial, and rendered-page validation evidence recorded in `Status.md`

## Done When

- the rewrite docs are self-sufficient for a fresh agent
- firmware and GUI implement the Phase 1 behavior in `ExecPlan.md`
- validation evidence clearly separates Phase 1 completed work from Phase 2 blocked work
