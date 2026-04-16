#!/usr/bin/env python3
"""
BSL Stop hook.

At session stop, checks whether firmware / GUI dirty sentinels are present and
whether an audit has been recorded since the last edit. If work looks unfinished,
writes a warning to stderr and suggests /handoff.

Does NOT block stop (advisory only). Exit 0 always.
"""

import os
import sys
from pathlib import Path

REPO_ROOT = Path("/Users/zz4/BSL/BSL-Laser")
STATE_DIR = REPO_ROOT / ".claude" / "state"


def main() -> int:
    # Discard hook input — advisory only
    try:
        sys.stdin.read()
    except Exception:
        pass

    warnings = []

    fw_dirty = STATE_DIR / "firmware.dirty"
    gui_dirty = STATE_DIR / "gui.dirty"
    last_fw_audit = STATE_DIR / "last-firmware-audit"
    last_gui_audit = STATE_DIR / "last-gui-audit"

    if fw_dirty.exists():
        if not last_fw_audit.exists() or last_fw_audit.stat().st_mtime < fw_dirty.stat().st_mtime:
            warnings.append("firmware tree was edited but /audit-firmware (bsl-firmware-auditor) has not been run since")

    if gui_dirty.exists():
        if not last_gui_audit.exists() or last_gui_audit.stat().st_mtime < gui_dirty.stat().st_mtime:
            warnings.append("GUI tree was edited but /audit-gui (bsl-uncodixfy-reviewer + bsl-interaction-reviewer) has not been run since")

    if not warnings:
        return 0

    sys.stderr.write("\n⚠  BSL WORKFLOW — PENDING AUDIT(S) AT SESSION STOP\n")
    for w in warnings:
        sys.stderr.write(f"   • {w}\n")
    sys.stderr.write(
        "\n   Before the next session can resume cleanly, run /handoff so the\n"
        "   docs (Status.md + ExecPlan.md) capture the pending audits.\n"
        "   The docs are the memory — do not rely on chat carry-over.\n\n"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
