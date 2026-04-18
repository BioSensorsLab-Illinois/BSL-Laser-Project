#!/usr/bin/env bash
# BSL SessionStart hook — authoritative banner for every new session.
# Reads sentinel state from .claude/state/ to surface dirty work / last audit / last validation.
# Non-blocking: always exit 0 regardless of internal errors.

set -u
# Portable repo-root resolution: Claude Code sets CLAUDE_PROJECT_DIR; fall back
# to the legacy BSL_REPO_ROOT, then to this script's own location
# (<repo>/.claude/hooks/<script>.sh → ../.. == <repo>).
REPO_ROOT="${CLAUDE_PROJECT_DIR:-${BSL_REPO_ROOT:-$(cd "$(dirname "$0")/../.." && pwd)}}"
STATE_DIR="${REPO_ROOT}/.claude/state"
mkdir -p "${STATE_DIR}" 2>/dev/null || true

# --- Gather dirty state signals ---
FW_DIRTY="clean"
GUI_DIRTY="clean"
LAST_FW_AUDIT="never"
LAST_GUI_AUDIT="never"
LAST_POWERED="never"
ACTIVE_INITIATIVE="powered-ready-console"

[[ -f "${STATE_DIR}/firmware.dirty" ]] && FW_DIRTY="DIRTY"
[[ -f "${STATE_DIR}/gui.dirty" ]] && GUI_DIRTY="DIRTY"
[[ -f "${STATE_DIR}/last-firmware-audit" ]] && LAST_FW_AUDIT="$(cat "${STATE_DIR}/last-firmware-audit" 2>/dev/null || echo never)"
[[ -f "${STATE_DIR}/last-gui-audit" ]] && LAST_GUI_AUDIT="$(cat "${STATE_DIR}/last-gui-audit" 2>/dev/null || echo never)"
[[ -f "${STATE_DIR}/last-powered-validation" ]] && LAST_POWERED="$(cat "${STATE_DIR}/last-powered-validation" 2>/dev/null || echo never)"

# --- Print banner using echo to avoid bash printf "-- means end-of-options" issue ---
# Using a heredoc so lines beginning with `--` are literal.
cat <<BANNER

================================================================
 BSL LASER CONTROLLER — safety-critical device
================================================================
 Authoritative docs (read in order):
   1. .agent/AGENT.md       — operating manual
   2. .agent/SOP.md         — one-page cheat sheet
   3. .agent/INDEX.md       — master navigation index
   4. .agent/runs/${ACTIVE_INITIATIVE}/Status.md — active work
----------------------------------------------------------------
 Or just run: /session-start
----------------------------------------------------------------
 Sub-agents (spawn via Agent tool with subagent_type):
   • bsl-orchestrator       — runs the 7-agent review loop
   • bsl-firmware-engineer  — structured firmware work
   • bsl-gui-engineer       — structured GUI work
   • bsl-hardware-safety    — hardware reasoner (read-only)
   • bsl-docs-guardian      — doc consistency auditor
   • Review-loop A1-A3      — Uncodixfy / interaction / consistency
   • Review-loop B1-B3      — firmware / GPIO / protocol
   • Review-loop C1         — powered bench validator
----------------------------------------------------------------
 Slash commands: /session-start /audit-firmware /audit-gui
                 /flash /validate-powered /render-check
                 /handoff /review-loop
----------------------------------------------------------------
 Current state:
   • firmware tree: ${FW_DIRTY}   (last audit: ${LAST_FW_AUDIT})
   • GUI tree:      ${GUI_DIRTY}   (last audit: ${LAST_GUI_AUDIT})
   • last powered validation: ${LAST_POWERED}
----------------------------------------------------------------
 Non-negotiable rules:
   • Firmware is the safety authority — GUI reflects state
   • TEC-before-LD sequencing; TEC loss = immediate LD shutdown
   • Never guess about hardware — schematics + datasheets first
   • 7-agent review loop before any milestone close
   • USB Phase 1 is NOT a substitute for Powered Phase 2
================================================================

BANNER
exit 0
