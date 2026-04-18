#!/usr/bin/env bash
# BSL statusline — shows branch, dirty flags, active initiative, last validation.
# Called frequently by Claude Code. Keep it fast.

set -u
# Portable repo-root resolution — see session-start-banner.sh for rationale.
REPO_ROOT="${CLAUDE_PROJECT_DIR:-${BSL_REPO_ROOT:-$(cd "$(dirname "$0")/../.." && pwd)}}"
STATE_DIR="${REPO_ROOT}/.claude/state"

BRANCH=$(git -C "${REPO_ROOT}" branch --show-current 2>/dev/null || echo no-branch)

FW="clean"
GUI="clean"
[[ -f "${STATE_DIR}/firmware.dirty" ]] && FW="DIRTY"
[[ -f "${STATE_DIR}/gui.dirty" ]] && GUI="DIRTY"

LAST_POWERED="never"
[[ -f "${STATE_DIR}/last-powered-validation" ]] && LAST_POWERED="$(cat "${STATE_DIR}/last-powered-validation" 2>/dev/null || echo never)"

printf 'BSL • %s • fw:%s • gui:%s • bench:%s • powered-ready-console active • .agent/INDEX.md' \
  "${BRANCH}" "${FW}" "${GUI}" "${LAST_POWERED}"
