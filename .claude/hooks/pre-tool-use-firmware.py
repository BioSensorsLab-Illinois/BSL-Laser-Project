#!/usr/bin/env python3
"""
BSL PreToolUse hook for firmware edits.

Fires on Edit / Write / MultiEdit targeting components/laser_controller/src/*.c or *.h.
Reminds the agent of the Firmware-Wide Logic Audit rule BEFORE the edit lands.
Does not block — writes a reminder to stderr. Exit 0.

Rationale: the BSL project has had real injury from unaudited incremental firmware
changes. This hook ensures the agent sees the audit rule at the decision point,
not afterwards.
"""

import json
import sys
from pathlib import Path

REPO_ROOT = Path("/Users/zz4/BSL/BSL-Laser")
FIRMWARE_PREFIX = REPO_ROOT / "components" / "laser_controller" / "src"
STATE_DIR = REPO_ROOT / ".claude" / "state"

REMINDER = """\
⚠  BSL FIRMWARE EDIT — AUDIT RULE REMINDER

You are about to edit a safety-critical firmware file. Before committing:

  1. Trace every line you change against every reader/writer of every field touched.
  2. Identify task/core for each reader/writer (control=5ms/prio-20, comms, main).
  3. Verify locking: control-task-owned fault fields (fault_latched, active_fault_*,
     latched_fault_*) are written WITHOUT locks. NEVER write them from another task.
  4. Every derive_state() return value must have an allowed transition from the
     CURRENT state in laser_controller_state.c.
  5. If GPIO6 / tof_illumination is in play: apply_tof_sideband_state MUST still
     be called from apply_outputs every tick. The runtime path MUST bail out when
     service owns the sideband.
  6. TEC rail must enable BEFORE LD rail. TEC loss = immediate LD safe.
  7. ADC telemetry is INVALID unless the corresponding rail is actually on.

After your edit:
  • Run /audit-firmware (spawns bsl-firmware-auditor agent) against the diff.
  • Update .agent/runs/powered-ready-console/Status.md.
  • Never close a milestone without running /review-loop.

Source: .agent/skills/firmware-change/SKILL.md (mandatory read before firmware work).
"""


def main() -> int:
    try:
        data = json.loads(sys.stdin.read())
    except Exception:
        return 0

    tool_input = data.get("tool_input", {}) or {}
    file_path = tool_input.get("file_path", "")
    if not file_path:
        return 0

    try:
        resolved = Path(file_path).resolve()
    except Exception:
        return 0

    try:
        resolved.relative_to(FIRMWARE_PREFIX)
    except ValueError:
        return 0

    # Only fire once per session — avoid nagging
    STATE_DIR.mkdir(parents=True, exist_ok=True)
    sentinel = STATE_DIR / "firmware-reminder.shown"
    if not sentinel.exists():
        sys.stderr.write(REMINDER)
        sentinel.write_text("shown")

    return 0


if __name__ == "__main__":
    sys.exit(main())
