#!/usr/bin/env bash
set -e

cd "$(dirname "$0")/.."

echo "[DewOS] Cleaning previous builds..."

rm -rf \
  build/rootfs \
  build/initramfs \
  build/kernel \
  out/initramfs.cpio.gz \
  out/bzImage \
  out/*.iso \
  out/*.img \
  out/*.log

mkdir -p \
  build/rootfs \
  build/initramfs \
  build/kernel \
  out

echo "[DewOS] Build artifacts cleaned."
echo "[DewOS] Sources were NOT deleted."