#!/usr/bin/env python3
"""
BSL PreToolUse hook for GUI edits.

Fires on Edit / Write / MultiEdit targeting host-console/src/ or host-console/src/index.css.
Reminds the agent of the Uncodixfy design language and common dead-code pitfalls.
Does not block — writes a reminder to stderr on first fire per session. Exit 0.
"""

import json
import sys
from pathlib import Path

REPO_ROOT = Path("/Users/zz4/BSL/BSL-Laser")
GUI_PREFIX = REPO_ROOT / "host-console" / "src"
STATE_DIR = REPO_ROOT / ".claude" / "state"

REMINDER = """\
⚠  BSL GUI EDIT — UNCODIXFY + DEAD-CODE REMINDER

You are about to edit a host-console file. Before committing:

  • Banned patterns (Uncodixfy): glass / frosted surfaces, gradient text,
    oversized rounded corners (>12px), pill overload, hero sections inside the
    dashboard, dramatic shadows, transform animations, ornamental labels,
    serif+sans combos, decorative color drift. See .agent/skills/Uncodixfy/SKILL.md.

  • Required aesthetic: restrained light instrument console. Think Linear,
    Raycast, Stripe, GitHub. Functional > decorative.

  • Canonical types: host-console/src/types.ts. Do NOT import from
    domain/* or platform/* — those are dead-code trees.

  • CSS: host-console/src/index.css only. Never styles.css (legacy).

  • State: useDeviceSession() + RealtimeTelemetryStore. No Redux / Zustand / context.

  • Transport envelope: {id, type: 'cmd', cmd, args}. Mock / WebSerial / WebSocket all
    implement DeviceTransport from types.ts.

  • Disabled controls MUST have a reason (title / aria-label / hover help).

  • OFF / INVALID / IN-DEPLOYMENT telemetry must render explicitly — no silent blanks.

  • Operate has NO PD refresh or PDO-write paths. Those live in BringupWorkbench / Integrate only.

After your edit:
  • Run /render-check (starts dev server, captures screenshots, Uncodixfy critique).
  • Run /audit-gui before claiming done.
  • If a protocol command was touched, ensure four-place sync — spawn bsl-protocol-auditor.
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
        resolved.relative_to(GUI_PREFIX)
    except ValueError:
        return 0

    STATE_DIR.mkdir(parents=True, exist_ok=True)
    sentinel = STATE_DIR / "gui-reminder.shown"
    if not sentinel.exists():
        sys.stderr.write(REMINDER)
        sentinel.write_text("shown")

    return 0


if __name__ == "__main__":
    sys.exit(main())
