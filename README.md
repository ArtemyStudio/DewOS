# DewOS

**DewOS** is a simple Linux-based operating system project.

It is built around a custom C++ `/init`, a small shell, an initramfs-based installer, GRUB boot support, QEMU testing, and a minimal installed root filesystem.

This is not a production-ready OS. It is a learning project, a system experiment, and a slow attempt to make Linux distribution.

---

## Status

DewOS is currently in early development.

### Working / partially working

- Custom C++ `/init`
- Basic shell prompt
- Root and user login logic
- Basic commands:
  - `help`
  - `clear`
  - `ls`
  - `cd`
  - `pwd`
  - `cat`
  - `touch`
  - `mkdir`
  - `mount`
  - `unmount`
  - `about`
  - `sh`
  - `install`
  - `rm`
  - `ps`
  - `dewfetch`
  - `reboot`
  - `poweroff`
- Initramfs live environment
- ext4 installation target
- Basic installer
- QEMU test disk installation
- DHCP networking with `netup`
- ISO generation through GRUB
- BusyBox-based rescue shell
- Simple text editor support through `kilo`

### Not ready yet

- Real hardware installation
- Full bootloader installation to disk
- Package manager
- Graphical UI
- Proper service manager
- Stable Wi-Fi support on real hardware
- Secure production login system

---

## Project structure

```txt
DewOS/
├── app/
│   ├── init/              # Custom C++ init and shell
│   └── installer/         # Initramfs installer
├── assets/                # Boot logos and visual assets
├── config/                # GRUB and system config files
├── configs/kernel/        # Kernel configuration helpers
├── docs/                  # Notes and documentation
├── scripts/               # Build, run, clean and fetch scripts
├── sources/               # Downloaded source archives
├── thirdparty/           # External small tools
├── Makefile
└── README.md
