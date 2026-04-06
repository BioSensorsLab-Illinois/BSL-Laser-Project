# V2 Rewrite Status

## Current State

- The run folder is now the active rewrite workspace.
- The rewrite is being executed as Phase 1 on the USB-only bench.
- Phase 1 must not claim PD, TEC rail, LD rail, deployment-ready completion, or actual laser-enable validation.

## Completed

- Promoted the seed run folder into the active rewrite memory stack.
- Updated repo-level workflow and skill docs with USB-only Phase 1 and GPIO ownership rules.

## In Progress

- Firmware runtime/protocol refactor for:
  - runtime mode selection
  - explicit telemetry validity
  - tighter GPIO ownership semantics
- GUI refactor for:
  - inline Control-page deployment workflow
  - Bring-up deployment lock states
  - OFF / INVALID / IN-DEPLOYMENT telemetry rendering
- Final independent review pass is being run against the validated state after integrating the first review findings.

## Next Step

- Fold the validated firmware build/flash results into the milestone record, then complete a fresh review-only pass against the now-flashed image and current working tree.

## Blockers

- Physical stage1 / stage2 trigger wiring is still not source-backed in the repo.
- Powered PD / TEC / LD validation is blocked on a Phase 2 bench.

## Validation

- Completed in this session:
  - `host-console`: `npm run build` passed
  - rendered-page inspection:
    - Control screenshot captured with inline pre-enable checklist and no standalone Deployment nav item
    - Bring-up screenshot captured while deployment was active and write-lock text was visible
  - serial sanity on live bench:
    - `parser-matrix` passed on `/dev/cu.usbmodem201101`
  - local ESP-IDF `v6.0` installed at `/Users/zz4/esp/esp-idf`
  - toolchain verification passed:
    - `idf.py --version` -> `ESP-IDF v6.0.0`
    - `xtensa-esp-elf-gcc` available from the exported environment
  - firmware build passed:
    - `. "$IDF_PATH/export.sh" && idf.py fullclean build`
    - `. "$IDF_PATH/export.sh" && idf.py build` passed again after integrating review fixes
  - firmware flash passed on `/dev/cu.usbmodem201101`
  - flashed image identity check passed:
    - `firmwareVersion` now reports `91b424d-dirty`
    - `buildUtc` now reports `2026-04-06T03:18:27Z`
  - USB-only Phase 1 hardware validation passed on the flashed image:
    - `parser-matrix`
    - `deployment-lockout`
    - `deployment-usb-safe-fail`
    - `runtime-mode-gating`
  - review findings fixed and reflashed:
    - Bring-up service-mode buttons are now visibly disabled during deployment lock
    - deployment enter/exit now return full snapshot responses that include runtime-mode fields
    - deployment-owned module health now uses a frontend grace window before showing missing-readback errors
- Remaining limitations:
  - host `npm run lint` still fails on existing repo-level React hook/compiler issues outside the current rewrite seam
  - Phase 2 powered PD / TEC / LD validation remains blocked on the required hardware bench

## Notes

- Use this file as the live status and audit log.
- Keep observed results separate from inferred conclusions.
