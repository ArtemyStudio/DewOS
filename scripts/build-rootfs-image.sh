#!/usr/bin/env bash
set -e

cd "$(dirname "$0")/.."

INIT_CPP="app/init/init.cpp"
ROOTFS_DIR="build/rootfs-disk"
IMAGE="out/rootfs.ext4"

if [ ! -f "$INIT_CPP" ]; then
  echo "[DewOS] Missing $INIT_CPP"
  exit 1
fi

echo "[DewOS] Cleaning old rootfs image..."
rm -rf "$ROOTFS_DIR"
rm -f "$IMAGE"

mkdir -p "$ROOTFS_DIR"/{bin,sbin,etc,dev,proc,sys,tmp,root,home,usr/bin,usr/lib,var/log}

echo "[DewOS] Building C++ init..."
g++ -std=c++20 -Wall -Wextra -O2 -static "$INIT_CPP" -o "$ROOTFS_DIR/sbin/init" -lcrypt

chmod +x "$ROOTFS_DIR/sbin/init"

KILO_C="thirdparty/kilo/kilo.c"

if [ -f "$KILO_C" ]; then
  echo "[DewOS] Building kilo..."
  gcc -Wall -Wextra -O2 -static "$KILO_C" -o "$ROOTFS_DIR/bin/kilo"
  chmod +x "$ROOTFS_DIR/bin/kilo"
else
  echo "[DewOS] thirdparty/kilo/kilo.c not found, skipping kilo."
fi

chmod 1777 "$ROOTFS_DIR/tmp"

echo "[DewOS] Creating basic device nodes..."
sudo mknod -m 600 "$ROOTFS_DIR/dev/console" c 5 1 || true
sudo mknod -m 666 "$ROOTFS_DIR/dev/null" c 1 3 || true
sudo mknod -m 666 "$ROOTFS_DIR/dev/zero" c 1 5 || true
sudo mknod -m 666 "$ROOTFS_DIR/dev/tty" c 5 0 || true
sudo mknod -m 620 "$ROOTFS_DIR/dev/tty1" c 4 1 || true

echo "[DewOS] Creating ext4 image..."
dd if=/dev/zero of="$IMAGE" bs=1M count=128 status=progress
mkfs.ext4 -F "$IMAGE"

echo "[DewOS] Copying rootfs into image..."
mkdir -p build/mnt-rootfs
sudo mount "$IMAGE" build/mnt-rootfs
sudo cp -a "$ROOTFS_DIR"/. build/mnt-rootfs/
sync
sudo umount build/mnt-rootfs

echo "[DewOS] RootFS image ready: $IMAGE"