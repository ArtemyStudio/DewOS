#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

KVER="${KVER:-$(cat sources/kernel/current-version 2>/dev/null || echo 6.12.84)}"
KDIR="sources/kernel/linux-${KVER}"
KERNEL_OUT="out/bzImage"

KERNEL_CONFIGS=(
  BLK_DEV_INITRD DEVTMPFS DEVTMPFS_MOUNT TMPFS PROC_FS SYSFS
  TTY VT VT_CONSOLE HW_CONSOLE UNIX98_PTYS
  INPUT INPUT_EVDEV INPUT_KEYBOARD KEYBOARD_ATKBD SERIO SERIO_I8042
  INPUT_MOUSE MOUSE_PS2 USB_SUPPORT USB_HID HID_GENERIC
  FB FB_VESA FRAMEBUFFER_CONSOLE DRM DRM_SIMPLEDRM DRM_FBDEV_EMULATION SYSFB SYSFB_SIMPLEFB
)

PACKAGES=(
  build-essential bc bison flex libelf-dev libssl-dev
  xz-utils wget cpio gzip grub-pc-bin grub-common xorriso mtools
  qemu-system-x86 busybox-static e2fsprogs util-linux fdisk imagemagick
)

COMMANDS=(
  g++ gcc make wget tar xz cpio gzip
  grub-mkrescue qemu-system-x86_64 busybox mkfs.ext4 convert
)

missing_command() {
  for cmd in "$@"; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
      echo "$cmd"
    fi
  done
}

missing_package() {
  command -v dpkg >/dev/null 2>&1 || return 0

  for pkg in "$@"; do
    if ! dpkg -s "$pkg" >/dev/null 2>&1; then
      echo "$pkg"
    fi
  done
}

install_packages() {
  if ! command -v apt-get >/dev/null 2>&1; then
    echo "[DewOS] Missing tools, and apt-get was not found."
    echo "[DewOS] Install these packages manually:"
    printf '  %s\n' "${PACKAGES[@]}"
    exit 1
  fi

  echo "[DewOS] Installing build packages..."
  sudo apt-get update
  sudo apt-get install -y "${PACKAGES[@]}"
}

build_kernel() {
  if [ ! -d "$KDIR" ]; then
    ./scripts/fetch-src.sh kernel
  fi

  echo "[DewOS] Building Linux ${KVER}..."
  mkdir -p out

  (
    cd "$KDIR"
    make mrproper
    make x86_64_defconfig

    if [ -x scripts/config ]; then
      for opt in "${KERNEL_CONFIGS[@]}"; do
        scripts/config --enable "$opt"
      done

      scripts/config \
        --enable SERIAL_8250 \
        --enable SERIAL_8250_CONSOLE \
        --enable PRINTK \
        --disable RANDOMIZE_BASE \
        --disable RANDOMIZE_MEMORY \
        --disable RANDOMIZE_KSTACK_OFFSET
    fi

    make olddefconfig
    make -j"$(nproc)" bzImage
  )

  cp "$KDIR/arch/x86/boot/bzImage" "$KERNEL_OUT"
  echo "[DewOS] Kernel ready: $KERNEL_OUT"
}

kernel_config_ready() {
  [ -f "$KDIR/.config" ] || return 1

  for opt in "${KERNEL_CONFIGS[@]}"; do
    if ! grep -q "^CONFIG_${opt}=y" "$KDIR/.config"; then
      return 1
    fi
  done

  return 0
}

MISSING_COMMANDS="$(missing_command "${COMMANDS[@]}")"
MISSING_PACKAGES="$(missing_package "${PACKAGES[@]}")"

if [ -n "$MISSING_COMMANDS" ] || [ -n "$MISSING_PACKAGES" ]; then
  echo "[DewOS] Missing commands:"
  printf '%s\n' "${MISSING_COMMANDS:-none}"

  echo "[DewOS] Missing packages:"
  printf '%s\n' "${MISSING_PACKAGES:-unknown, checking by command only}"

  install_packages
fi

if [ ! -d "$KDIR" ]; then
  echo "[DewOS] Kernel source is missing."
  ./scripts/fetch-src.sh kernel
fi

if [ ! -f "$KERNEL_OUT" ] || ! kernel_config_ready; then
  build_kernel
fi

echo "[DewOS] Host tools and kernel are ready."
