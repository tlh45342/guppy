# Guppy Future Work

This file records deliberate limitations and deferred features. Items here are
not implied to be bugs in the current supported workflow.

## Filesystem and VFS

- **EXT2 VFS metadata reader across later inode groups.** The compact metadata
  reader in `src/vfs_ext2.c` currently caches the inode table from block group
  0. Make its inode lookup group-aware before relying on VFS metadata operations
  for inodes allocated in later groups.
- **EXT2 triple-indirect file blocks.** Direct, single-indirect, and
  double-indirect addressing are implemented. Triple-indirect addressing is
  deferred until a workload requires it.
- **Recursive VFS copy (`cp -r`).** Regular-file copy is supported; recursive
  directory copy is not yet implemented.
- Review and eventually remove the EXT2 mount-session directory-cache fallback
  once all creation/lookup paths are proven to rely solely on persistent disk
  metadata.

## Disk and Partition Commands

- `mbr`, `part`, and `format` remain legacy command placeholders. Their final
  behavior should either be implemented deliberately or the commands should be
  retired in favor of the working GPT/mkfs command families.
- Add GPT partition-overlap rejection to `gpt add`.
- `del_disk()` does not yet unregister a disk/device tree. No current command
  path depends on it, but it must be implemented before device removal is
  exposed as a supported operation.

## Filesystems and Image Formats

- The NTFS formatter is probe/bootstrap scaffolding, not a complete NTFS
  filesystem creator. A complete implementation requires real NTFS metadata
  records and attributes.
- Continue broader FAT/VFAT, NTFS, filesystem, and virtual-disk format support
  as project requirements call for it.
