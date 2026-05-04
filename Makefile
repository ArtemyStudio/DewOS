.PHONY: all check fetch kernel initramfs iso run run-direct clean clean-disk clean-all rebuild

all: iso

check:
	./scripts/check-all.sh

fetch:
	./scripts/fetch-src.sh all

kernel:
	./scripts/check-all.sh

initramfs:
	./scripts/build-initramfs.sh

iso:
	./scripts/build-iso.sh

run:
	./scripts/run.sh iso

run-direct:
	./scripts/run.sh direct

rebuild:
	./scripts/clear.sh build
	./scripts/build-iso.sh

clean:
	./scripts/clear.sh build

clean-disk:
	./scripts/clear.sh disk

clean-all:
	./scripts/clear.sh all
