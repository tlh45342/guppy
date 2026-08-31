# Changelog
All notable changes to this project will be documented in this file.

The format is based on **Keep a Changelog** and this project aims to follow **Semantic Versioning**.
- Keep a Changelog: https://keepachangelog.com/en/1.1.0/
- SemVer: https://semver.org/spec/v2.0.0.html

## [Unreleased]

## [0.0.34] – 2026-08-27

### Fixed
- `stat` now resolves mounted filesystem paths through `vfs_stat()` instead of
  the host operating system.
- Relative VFS paths now resolve against Guppy's current working directory.
- `cd` validates that its target exists and is a directory before updating cwd.

### Added
- Rock Ridge / SUSP support for ISO9660:
  - `SP` SUSP detection.
  - `NM` POSIX filenames.
  - `PX` POSIX mode, UID, GID, and link count.
  - `TF` timestamps.
  - `CE` continuation areas.
  - `SL` symbolic-link target decoding and VFS `readlink` exposure.
- MIT licensing and author information in project documentation.

### Notes
- Rock Ridge filename decoding is shared by directory enumeration and lookup.
- Large multi-megabyte EXT2 writes remain a later correctness/performance round.


## [0.0.31] – 2026-08-27

- `create` now prefers the compact form `create <image> <size>`, for example
  `create target.img 64M`.
- Existing `create <image> --size <size>` and `--size=<size>` forms remain
  supported for compatibility.
- Centralized size semantics used by `create` and GPT byte-size specifications:
  - `K`, `M`, `G` are binary convenience units (KiB/MiB/GiB).
  - `KiB`, `MiB`, `GiB` are explicit IEC binary units.
  - `KB`, `MB`, `GB` are decimal SI units.
  - plain numbers are bytes.
- GPT retains `s` for explicit sector/LBA values and `%` for percentage-based
  placement/sizing.
- Added size-syntax documentation and a small script probe.


## [0.0.30] – 2026-08-26
### Added
- Persistent EXT2 directory creation through the mounted VFS, including nested `mkdir -p`.
- Persistent lookup and directory listing for nested EXT2 directories.
- Nested EXT2 file creation, so VFS `cp`/`echo` can write files below the filesystem root.

### Changed
- EXT2 mutation is now tied to the resolved backing image/partition (`key` + filesystem offset) instead of a path-only global helper.
- Removed the temporary in-memory EXT2 directory registry; directory visibility now comes from on-disk EXT2 structures.

### Notes
- The current EXT2 writer remains intentionally small: regular-file writes are limited to one filesystem block (1 KiB with the current formatter). Multi-block/indirect file support is a later phase.

### Added
- **Local host utilities**:
  - `lls` – local directory listing (`lls [-l] [-a] [path]`).
  - `lcat` – local file viewer (`lcat <file>`).
  - `stat` – local file stat shim (`stat <path>`).
- **Debugging**:
  - Runtime debug flag dump (e.g., `debug: flags=0x... [iso|vfs]`).
  - `DBG_STMT`/`DBG_PRINT` patterns to gate verbose output per-category.
  - Extra DBG in `vblk_open()` to trace lookups and rejection reasons.
- **Mount UX**:
  - `mount` with **no arguments** now lists the current mount table (prints a “no mounts” message when empty).

### Changed
- **ISO9660 is always registered** at init (no `-DVFS_ISO9660` build flag required).
- `cmd_use` normalized device naming:
  - Internal key uses **basename** (e.g., `b`).
  - Display keeps **/dev path** (e.g., `/dev/b`).
  - Parent device gets a real size (`img_bytes/512`) so raw, partitionless mounts work.
- `cmd_mount` refactor:
  - Clean auto-probe path; retries `vblk_open` without `/dev/` prefix.
  - On success, defers ownership to VFS; on failure, closes the handle.
- **Documentation**:
  - `manifest.txt` promoted to **`manifest.md`** (human-readable index).
  - `README.md` updated to document new commands and the “mount with no args” behavior.

### Fixed
- Implemented `vblk_open()` (previously a stub returning `NULL`) so mounts can succeed.
- `lls` now includes a proper `readlink()` declaration by defining `_POSIX_C_SOURCE` before headers.
- Command registry entry for `lcat` points to `cmd_lcat` (not `cmd_cat`).

### Notes / Migration
- Build with `-DDEBUG` to enable DBG output; at runtime use `debug all on|off` (and per-category toggles like `debug iso on`).
- ISO9660 mounts on whole devices (no partitions) will now auto-probe and mount as expected.

---

## [0.0.25] – 2025-09-?? (baseline)
Initial public baseline visible in logs (`Guppy 0.0.25`).  
Core features: REPL, device attach (`use`), partition scan (MBR/GPT), basic VFS, `mount`, `ls`, `cat`, and basic filesystem tooling.

## 0.0.33 - raw partition write

- Added `write <dev> <host-file> [offset]`.
- Writes a host file directly into a registered Guppy block device or partition.
- The optional offset is relative to the selected device/partition and uses Guppy's common size syntax.
- Bounds checks prevent a write from extending beyond the selected partition.
- Intended first use: placing boot-stage payloads in a GPT BIOS Boot Partition without teaching Guppy about any particular bootloader.

## 0.0.42 - housekeeping

- Cleaned stale shim/placeholder comments that no longer described current VFS/EXT2 behavior.
- Added `FUTURE.md` to collect deliberate deferred work and known limitations.
- Documented the remaining group-0 assumption in the compact VFS-side EXT2 metadata reader.
- Changed the unused `del_disk()` placeholder from false success to explicit failure until device removal is implemented.
- Added the missing `<sys/types.h>` includes needed by the existing 64-bit file-offset code on Linux.
- No new filesystem or user command functionality is introduced by this housekeeping pass.
