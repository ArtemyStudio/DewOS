#!/usr/bin/env bash
set -e

cd "$(dirname "$0")/.."

KERNEL="out/bzImage"
INITRD="out/initramfs.cpio.gz"
DISK="out/install-test.img"

if [ ! -f "$KERNEL" ]; then
  echo "[DewOS] Missing kernel: $KERNEL"
  echo "[DewOS] Run: make kernel"
  exit 1
fi

if [ ! -f "$INITRD" ]; then
  echo "[DewOS] Missing initramfs: $INITRD"
  echo "[DewOS] Run: ./scripts/build-initramfs.sh"
  exit 1
fi

mkdir -p out

if [ ! -f "$DISK" ]; then
  echo "[DewOS] Creating test install disk: $DISK"
  dd if=/dev/zero of="$DISK" bs=1M count=2048 status=progress
fi

unset GTK_MODULES
unset LD_LIBRARY_PATH
unset LD_PRELOAD

/usr/bin/qemu-system-x86_64 \
  -m 2048M \
  -kernel "$KERNEL" \
  -initrd "$INITRD" \
  -drive file="$DISK",format=raw,if=virtio \
  -append "console=tty1 rdinit=/init debug loglevel=7 vt.global_cursor_default=1" \
  -display sdl \
  -vga std \
  -netdev user,id=net0 \
  -device e1000,netdev=net0 \
  -no-reboot
