# DewOS

DewOS is a small experimental Linux-based OS project built for learning and experimentation. It features a custom C++ `/init`, a bespoke shell, a TUI installer, GRUB boot support, QEMU testing, and a minimal package manager called `drop`.

## Project Structure

```
DewOS/
├── app/                  # Custom init, shell, installer and runtime files
│   ├── init/             # C++ /init process source
│   ├── drop/             # Custom package manager source
│   ├── installer/        # TUI dew-install application
│   └── include/          # Shared headers
├── assets/               # Boot logos and visual assets
├── config/               # GRUB and system config files
├── configs/kernel/       # Kernel configuration helpers
├── docs/                 # Notes and documentation
├── scripts/              # Fetch, check, build, clean and run scripts
├── thirdparty/           # Small vendored tools kept in Git
├── Makefile
└── README.md
```

## Build Commands

Check host tools and build the kernel (if missing):
```bash
./scripts/check-all.sh
```

Fetch the Linux kernel source:
```bash
./scripts/fetch-src.sh kernel
```

Fetch full downloadable source set:
```bash
./scripts/fetch-src.sh all
```

Build only the initramfs:
```bash
./scripts/build-initramfs.sh
```

Build the bootable ISO:
```bash
make iso
```

Force a kernel rebuild:
```bash
DEW_REBUILD_KERNEL=1 ./scripts/check-all.sh
```

## Run in QEMU

```bash
./scripts/run.sh iso       # Boot live ISO/installer
./scripts/run.sh disk      # Boot installed disk
./scripts/run.sh direct    # Direct kernel/initramfs boot
```

## Clean

```bash
make clean       # Clean build artifacts
make clean-disk  # Clean test disks
make clean-all   # Clean everything
```

## User Preferences

- This is a build/OS project with no web frontend or backend server.
