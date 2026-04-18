#!/usr/bin/env python3
"""
BSL UserPromptSubmit hook.

If the firmware or GUI tree is dirty (sentinels present) and the user's prompt
does NOT obviously reference an audit / validation / handoff command, inject
a terse reminder so the agent considers running the appropriate audit.

Output format (for UserPromptSubmit hooks): print to stdout and exit 0 to
append context that the agent will see.
"""

import json
import os
import sys
from pathlib import Path

# Portable repo-root resolution — see mark-audit-done.py for rationale.
REPO_ROOT = Path(
    os.environ.get("CLAUDE_PROJECT_DIR")
    or os.environ.get("BSL_REPO_ROOT")
    or Path(__file__).resolve().parents[2]
)
STATE_DIR = REPO_ROOT / ".claude" / "state"

AUDIT_KEYWORDS = (
    "audit", "/audit-firmware", "/audit-gui", "/review-loop", "/handoff",
    "/validate-powered", "/render-check", "commit", "finalize", "close",
)


def main() -> int:
    try:
        data = json.loads(sys.stdin.read())
    except Exception:
        return 0

    prompt = (data.get("prompt") or "").lower()
    if any(kw in prompt for kw in AUDIT_KEYWORDS):
        return 0

    notes = []
    fw = STATE_DIR / "firmware.dirty"
    gui = STATE_DIR / "gui.dirty"
    if fw.exists():
        notes.append(f"firmware tree DIRTY (last edit {fw.read_text().strip()}) — run /audit-firmware before closing")
    if gui.exists():
        notes.append(f"GUI tree DIRTY (last edit {gui.read_text().strip()}) — run /audit-gui and /render-check before closing")

    if not notes:
        return 0

    # Anything written to stdout by a UserPromptSubmit hook is added to the
    # agent's context as additional system-side information.
    sys.stdout.write("<bsl-workflow-reminder>\n")
    for n in notes:
        sys.stdout.write(f"  • {n}\n")
    sys.stdout.write("</bsl-workflow-reminder>\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
