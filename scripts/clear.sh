#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

MODE="${1:-build}"

case "$MODE" in
  build)
    rm -rf build
    rm -f out/bzImage out/initramfs.cpio.gz out/dewos.iso
    ;;
  disk)
    rm -f out/install-test.img out/rootfs.ext4
    ;;
  all)
    rm -rf build out
    ;;
  *)
    echo "usage: $0 {build|disk|all}"
    exit 1
    ;;
esac

mkdir -p build out
echo "[DewOS] Cleaned: $MODE"
