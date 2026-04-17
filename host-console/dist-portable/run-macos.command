#!/bin/bash
# BSL Host Console — macOS launcher.
# Double-click this file in Finder to start the console.
#
# Requirement: python3 (shipped with Xcode Command Line Tools).
# If missing, run once in Terminal: xcode-select --install
# That's a free one-click install from Apple.

cd "$(dirname "$0")" || exit 1

if ! command -v python3 >/dev/null 2>&1; then
  osascript -e 'display alert "Python 3 not found" message "Install Xcode Command Line Tools, then run this launcher again.\n\nOpen Terminal and run:\n\n    xcode-select --install"'
  exit 1
fi

python3 server/serve.py "$@"
