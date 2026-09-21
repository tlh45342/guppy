# Guppy Project Status

## 0.0.44

0.0.44 adds functional offline `useradd` and `passwd`, EXT2 UID/GID ownership
persistence, consolidated package regression coverage, fixed/full VDI,
dynamic/sparse VDI allocation-on-write, and the first `diskq` inspection and
validation framework.

The default development regression suite currently contains 17 groups.

## 0.0.43

0.0.43 was a maintenance checkpoint for regression and application-output
infrastructure. It extended scripted disk/GPT reporting, corrected GPT
detection in `parted -l`, and made root `make clean` clean generated test
artifacts.

## 0.0.42

0.0.42 consolidated VFS/EXT2 repairs and introduced the first `libvdisk`
container work. RAW access moved through the virtual-disk layer and fixed VDI
create/open/read/write/flush support landed. A Guppy-created fixed VDI was
independently boot-tested under VirtualBox.

## Independent filesystem interoperability

A separately written x86 BIOS bootloader has parsed a Guppy-created GPT disk,
located its EXT2 root filesystem, traversed `/boot`, resolved direct,
single-indirect, and double-indirect blocks, and read file data. The same path
has located a Debian kernel and initrd stored by Guppy and validated the Linux
kernel boot header.

This provides an independent check that Guppy's on-disk structures are usable
outside Guppy itself.

## Current direction

Near-term work is centered on:

- sparse VDI torture and external interoperability;
- fixed VHD interoperability/hardening;
- simple/full VMDK before sparse VMDK;
- MBR/legacy partition completion;
- FAT/VFAT mounting;
- metadata/error hardening;
- package dependency/order work toward more complete offline Debian images.

Deliberately deferred features and known limitations belong in `FUTURE.md`.
