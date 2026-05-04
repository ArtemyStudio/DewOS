#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

MODE="${1:-iso}"

KERNEL="out/bzImage"
INITRAMFS="out/initramfs.cpio.gz"
ISO="out/dewos.iso"
DISK="out/install-test.img"

ensure_disk() {
  mkdir -p out

  if [ ! -f "$DISK" ]; then
    echo "[DewOS] Creating test disk: $DISK"
    dd if=/dev/zero of="$DISK" bs=1M count=2048 status=progress
  fi
}

check_file() {
  if [ ! -f "$1" ]; then
    echo "[DewOS] Missing: $1"
    echo "[DewOS] Build it first. Example:"
    echo "  make iso"
    exit 1
  fi
}

qemu_common_env() {
  unset GTK_MODULES
  unset LD_LIBRARY_PATH
  unset LD_PRELOAD
}

qemu_serial_args() {
  if [ "${DEBUG_SERIAL:-0}" = "1" ]; then
    echo "-serial"
    echo "stdio"
  else
    echo "-serial"
    echo "none"
  fi
}

run_iso() {
  check_file "$ISO"
  ensure_disk
  qemu_common_env

  echo "[DewOS] Running ISO: $ISO"
  mapfile -t serial_args < <(qemu_serial_args)

  qemu-system-x86_64 \
    -m 2048M \
    -cdrom "$ISO" \
    -boot d \
    -drive file="$DISK",format=raw,if=virtio \
    -display sdl \
    -vga std \
    "${serial_args[@]}" \
    -usb \
    -device usb-kbd \
    -netdev user,id=net0 \
    -device e1000,netdev=net0
}

run_direct() {
  check_file "$KERNEL"
  check_file "$INITRAMFS"
  ensure_disk
  qemu_common_env

  echo "[DewOS] Running direct kernel/initramfs boot"
  mapfile -t serial_args < <(qemu_serial_args)

  qemu-system-x86_64 \
    -m 2048M \
    -kernel "$KERNEL" \
    -initrd "$INITRAMFS" \
    -drive file="$DISK",format=raw,if=virtio \
    -append "console=tty1 rdinit=/init quiet loglevel=0 vt.global_cursor_default=0 nokaslr" \
    -display sdl \
    -vga std \
    "${serial_args[@]}" \
    -usb \
    -device usb-kbd \
    -netdev user,id=net0 \
    -device e1000,netdev=net0 \
    -no-reboot
}

case "$MODE" in
  iso)
    run_iso
    ;;
  direct)
    run_direct
    ;;
  *)
    echo "usage: $0 {iso|direct}"
    exit 1
    ;;
esac
