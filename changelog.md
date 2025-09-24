# Changelog
All notable changes to this project will be documented in this file.

The format is based on **Keep a Changelog** and this project aims to follow **Semantic Versioning**.
- Keep a Changelog: https://keepachangelog.com/en/1.1.0/
- SemVer: https://semver.org/spec/v2.0.0.html

## [Unreleased]
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
Core features: REPL, device attach (`use`), partition scan (MBR/GPT), basic VFS, `mount`, `ls`, `ca