#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

KERNEL="out/bzImage"
INITRAMFS="out/initramfs.cpio.gz"
ISO="out/dewos.iso"
ISO_DIR="build/iso"

echo "[DewOS] Checking host tools and kernel..."
./scripts/check-all.sh

echo "[DewOS] Building initramfs..."
./scripts/build-initramfs.sh

echo "[DewOS] Building ISO..."
rm -rf "$ISO_DIR"
mkdir -p "$ISO_DIR/boot/grub" out

if [ ! -f "$KERNEL" ]; then
    echo "[DewOS] Missing kernel: $KERNEL"
    exit 1
fi

if [ ! -f "$INITRAMFS" ]; then
    echo "[DewOS] Missing initramfs: $INITRAMFS"
    exit 1
fi

cp -f "$KERNEL" "$ISO_DIR/boot/vmlinuz-dewos"
cp -f "$INITRAMFS" "$ISO_DIR/boot/initramfs.cpio.gz"
cp config/grub.cfg "$ISO_DIR/boot/grub/grub.cfg"

grub-mkrescue -o "$ISO" "$ISO_DIR" \
    -- -as mkisofs \
    -rock \
    -joliet \
    -joliet-long 

echo "[DewOS] ISO ready: $ISO"
echo "[DewOS] ISO size: $(du -h "$ISO" | awk '{print $1}')"