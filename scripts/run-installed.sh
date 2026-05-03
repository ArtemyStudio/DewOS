#!/usr/bin/env bash
set -e

cd "$(dirname "$0")/.."

KERNEL="out/bzImage"
DISK="out/install-test.img"

if [ ! -f "$KERNEL" ]; then
  echo "[DewOS] Missing kernel: $KERNEL"
  echo "[DewOS] Run: make kernel"
  exit 1
fi

if [ ! -f "$DISK" ]; then
  echo "[DewOS] Missing installed disk: $DISK"
  echo "[DewOS] Run installer first:"
  echo "  ./scripts/run-qemu-no-fs.sh"
  exit 1
fi

unset GTK_MODULES
unset LD_LIBRARY_PATH
unset LD_PRELOAD

/usr/bin/qemu-system-x86_64 \
  -m 2048M \
  -kernel "$KERNEL" \
  -drive file="$DISK",format=raw,if=virtio \
  -append "root=/dev/vda1 rw rootfstype=ext4 rootwait init=/init console=tty1 quiet loglevel=0 vt.global_cursor_default=0" \
  -display sdl \
  -vga std \
  -netdev user,id=net0 \
  -device e1000,netdev=net0
