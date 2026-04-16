---
description: Walk through a firmware flash with preflight port detection. Load device-flashing skill; surface the Wi-Fi-no-serial friction if applicable.
---

Load `.agent/skills/device-flashing/SKILL.md`.

Before proposing any flash path, run preflight:

    ls /dev/cu.usbmodem* 2>/dev/null || echo "no-serial-port"

Based on the result:

### If a `/dev/cu.usbmodem*` path exists

Confirm with me which board is attached (port path), then propose either:
- Path 1: `./flash-firmware.command` (auto-selects first port, builds, flashes, monitors)
- Path 2: `. "$IDF_PATH/export.sh" && idf.py build && idf.py -p <the-port> flash monitor`

Wait for my explicit approval before running flash commands.

### If no `/dev/cu.usbmodem*` path exists

This is the Friction Mode documented in `device-flashing/SKILL.md`. Do NOT attempt to flash. Report exactly:
- What was checked (`ls /dev/cu.usbmodem*` returned empty)
- Why the flash cannot proceed (no serial CDC enumerated)
- The three options the skill lists (recable / hand-off / no Wi-Fi OTA)
- Record the blocker in `.agent/runs/powered-ready-console/Status.md` under `Blockers` following the `context-handoff` pattern

### After any successful flash

1. Confirm flashed image identity by reading `firmwareVersion` and `buildUtc` from a `status.get` response.
2. Run USB Phase 1 smoke at minimum:
       python3 host-console/scripts/live_controller_validation.py --transport serial --port <port> --scenario parser-matrix
3. If this flash is in service of a powered-ready milestone, load `powered-bench-validation` skill and run the three mandatory Powered Phase 2 passes.
