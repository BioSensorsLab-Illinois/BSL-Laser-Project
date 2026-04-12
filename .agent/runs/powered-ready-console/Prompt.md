# Powered-Ready Console Rewrite Prompt

## Purpose

Replace the current host console, host/firmware command contract, and powered deployment runtime flow with a new operator console that can drive a real powered-ready bench instead of the legacy USB-only safe-fail path.

## Goals

- rebuild the host console into five workspaces:
  - `System`
  - `Operate`
  - `Integrate`
  - `Update`
  - `History`
- remove the current dark glass-heavy UI and replace it with a restrained light console that follows `.agent/skills/Uncodixfy/SKILL.md`
- replace the active host command and snapshot contract with explicit command families:
  - `status.*`
  - `deployment.*`
  - `operate.*`
  - `integrate.*`
- make deployment progress publish live step updates instead of only reporting terminal results
- require deployment success to mean the controller is actually ready:
  - TEC rail enabled and `PGOOD`
  - LD rail enabled and `PGOOD`
  - `SBDN` high
  - ready-posture validation completed
  - final idle-ready posture restored with `PCN` low
- make integrate safety edits persistent across reboot and automatically consumed by deployment without a manual sync step

## Non-Goals

- do not claim final `binary_trigger` completion before trigger wiring is source-backed
- do not remove desktop Tauri capabilities such as serial, wireless, firmware inspect/flash, and session export
- do not keep the current host component architecture alive behind the new UI unless it is still required as an adapter during migration

## Hard Constraints

- stability first
- GUI work must follow `.agent/skills/Uncodixfy/SKILL.md`
- GUI validation must use real rendered-page inspection
- firmware changes must preserve TEC-before-LD sequencing and immediate LD shutdown on TEC loss
- every major milestone must update the run docs and validation evidence

## Deliverables

- powered-ready rewrite run docs in this folder
- updated repo docs reflecting the new contract and validation target
- rewritten host console using the new five-workspace model
- firmware aliases and runtime changes for the new command families and deployment semantics
- updated mock and validation tooling

## Done When

- host build passes on the rewritten app
- firmware build passes on the rewritten contract
- rendered verification shows the new `System`, `Operate`, and `Integrate` workspaces
- deployment checklist rows update in real time
- deployment only succeeds when the controller reaches actual ready posture
- persisted integrate safety survives reboot and is used automatically by deployment
