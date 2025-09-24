# Guppy Code Manifest

> High-level map of source files and the major symbols they provide.  
> This is hand-maintained for quick onboarding and code spelunking.

---

## Block Devices & Partition Scanning

- `src/blkdev.c`  
  `add_disk`, `disk_scan_partitions`, `scan_gpt`, `scan_mbr`, `scan_ebr_chain`, `block_rescan`, `del_disk`, `gpt_validate_at`

- `src/blkio.c`  
  `blkio_map_image`, `blk_read_bytes`, `blk_write_bytes`, `find_file_for_abs`

- `src/diskio.c`  
  `diskio_pread`, `diskio_pwrite`, `diskio_size_bytes`, `filesize_bytes`, `map_find_index`, `is_devkey`, `diskio_detach`

- `src/vblk.c`  
  `vblk_register`, `vblk_by_name`/`vblk_open`, `vblk_read_bytes`, `vblk_read_block`, `vblk_resolve_to_base`, `part_bytes_limit`

## Virtual File System (VFS)

- `src/vfs.c`  
  Mount table, router, `vfs_mount`, `vfs_unmount`, `vfs_stat`, `vfs_readdir`, `vfs_read_all`, `vfs_mkdir`, `vfs_write`, iterator helpers

- `src/vfs_init.c`  
  Registers built-in filesystems at startup (ISO9660 is **always** registered; no build flag required)

- Filesystem shims:  
  `src/vfs_iso.c`, `src/vfs_ext2.c`, `src/vfs_fat.c`

## Filesystems

- `src/iso9660.c` — ISO9660 reader: PVD/SVD probe, directory walk, name decoding (Joliet aware), read-by-path  
- `src/ext2.c`, `src/ext2_dir.c` — minimal ext2 reader/writer (WIP)  
- `src/fs_vfat.c`, `src/fat_compat.c` — FAT/VFAT helpers (WIP)

## Command Implementations

- Core:
  `cmd_use.c`, `cmd_mount.c`, `cmd_ls.c`, `cmd_pwd.c`, `cmd_cat.c`, `cmd_mkdir.c`, `cmd_cp.c`, `cmd_do.c`, `cmd_help.c`, `cmd_exit.c`, `cmd_version.c`, `cmd_echo.c`, `cmd_parted.c`, `cmd_part.c`, `cmd_mbr.c`, `cmd_gpt.c`, `cmd_mkfs_ext2.c`, `cmd_mkfs_fat.c`, `cmd_mkfs_vfat.c`, `cmd_mkfs_ntfs.c`

- **Local host helpers (new):**
  - `cmd_lls.c` — list host files in PWD (`lls [-l] [-a] [path]`)
  - `cmd_lcat.c` — print host file (`lcat <file>`)
  - `cmd_stat.c` — show host file metadata (`stat <path>`)

- Registry:
  `cmd_registry.c` — adds `lls`, `lcat`, `stat` to the command table  
  _(ensure `lcat` maps to `cmd_lcat`, not `cmd_cat`)_ :contentReference[oaicite:1]{index=1}

## Utilities

- `src/fileutil.c`, `src/helper.c`, `src/gu_dirent.c`, `src/cwd.c`, `src/mnttab.c`, `src/debug.c` …

---

### Notes
- `mount` with **no args** prints the current mount table (or a “no mounts” message).  
- ISO9660 is **baked in**; no `-D` flag is required at build time.  
- Debugging: compile with `-DDEBUG`, use `debug [iso|vfs|all] on` in the REPL.
