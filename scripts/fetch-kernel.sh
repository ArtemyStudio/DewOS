#!/usr/bin/env bash
set -e

cd "$(dirname "$0")/.."

KVER="${1:-6.12.84}"
MAJOR="${KVER%%.*}"

mkdir -p sources/kernel

echo "[DewOS] Downloading vanilla Linux kernel $KVER from kernel.org..."
wget -c "https://cdn.kernel.org/pub/linux/kernel/v${MAJOR}.x/linux-${KVER}.tar.xz" \
  -O "sources/kernel/linux-${KVER}.tar.xz"

echo "[DewOS] Extracting..."
tar -xf "sources/kernel/linux-${KVER}.tar.xz" -C sources/kernel

echo "$KVER" > sources/kernel/current-version

echo "[DewOS] Kernel source ready: sources/kernel/linux-${KVER}"
