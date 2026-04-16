#!/bin/zsh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
HOST_DIR="$ROOT_DIR/host-console"

cd "$HOST_DIR"

if [ ! -d node_modules ]; then
  npm install
fi

npm run build && npm run dev -- --host 0.0.0.0 --open
