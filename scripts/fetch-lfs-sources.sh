#!/usr/bin/env bash
set -e

cd "$(dirname "$0")/.."

LFS_URL="https://www.linuxfromscratch.org/lfs/downloads/stable-systemd"

echo "[DewOS] Fetching official LFS systemd wget-list and md5sums..."
wget -c "$LFS_URL/wget-list" -O sources/lfs/wget-list
wget -c "$LFS_URL/md5sums" -O sources/lfs/md5sums

echo "[DewOS] Downloading LFS source packages..."
wget --input-file=sources/lfs/wget-list --continue --directory-prefix=sources/lfs

echo "[DewOS] Checking md5..."
cd sources/lfs
md5sum -c md5sums
