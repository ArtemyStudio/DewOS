#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

ROOT="build/rootfs"
INIT_BIN="$ROOT/init"
INSTALLER_SRC="app/installer/dew-install-initramfs.sh"
INSTALLER_BIN="$ROOT/sbin/dew-install"
INITRAMFS="out/initramfs.cpio.gz"

find_busybox() {
  if [ -x /bin/busybox ]; then
    echo /bin/busybox
  elif [ -x /usr/bin/busybox ]; then
    echo /usr/bin/busybox
  else
    echo "[DewOS] Missing busybox. Run: ./scripts/check-all.sh" >&2
    exit 1
  fi
}

copy_bin_with_libs() {
  local bin="$1"
  local dest="$ROOT/bin/$(basename "$bin")"

  case "$bin" in
    /usr/sbin/*|/sbin/*) dest="$ROOT/sbin/$(basename "$bin")" ;;
  esac

  [ -x "$bin" ] || return 0

  cp "$bin" "$dest"
  chmod +x "$dest"

  ldd "$bin" 2>/dev/null | while read -r line; do
    local lib=""

    case "$line" in
      *"=>"*) lib="$(printf '%s\n' "$line" | awk '{print $3}')" ;;
      /*) lib="$(printf '%s\n' "$line" | awk '{print $1}')" ;;
    esac

    if [ -n "$lib" ] && [ -f "$lib" ]; then
      mkdir -p "$ROOT$(dirname "$lib")"
      cp "$lib" "$ROOT$lib" || true
    fi
  done
}

echo "[DewOS] Building initramfs tree..."
rm -rf "$ROOT"
mkdir -p "$ROOT"/{bin,sbin,etc,proc,sys,dev,tmp,root,home,mnt,var,run,usr/bin,usr/sbin,lib,lib64}
mkdir -p out

echo "[DewOS] Building /init..."
g++ -std=c++20 -Wall -Wextra -O2 -static \
  app/init/init.cpp app/init/shell.cpp app/init/system.cpp \
  -o "$INIT_BIN" -lcrypt
chmod +x "$INIT_BIN"

echo "[DewOS] Adding BusyBox..."
cp "$(find_busybox)" "$ROOT/bin/busybox"
chmod +x "$ROOT/bin/busybox"

for app in \
  ash sh echo printf cat head tail ls pwd mkdir rmdir rm cp mv touch chmod chown \
  grep sed awk find xargs test true false clear reset sleep date uname hostname \
  whoami id ps kill dmesg free top mount umount sync df du ifconfig ip route \
  ping wget udhcpc vi hexdump cut sort uniq wc tr basename dirname reboot poweroff halt
do
  ln -sf /bin/busybox "$ROOT/bin/$app" 2>/dev/null || true
  ln -sf /bin/busybox "$ROOT/sbin/$app" 2>/dev/null || true
done

echo "[DewOS] Adding installer tools..."
for cmd in \
  /usr/bin/lsblk /usr/sbin/fdisk /usr/sbin/partprobe /usr/sbin/blkid \
  /usr/sbin/mkfs.ext4 /usr/bin/udevadm /usr/bin/openssl \
  /usr/sbin/wpa_supplicant /usr/bin/wpa_passphrase /usr/sbin/iw /usr/sbin/rfkill
do
  copy_bin_with_libs "$cmd"
done

if [ -f "$INSTALLER_SRC" ]; then
  cp "$INSTALLER_SRC" "$INSTALLER_BIN"
  chmod +x "$INSTALLER_BIN"
fi

if [ -f assets/boot-logo.png ] || [ -f assets/boot-logo.ppm ]; then
  echo "[DewOS] Adding boot logo..."
  LOGO_SOURCE="assets/boot-logo.png"

  if [ ! -f "$LOGO_SOURCE" ]; then
    LOGO_SOURCE="assets/boot-logo.ppm"
  fi

  if head -c 2 "$LOGO_SOURCE" | grep -q "P6"; then
    cp "$LOGO_SOURCE" "$ROOT/etc/dewos-logo.ppm"
  elif command -v convert >/dev/null 2>&1; then
    convert "$LOGO_SOURCE" -resize 360x360 -strip ppm:"$ROOT/etc/dewos-logo.ppm"
  else
    echo "[DewOS] Warning: boot logo is not PPM P6 and convert is missing"
  fi
fi

if [ -d app/installer/initramfs-files ]; then
  echo "[DewOS] Adding initramfs files..."
  cp -a app/installer/initramfs-files/. "$ROOT"/
fi

cat > "$ROOT/etc/resolv.conf" <<'CONFIG'
nameserver 1.1.1.1
nameserver 8.8.8.8
CONFIG

cat > "$ROOT/etc/hostname" <<'CONFIG'
dewos-live
CONFIG

cat > "$ROOT/etc/passwd" <<'CONFIG'
root:x:0:0:root:/root:/bin/sh
CONFIG

cat > "$ROOT/etc/group" <<'CONFIG'
root:x:0:
CONFIG

cat > "$ROOT/etc/profile" <<'CONFIG'
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
export HOME=/root
export TERM=linux
CONFIG

chmod 1777 "$ROOT/tmp"

if [ "${INCLUDE_FIRMWARE:-0}" = "1" ]; then
  echo "[DewOS] Adding firmware..."
  mkdir -p "$ROOT/lib/firmware"

  if [ -d /lib/firmware ]; then
    cp -a /lib/firmware/iwlwifi-* "$ROOT/lib/firmware/" 2>/dev/null || true
    cp -a /lib/firmware/rtlwifi "$ROOT/lib/firmware/" 2>/dev/null || true
    cp -a /lib/firmware/ath10k "$ROOT/lib/firmware/" 2>/dev/null || true
    cp -a /lib/firmware/ath11k "$ROOT/lib/firmware/" 2>/dev/null || true
    cp -a /lib/firmware/brcm "$ROOT/lib/firmware/" 2>/dev/null || true
    cp -a /lib/firmware/mediatek "$ROOT/lib/firmware/" 2>/dev/null || true
  fi
else
  mkdir -p "$ROOT/lib/firmware"
  echo "[DewOS] Skipping firmware. Use INCLUDE_FIRMWARE=1 to include it."
fi

echo "[DewOS] Packing $INITRAMFS..."
(
  cd "$ROOT"
  find . -print0 | cpio --quiet --null -o --format=newc | gzip -9
) > "$INITRAMFS"

echo "[DewOS] Initramfs ready: $INITRAMFS"
