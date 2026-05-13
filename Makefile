.PHONY: all iso initramfs kernel pkgs check fetch run run-direct run-disk clean clean-disk clean-all rebuild

all: iso

check:
	./scripts/check-all.sh

fetch:
	./scripts/fetch-src.sh all

fetch-kernel:
	./scripts/fetch-src.sh kernel

kernel:
	DEW_REBUILD_KERNEL=1 ./scripts/check-all.sh

pkgs:
	./scripts/build-pkgs.sh

initramfs:
	./scripts/build-initramfs.sh

iso: check initramfs
	./scripts/build-iso.sh

run:
	./scripts/run.sh iso

run-disk:
	./scripts/run.sh disk

run-direct:
	./scripts/run.sh direct

rebuild: clean iso

clean:
	./scripts/clear.sh build

clean-disk:
	./scripts/clear.sh disk

clean-all:
	./scripts/clear.sh all