# DewOS

**DewOS** is a small experimental Linux-based OS project.

It is built around a custom C++ `/init`, a small shell, an initramfs live environment, a TUI installer, GRUB boot support, QEMU testing, and a minimal installed root filesystem.

This is not a production-ready OS. It is a learning project, a system experiment, and a slow attempt to make a Linux distribution.

## Status

DewOS is currently in early development.

Working or partially working:

- Custom C++ `/init`
- Basic shell and login flow
- Initramfs live environment
- GRUB ISO boot menu
- TUI installer
- ext4 install target
- QEMU test disk installation
- DHCP networking with `netup`
- Wi-Fi debug/detect/scan shell commands
- BusyBox-based rescue shell
- Simple text editor support through `kilo`

Not ready yet:

- Package manager
- Graphical UI
- Proper service manager
- Stable Wi-Fi support on all real hardware
- Secure production login system

## Project Structure

```txt
DewOS/
├── app/                  # Custom init, shell, installer and runtime files
├── assets/               # Boot logos and visual assets
├── config/               # GRUB and system config files
├── configs/kernel/       # Kernel configuration helpers
├── docs/                 # Notes and documentation
├── scripts/              # Fetch, check, build, clean and run scripts
├── thirdparty/           # Small vendored tools kept in Git
├── Makefile
└── README.md
```

## What Is Not Committed

Generated or downloadable files are intentionally not stored in Git:

- `sources/` - downloaded Linux/LFS sources.
- `build/` - temporary build trees and generated rootfs/initramfs contents.
- `out/` - kernel image, initramfs, ISO, rootfs images, and QEMU test disks.
- `third_party/` - external/restorable dependencies.

These directories can be restored with the scripts below.

## Restore On A New PC

Install/check host tools and build the kernel if it is missing:

```bash
./scripts/check-all.sh
```

Fetch only the Linux kernel source:

```bash
./scripts/fetch-src.sh kernel
```

Fetch the full downloadable source set:

```bash
./scripts/fetch-src.sh all
```

Build only the initramfs:

```bash
./scripts/build-initramfs.sh
```

Build the bootable ISO:

```bash
./scripts/build-iso.sh
```

Force a kernel rebuild:

```bash
DEW_REBUILD_KERNEL=1 ./scripts/check-all.sh
```

## Run In QEMU

Reset the QEMU test disk:

```bash
./scripts/run.sh reset-disk
```

Boot the live ISO/installer:

```bash
./scripts/run.sh iso
```

After installing DewOS to `/dev/vda`, boot the installed disk:

```bash
./scripts/run.sh disk
```

Direct kernel/initramfs boot for quick debugging:

```bash
./scripts/run.sh direct
```

## Clean

Clean generated build artifacts:

```bash
./scripts/clear.sh build
```

Clean test disks:

```bash
./scripts/clear.sh disk
```

Clean all generated files:

```bash
./scripts/clear.sh all
```
