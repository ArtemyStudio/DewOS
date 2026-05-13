#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

echo "[DewOS] Starting full build..."
make iso

ISO="out/dewos.iso"
DEST="releases/latest/download"

if [ ! -f "$ISO" ]; then
  echo "[DewOS] ERROR: ISO not found at $ISO" >&2
  exit 1
fi

mkdir -p "$DEST"
cp "$ISO" "$DEST/dewos.iso"
echo "[DewOS] ISO copied to $DEST/dewos.iso"
ls -lh "$DEST/dewos.iso"
