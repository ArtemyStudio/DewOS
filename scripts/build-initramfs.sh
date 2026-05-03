#!/usr/bin/env bash
set -e

cd "$(dirname "$0")/.."

INIT_CPP="app/init/init.cpp"
INIT_BIN="build/rootfs/init"

INSTALLER_SRC="app/installer/dew-install-initramfs.sh"
INSTALLER_BIN="build/rootfs/sbin/dew-install"

ROOT="build/rootfs"

find_busybox() {
  if [ -x /bin/busybox ]; then
    echo /bin/busybox
  elif [ -x /usr/bin/busybox ]; then
    echo /usr/bin/busybox
  else
    echo "[DewOS] Missing busybox. Run: sudo apt install busybox-static" >&2
    exit 1
  fi
}

copy_bin_with_libs() {
  local bin="$1"
  local dest

  if [ ! -x "$bin" ]; then
    echo "[DewOS] Missing binary: $bin"
    return 1
  fi

  case "$bin" in
    /usr/sbin/*|/sbin/*) dest="$ROOT/sbin/$(basename "$bin")" ;;
    *) dest="$ROOT/bin/$(basename "$bin")" ;;
  esac

  echo "[DewOS] Copying $(basename "$bin")..."
  cp "$bin" "$dest"
  chmod +x "$dest"

  ldd "$bin" 2>/dev/null | while read -r line; do
    lib=""

    case "$line" in
      *"=>"*)
        lib="$(echo "$line" | awk '{print $3}')"
        ;;
      /*)
        lib="$(echo "$line" | awk '{print $1}')"
        ;;
    esac

    if [ -n "$lib" ] && [ -f "$lib" ]; then
      mkdir -p "$ROOT$(dirname "$lib")"
      cp -n "$lib" "$ROOT$lib" || true
    fi
  done

  interp="$(ldd "$bin" 2>/dev/null | grep -o '/lib[^ ]*ld-linux[^ ]*' | head -n1 || true)"
  if [ -n "$interp" ] && [ -f "$interp" ]; then
    mkdir -p "$ROOT$(dirname "$interp")"
    cp -n "$interp" "$ROOT$interp" || true
  fi
}

require_file() {
  if [ ! -f "$1" ]; then
    echo "[DewOS] Missing $1"
    exit 1
  fi
}

require_file "$INIT_CPP"
require_file "$INSTALLER_SRC"

BUSYBOX="$(find_busybox)"

rm -rf "$ROOT"
mkdir -p "$ROOT"/{bin,sbin,etc,proc,sys,dev,tmp,root,home,mnt,var,run,usr/bin,usr/sbin,lib,lib64}
mkdir -p out

echo "[DewOS] Building /init..."
g++ -std=c++20 -Wall -Wextra -O2 -static "$INIT_CPP" -o "$INIT_BIN" -lcrypt
chmod +x "$INIT_BIN"

echo "[DewOS] Adding BusyBox..."
cp "$BUSYBOX" "$ROOT/bin/busybox"
chmod +x "$ROOT/bin/busybox"

for app in \
  ash sh echo printf cat head tail \
  ls pwd mkdir rmdir rm cp mv touch chmod chown \
  grep sed awk find xargs test true false \
  clear reset sleep date uname hostname whoami id \
  ps kill dmesg free top \
  mount umount sync df du \
  ifconfig ip route ping wget udhcpc \
  vi hexdump cut sort uniq wc tr basename dirname \
  reboot poweroff halt
do
  ln -sf /bin/busybox "$ROOT/bin/$app" 2>/dev/null || true
  ln -sf /bin/busybox "$ROOT/sbin/$app" 2>/dev/null || true
done

echo "[DewOS] Adding real installer tools..."

for cmd in \
  /usr/bin/lsblk \
  /usr/sbin/fdisk \
  /usr/sbin/partprobe \
  /usr/sbin/blkid \
  /usr/sbin/mkfs.ext4 \
  /usr/bin/udevadm \
  /usr/bin/openssl \
  /usr/sbin/wpa_supplicant \
  /usr/bin/wpa_passphrase \
  /usr/sbin/iw \
  /usr/sbin/rfkill
do
  if [ -x "$cmd" ]; then
    copy_bin_with_libs "$cmd" || true
  else
    echo "[DewOS] Warning: $cmd not found"
  fi
done

echo "[DewOS] Adding /sbin/dew-install..."
cp "$INSTALLER_SRC" "$INSTALLER_BIN"
chmod +x "$INSTALLER_BIN"

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

echo "[DewOS] Adding Wi-Fi firmware if available..."

mkdir -p "$ROOT/lib/firmware"

if [ -d /lib/firmware ]; then
  cp -a /lib/firmware/iwlwifi-* "$ROOT/lib/firmware/" 2>/dev/null || true
  cp -a /lib/firmware/rtlwifi "$ROOT/lib/firmware/" 2>/dev/null || true
  cp -a /lib/firmware/ath10k "$ROOT/lib/firmware/" 2>/dev/null || true
  cp -a /lib/firmware/ath11k "$ROOT/lib/firmware/" 2>/dev/null || true
  cp -a /lib/firmware/brcm "$ROOT/lib/firmware/" 2>/dev/null || true
  cp -a /lib/firmware/mediatek "$ROOT/lib/firmware/" 2>/dev/null || true
fi

echo "[DewOS] Adding initramfs config files..."

if [ -d app/installer/initramfs-files ]; then
  cp -a app/installer/initramfs-files/. "$ROOT"/
fi

echo "[DewOS] Creating initramfs..."
(
  cd "$ROOT"
  find . -print0 | cpio --null -ov --format=newc | gzip -9
) > out/initramfs.cpio.gz

echo "[DewOS] Created out/initramfs.cpio.gz"
