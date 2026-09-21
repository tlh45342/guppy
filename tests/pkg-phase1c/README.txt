Phase 1C - real Debian base-files install + EXT2 fast symlinks

Run from O:\guppy\tests\pkg-phase1c:
    guppy.exe install.script

The test creates pkg-target.img, formats EXT2, installs base-files directly
from the mounted Debian ISO, then probes files/symlinks.

Important scope:
- EXT2 fast symlinks (targets <= 60 bytes) are implemented.
- Debian base-files' observed symlink targets all fit this representation.
- Long/block-backed EXT2 symlinks remain a later filesystem enhancement.
- TAR type '2' now installs through vfs_symlink().
