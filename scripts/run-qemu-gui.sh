#!/usr/bin/env bash
set -e

cd "$(dirname "$0")/.."

KERNEL="out/bzImage"
ROOTFS="out/rootfs.ext4"

if [ ! -f "$KERNEL" ]; then
  echo "[DewOS] Missing kernel: $KERNEL"
  echo "[DewOS] Run: make kernel"
  exit 1
fi

if [ ! -f "$ROOTFS" ]; then
  echo "[DewOS] Missing rootfs: $ROOTFS"
  echo "[DewOS] Run: make rootfs"
  exit 1
fi

unset GTK_MODULES
unset LD_LIBRARY_PATH
unset LD_PRELOAD

/usr/bin/qemu-system-x86_64 \
  -m 512M \
  -kernel "$KERNEL" \
  -drive file="$ROOTFS",format=raw,if=virtio \
  -append "root=/dev/vda rw init=/sbin/init console=tty1 quiet loglevel=0 vt.global_cursor_default=0" \
  -display sdl \
  -vga std \
  -full-screen \
  -netdev user,id=net0 \
  -device e1000,netdev=net0 \