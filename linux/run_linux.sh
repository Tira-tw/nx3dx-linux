#!/usr/bin/env bash
# Run the new main-repository Linux build without replacing the older Linux port.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
BIN="$ROOT/build/linux-main/NX3DX"
DATA="${1:-$ROOT/data}"
if [[ $# -gt 0 ]]; then shift; fi
if [[ ! -s "$DATA/menu.bin" ]]; then
  echo "ERROR: game data not found: $DATA/menu.bin" >&2
  echo "Pass the existing local data directory as argument 1." >&2
  exit 1
fi
if [[ ! -x "$BIN" ]]; then
  echo "ERROR: $BIN is not built." >&2
  echo "Run bash linux/build_linux.sh first." >&2
  exit 1
fi
if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
  echo "ERROR: No graphical session detected in this terminal." >&2
  exit 1
fi
echo "[NX3DX Linux main GLES2] binary: $BIN"
echo "[NX3DX Linux main GLES2] data: $DATA"
echo "[NX3DX Linux main GLES2] saved games: SDL user-specific data folder"
echo "Tip: run with '2>&1 | tee ~/nx3dx-reborn-test.log' to capture a log"
exec "$BIN" --data "$DATA" "$@"
