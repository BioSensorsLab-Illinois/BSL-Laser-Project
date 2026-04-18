#!/usr/bin/env python3
"""
BSL PostToolUse hook.

Tracks dirty state after firmware / GUI / docs edits. Writes sentinel files into
.claude/state/ so the Stop hook and the SessionStart banner can surface pending
audits / validations.

Also touches ".agent/STATE.md" with a live snapshot of what changed in this session —
a cheap way to give the next fresh agent a hint about what audits are pending.
"""

import json
import os
import sys
from datetime import datetime, timezone
from pathlib import Path

# Portable repo-root resolution — see mark-audit-done.py for rationale.
REPO_ROOT = Path(
    os.environ.get("CLAUDE_PROJECT_DIR")
    or os.environ.get("BSL_REPO_ROOT")
    or Path(__file__).resolve().parents[2]
)
STATE_DIR = REPO_ROOT / ".claude" / "state"

FIRMWARE_PREFIX = REPO_ROOT / "components" / "laser_controller" / "src"
GUI_PREFIX = REPO_ROOT / "host-console" / "src"
DOCS_PREFIX = REPO_ROOT / "docs"
AGENT_PREFIX = REPO_ROOT / ".agent"


def classify(file_path: str) -> str | None:
    if not file_path:
        return None
    try:
        resolved = Path(file_path).resolve()
    except Exception:
        return None
    for prefix, kind in (
        (FIRMWARE_PREFIX, "firmware"),
        (GUI_PREFIX, "gui"),
        (DOCS_PREFIX, "docs"),
        (AGENT_PREFIX, "agent"),
    ):
        try:
            resolved.relative_to(prefix)
            return kind
        except ValueError:
            continue
    return None


def main() -> int:
    try:
        data = json.loads(sys.stdin.read())
    except Exception:
        return 0

    tool_name = data.get("tool_name", "")
    if tool_name not in {"Edit", "Write", "MultiEdit"}:
        return 0

    tool_input = data.get("tool_input", {}) or {}
    file_path = tool_input.get("file_path", "")
    kind = classify(file_path)
    if kind is None:
        return 0

    STATE_DIR.mkdir(parents=True, exist_ok=True)
    if kind in ("firmware", "gui"):
        sentinel = STATE_DIR / f"{kind}.dirty"
        sentinel.write_text(datetime.now(timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z"))

    # Append to session changelog
    changelog = STATE_DIR / "session-changelog.txt"
    with changelog.open("a") as fh:
        fh.write(f"{datetime.now(timezone.utc).isoformat(timespec='seconds').replace('+00:00', 'Z')} {kind} {file_path}\n")

    return 0


if __name__ == "__main__":
    sys.exit(main())
