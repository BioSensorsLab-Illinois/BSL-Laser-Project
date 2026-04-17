#!/bin/bash
# BSL Host Console — Linux launcher.
# Run with:  ./run-linux.sh
# (May need to chmod +x run-linux.sh once after unzipping.)

cd "$(dirname "$0")" || exit 1

if ! command -v python3 >/dev/null 2>&1; then
  echo "error: python3 not found on PATH." >&2
  echo "  Debian/Ubuntu:  sudo apt install python3" >&2
  echo "  Fedora/RHEL:    sudo dnf install python3" >&2
  echo "  Arch:           sudo pacman -S python"    >&2
  exit 1
fi

python3 server/serve.py "$@"
