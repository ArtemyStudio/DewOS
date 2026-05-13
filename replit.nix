{pkgs}: {
  deps = [
    pkgs.patchelf
    pkgs.perl
    pkgs.openssl
    pkgs.bc
    pkgs.bison
    pkgs.flex
    pkgs.elfutils
    pkgs.qemu_kvm
    pkgs.qemu
    pkgs.mtools
    pkgs.xorriso
    pkgs.dosfstools
    pkgs.busybox
    pkgs.grub2
    pkgs.cpio
    pkgs.wget
  ];
}
