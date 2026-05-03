#!/usr/bin/env bash
set -e

cd "$(dirname "$0")/.."

KVER="${1:-$(cat sources/kernel/current-version 2>/dev/null || echo 6.12.84)}"
KDIR="sources/kernel/linux-${KVER}"

if [ ! -d "$KDIR" ]; then
  echo "[DewOS] Missing kernel source: $KDIR"
  echo "[DewOS] Run:"
  echo "        ./scripts/fetch-kernel.sh $KVER"
  exit 1
fi

echo "[DewOS] Building vanilla Linux kernel $KVER..."

cd "$KDIR"

make mrproper
make x86_64_defconfig

./scripts/config \
  --enable BLK_DEV_INITRD \
  --enable DEVTMPFS \
  --enable DEVTMPFS_MOUNT \
  --enable TMPFS \
  --enable PROC_FS \
  --enable SYSFS \
  --enable TTY \
  --enable SERIAL_8250 \
  --enable SERIAL_8250_CONSOLE \
  --enable PRINTK \
  --enable INPUT \
  --enable INPUT_EVDEV \
  --enable INPUT_MOUSE \
  --enable MOUSE_PS2 \
  --enable USB_HID \
  --enable HID_GENERIC \

make -j"$(nproc)" bzImage

cd ../../../

cp "$KDIR/arch/x86/boot/bzImage" out/bzImage

echo "[DewOS] Kernel built: out/bzImage"
