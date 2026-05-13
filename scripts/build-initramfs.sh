#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

ROOT="build/rootfs"
INIT_BIN="$ROOT/init"
INSTALLER_SRC="app/installer/dew-install.cpp"
INSTALLER_BIN="$ROOT/sbin/dew-install"
DROP_SRC="app/drop/drop.cpp"
DROP_BIN="$ROOT/bin/drop"
INITRAMFS="out/initramfs.cpio.gz"

find_busybox() {
  if [ -x /bin/busybox ]; then
    echo /bin/busybox
  elif [ -x /usr/bin/busybox ]; then
    echo /usr/bin/busybox
  elif command -v busybox >/dev/null 2>&1; then
    command -v busybox
  else
    echo "[DewOS] Missing busybox. Run: ./scripts/check-all.sh" >&2
    exit 1
  fi
}

patch_bin_libs() {
  local dest="$1"
  [ -x "$dest" ] || return 0

  mkdir -p "$ROOT/lib" "$ROOT/lib64"

  (ldd "$dest" 2>/dev/null || true) | while read -r line; do
    local lib=""

    case "$line" in
      *"=>"*) lib="$(printf '%s\n' "$line" | awk '{print $3}')" ;;
      /*) lib="$(printf '%s\n' "$line" | awk '{print $1}')" ;;
    esac

    if [ -n "$lib" ] && [ "$lib" != "not" ] && [ -f "$lib" ]; then
      local libname
      libname="$(basename "$lib")"
      cp -n "$lib" "$ROOT/lib/$libname" 2>/dev/null || true
      if [ ! -e "$ROOT/lib64/$libname" ]; then
        ln -sf "/lib/$libname" "$ROOT/lib64/$libname" 2>/dev/null || true
      fi
    fi
  done

  if command -v patchelf >/dev/null 2>&1; then
    local interp
    interp="$(patchelf --print-interpreter "$dest" 2>/dev/null || true)"
    if [ -n "$interp" ]; then
      local interp_name
      interp_name="$(basename "$interp")"
      if [ -f "$ROOT/lib/$interp_name" ]; then
        patchelf --set-interpreter "/lib/$interp_name" "$dest" 2>/dev/null || true
      fi
    fi
    patchelf --set-rpath "/lib:/lib64" "$dest" 2>/dev/null || true
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

  patch_bin_libs "$dest"
}

echo "[DewOS] Building initramfs tree..."
rm -rf "$ROOT"
mkdir -p "$ROOT"/{bin,sbin,etc,proc,sys,dev,tmp,root,home,mnt,var,run,usr/bin,usr/sbin,lib,lib64}
mkdir -p "$ROOT"/{bin,sbin,etc,proc,sys,dev,tmp,root,home,mnt,var,run,usr/bin,usr/sbin,lib,lib64}
chmod 755 "$ROOT"/bin "$ROOT"/sbin "$ROOT"/usr/bin "$ROOT"/usr/sbin
mkdir -p "$ROOT/boot"
mkdir -p "$ROOT/var/drop/repo" "$ROOT/var/drop/installed"
mkdir -p out

echo "[DewOS] Building /init..."
g++ -std=c++20 -Wall -Wextra -O2 \
  app/init/init.cpp app/init/shell.cpp app/init/system.cpp app/init/network.cpp \
  -o "$INIT_BIN"
chmod +x "$INIT_BIN"
copy_bin_with_libs "$INIT_BIN"

echo "[DewOS] Adding BusyBox..."
cp "$(find_busybox)" "$ROOT/bin/busybox"
chmod +x "$ROOT/bin/busybox"

for app in \
  ash sh echo printf cat head tail ls pwd mkdir rmdir rm cp mv touch chmod chown \
  grep sed awk find xargs test true false clear reset sleep date uname hostname \
  whoami id ps kill dmesg free top mount umount sync df du ifconfig ip route \
  blockdev mknod stty tar gzip gunzip \
  ping wget udhcpc vi hexdump cut sort uniq wc tr basename dirname reboot poweroff halt \
  wipefs
do
  ln -sf /bin/busybox "$ROOT/bin/$app" 2>/dev/null || true
  ln -sf /bin/busybox "$ROOT/sbin/$app" 2>/dev/null || true
done

echo "[DewOS] Adding installer tools..."
for cmd in \
  /usr/bin/lsblk /usr/sbin/sfdisk /usr/sbin/fdisk /usr/sbin/partprobe /usr/sbin/blkid \
  /usr/sbin/mkfs.ext4 /usr/sbin/mkfs.vfat /usr/bin/udevadm /usr/bin/openssl \
  /usr/sbin/wpa_supplicant /usr/bin/wpa_passphrase /usr/sbin/iw /usr/sbin/rfkill
do
  copy_bin_with_libs "$cmd"
done

for cmd in /usr/bin/grub-* /usr/sbin/grub-*; do
  copy_bin_with_libs "$cmd"
done

echo "[DewOS] Building TUI installer (dew-install)..."
if [ -f "$INSTALLER_SRC" ]; then
  g++ -std=c++20 -Wall -Wextra -O2 \
    "$INSTALLER_SRC" -o "$INSTALLER_BIN"
  chmod +x "$INSTALLER_BIN"
  patch_bin_libs "$INSTALLER_BIN"
elif [ -f app/installer/dew-install ]; then
  cp app/installer/dew-install "$INSTALLER_BIN"
  chmod +x "$INSTALLER_BIN"
  patch_bin_libs "$INSTALLER_BIN"
else
  echo "[DewOS] WARNING: no installer source or binary found" >&2
fi

echo "[DewOS] Building drop package manager..."
if [ -f "$DROP_SRC" ]; then
  g++ -std=c++20 -Wall -Wextra -O2 "$DROP_SRC" -o "$DROP_BIN"
  chmod +x "$DROP_BIN"
  patch_bin_libs "$DROP_BIN"
fi

if [ -x scripts/build-pkgs.sh ]; then
  echo "[DewOS] Building drop packages..."
  ./scripts/build-pkgs.sh || echo "[DewOS] WARNING: build-pkgs.sh had errors, continuing"

  if [ -d build/repo ] && [ -n "$(ls -A build/repo 2>/dev/null)" ]; then
    echo "[DewOS] Copying drop repo into initramfs..."
    cp -a build/repo/. "$ROOT/var/drop/repo/"
  fi
fi

if [ -d build/rootfs-disk ]; then
  echo "[DewOS] Adding installable rootfs template..."
  mkdir -p "$ROOT/rootfs-disk"
  (
    cd build/rootfs-disk
    find . -path ./dev -prune -o -print0 | cpio --quiet --null -pdm "../../$ROOT/rootfs-disk"
  )
  mkdir -p "$ROOT/rootfs-disk/dev"
fi

if [ -f out/bzImage ]; then
  cp out/bzImage "$ROOT/boot/vmlinuz-dewos"
fi

if [ -d /usr/lib/grub ]; then
  echo "[DewOS] Adding GRUB runtime..."
  mkdir -p "$ROOT/usr/lib"
  cp -a /usr/lib/grub "$ROOT/usr/lib/"
fi

if [ -d /usr/share/grub ]; then
  mkdir -p "$ROOT/usr/share"
  cp -a /usr/share/grub "$ROOT/usr/share/"
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
