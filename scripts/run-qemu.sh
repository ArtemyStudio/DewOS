#!/usr/bin/env bash
set -e

cd "$(dirname "$0")/.."

DISK="out/install-test.img"
MNT="/mnt/dew-check-auto"

is_installed() {
  if [ ! -f "$DISK" ]; then
    return 1
  fi

  sudo mkdir -p "$MNT"

  LOOP="$(sudo losetup --find --show -P "$DISK")"

  cleanup() {
    sudo umount "$MNT" 2>/dev/null || true
    sudo losetup -d "$LOOP" 2>/dev/null || true
  }

  if ! sudo mount "${LOOP}p1" "$MNT" 2>/dev/null; then
    cleanup
    return 1
  fi

  if [ -x "$MNT/init" ] && [ -f "$MNT/etc/dewos-installed" ]; then
    cleanup
    return 0
  fi

  cleanup
  return 1
}

echo "[DewOS] Checking install state..."

if is_installed; then
  echo "[DewOS] Installed system found."
  exec ./scripts/run-installed.sh
fi

echo "[DewOS] No installed system found."
echo "[DewOS] Starting live installer..."

./scripts/run-qemu-no-fs.sh || true

echo "[DewOS] QEMU live session ended."
echo "[DewOS] Checking install state again..."

if is_installed; then
  echo "[DewOS] Installed system found after install."
  echo "[DewOS] Booting installed DewOS..."
  exec ./scripts/run-installed.sh
fi

echo "[DewOS] Still no installed system."
echo "[DewOS] Nothing to boot."
