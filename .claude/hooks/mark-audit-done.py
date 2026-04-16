#!/usr/bin/env python3
"""
Helper invoked manually by audit slash-commands to record that an audit completed.

Not wired into settings.json. The audit commands (/audit-firmware, /audit-gui,
/validate-powered) call this explicitly at the end of a successful audit so
the Stop / SessionStart hooks can surface the freshness.

Usage:
    .claude/hooks/mark-audit-done.py firmware
    .claude/hooks/mark-audit-done.py gui
    .claude/hooks/mark-audit-done.py powered
"""

import sys
from datetime import datetime, timezone
from pathlib import Path

REPO_ROOT = Path("/Users/zz4/BSL/BSL-Laser")
STATE_DIR = REPO_ROOT / ".claude" / "state"


def main() -> int:
    if len(sys.argv) != 2:
        sys.stderr.write("usage: mark-audit-done.py {firmware|gui|powered}\n")
        return 2
    kind = sys.argv[1]
    if kind not in {"firmware", "gui", "powered"}:
        sys.stderr.write(f"unknown kind: {kind}\n")
        return 2

    STATE_DIR.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z")

    if kind == "firmware":
        (STATE_DIR / "last-firmware-audit").write_text(stamp)
        dirty = STATE_DIR / "firmware.dirty"
        if dirty.exists():
            dirty.unlink()
    elif kind == "gui":
        (STATE_DIR / "last-gui-audit").write_text(stamp)
        dirty = STATE_DIR / "gui.dirty"
        if dirty.exists():
            dirty.unlink()
    elif kind == "powered":
        (STATE_DIR / "last-powered-validation").write_text(stamp)

    sys.stdout.write(f"recorded {kind} audit at {stamp}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
