#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

MODE="${1:-all}"
KVER="${KVER:-6.12.84}"
KMAJOR="${KVER%%.*}"
KERNEL_URL="https://cdn.kernel.org/pub/linux/kernel/v${KMAJOR}.x/linux-${KVER}.tar.xz"
LFS_URL="https://www.linuxfromscratch.org/lfs/downloads/stable-systemd"

fetch_kernel() {
  mkdir -p sources/kernel

  if [ ! -f "sources/kernel/linux-${KVER}.tar.xz" ]; then
    echo "[DewOS] Fetching Linux ${KVER}..."
    wget -c "$KERNEL_URL" -O "sources/kernel/linux-${KVER}.tar.xz"
  fi

  if [ ! -d "sources/kernel/linux-${KVER}" ]; then
    echo "[DewOS] Extracting Linux ${KVER}..."
    tar -xf "sources/kernel/linux-${KVER}.tar.xz" -C sources/kernel
  fi

  echo "$KVER" > sources/kernel/current-version
  echo "[DewOS] Kernel source ready: sources/kernel/linux-${KVER}"
}

fetch_lfs() {
  mkdir -p sources/lfs

  echo "[DewOS] Fetching LFS source list..."
  wget -c "$LFS_URL/wget-list" -O sources/lfs/wget-list
  wget -c "$LFS_URL/md5sums" -O sources/lfs/md5sums

  echo "[DewOS] Fetching LFS packages..."
  wget --input-file=sources/lfs/wget-list --continue --directory-prefix=sources/lfs

  echo "[DewOS] Checking LFS md5 sums..."
  (cd sources/lfs && md5sum -c md5sums)
}

case "$MODE" in
  kernel) fetch_kernel ;;
  lfs) fetch_lfs ;;
  all)
    fetch_kernel
    fetch_lfs
    ;;
  *)
    echo "usage: $0 {all|kernel|lfs}"
    exit 1
    ;;
esac
