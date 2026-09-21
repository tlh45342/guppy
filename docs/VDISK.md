# Guppy Virtual Disk Containers

Guppy separates host-side image containers from logical block-device access
through `libvdisk`. Partition and filesystem code should not need to know
whether its logical disk is backed by RAW, VDI, fixed VHD, or a future
VMDK/QCOW2 backend.

## Current support

### RAW

RAW is the simplest backing format and remains the default for ordinary image
names. Because RAW has no identifying container header, it cannot be positively
identified from bytes alone.

### VDI

Guppy supports VDI 1.1 fixed/full images and dynamic/sparse images.

Fixed/full images have direct logical-to-data-region mapping. Dynamic images
maintain a block map and allocate 1 MiB physical blocks on first write.
Unallocated guest reads return zero. Newly allocated blocks are zeroed before
the mapping is published.

A Guppy-created fixed VDI has been partitioned with GPT, populated with boot
code, attached directly to VirtualBox, and successfully BIOS-booted.

Sparse VDI is covered by regression tests for creation, allocation, filesystem
use, close/reopen persistence, and host-file growth. Additional VirtualBox
interoperability testing is desirable.

### VHD

Guppy supports the legacy fixed/full VHD representation. Guest byte zero maps
directly to host byte zero and the 512-byte VHD footer follows the complete
guest disk payload. Creation emits the standard `conectix` footer, big-endian
metadata, CHS compatibility geometry, UUID, and footer checksum.

Fixed VHD is covered by a regression that creates GPT + EXT2 through libvdisk,
closes and reopens the container, verifies file persistence, and audits both the
VHD footer and the contained GPT through `diskq`. Dynamic and differencing VHD
images remain recognition-only.

## diskq

`diskq` is a read-only inspection command:

```text
diskq image.vdi
diskq random.dsk
```

It does not trust the filename extension for formats with recognizable
signatures.

For VDI it reports container type, logical and host size, sector/block sizes,
block counts, allocation-map statistics, metadata offsets, invalid map entries,
duplicate mappings, and structural sanity checks. It then separately reports
the partition layer when logical block access is available.

This separation is intentional: a valid container may contain a bad partition
table, while a corrupt container may contain bytes that merely resemble one.

Dynamic/differencing VHD, VMDK descriptor/sparse images, and QCOW2 are
recognized but do not yet have logical block backends. Recognition is not the
same as backend support or full validation.

## Direction

The intended progression is to prove the simplest/full representation of a
container before adding its dynamic/sparse form:

1. VDI fixed/full -- implemented
2. VDI dynamic/sparse -- implemented; hardening continues
3. VHD fixed -- implemented
4. simple/full VMDK
5. dynamic VHD
6. sparse VMDK
7. QCOW2 when its metadata complexity is justified

Snapshots, differencing images, compaction, and other advanced container
features are not current goals.
