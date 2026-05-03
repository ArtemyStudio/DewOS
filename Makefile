.PHONY: fetch-lfs fetch-kernel kernel initramfs rootfs run run-gui clean clean-builds build rebuild give-perm

fetch-lfs:
	./scripts/fetch-lfs-sources.sh

fetch-kernel:
	./scripts/fetch-kernel.sh

kernel:
	./scripts/build-kernel.sh

initramfs:
	./scripts/build-initramfs.sh

rootfs:
	./scripts/build-rootfs-image.sh

build: rootfs

rebuild: fetch-kernel kernel initramfs rootfs

run:
	./scripts/run-qemu-gui.sh

run-gui:
	./scripts/run-qemu-gui.sh

clean:
	./scripts/clean-builds.sh

clean-builds:
	./scripts/clean-builds.sh

give-perm:
	chmod +x ./scripts/*.sh