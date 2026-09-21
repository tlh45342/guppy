# Guppy Filesystems and VFS

Guppy keeps host paths and mounted guest-filesystem paths separate. Filesystems
are accessed through Guppy's own VFS and block layers; the host operating
system does not need to mount the guest filesystem.

## Namespace examples

- `ls`, `cd`, `pwd`, `cat`, `stat`, `chmod`, `rm`, `cp`, and `hexdump` operate
  on Guppy VFS paths.
- `lls` and `lcat` operate on local/host paths.
- `lcp` copies local/host -> Guppy VFS.
- `write` copies a local file directly to a bounded block device/partition.

## EXT2

The EXT2 implementation is intended for actual image construction, not merely
inspection.

Current regular-file addressing supports 12 direct pointers, single-indirect,
and double-indirect blocks. Triple-indirect addressing is not implemented.

Implemented work includes multi-group formatting/allocation, directories,
regular-file create/read/write, overwrite through truncate semantics, removal
and block/inode reclamation, chmod, UID/GID ownership, and persistent metadata.

## ISO9660 / Rock Ridge

ISO9660 is read-only. Guppy prefers Rock Ridge metadata when present. Current
SUSP/Rock Ridge records include `SP`, `NM`, `PX`, `TF`, `CE`, and `SL`.

The Rock Ridge decoder is shared by lookup and enumeration so names shown by
`ls` are the names accepted by other VFS consumers.

## FAT/VFAT

Formatting support exists. FAT/VFAT mounting is not currently complete and
should not be described as supported read/write VFS access.
