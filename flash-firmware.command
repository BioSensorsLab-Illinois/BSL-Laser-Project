#!/bin/zsh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"

. "$HOME/esp/esp-idf/export.sh"

echo "Building firmware..."
idf.py build

echo ""
echo "Looking for USB serial device..."
PORT=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)

if [ -z "$PORT" ]; then
  echo "No USB serial device found. Connect the board and try again."
  exit 1
fi

echo "Flashing to $PORT..."
idf.py -p "$PORT" flash 
