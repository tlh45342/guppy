# Guppy

**Guppy** is a tiny, pragmatic tool for tinkering with disk images. It gives you a friendly REPL (plus one-shot CLI) to create images, map them under `/dev/*`, lay down MBR/GPT, mount simple filesystems, and move files around — all without touching real disks.

> ⚠️ **Alpha software** — expect sharp edges. Designed for images, not physical devices.

---

## Highlights

- **Image-first & deterministic**  
  Works on regular files. Prints what it writes. Easy to script and diff.

- **Tiny C11 codebase**  
  Portable, minimal dependencies. Tested on Linux, macOS, and Windows (Cygwin).

- **REPL + scripts**  
  Explore interactively or run repeatable `.script` files.

- **Early VFS with mounts**  
  Mount multiple “devices” at paths (e.g., `/`, `/mnt`) and route operations via a simple virtual file system.

---

## Build

Prereqs: a C toolchain (gcc/clang) and `make`.

```
git clone https://github.com/tlh45342/guppy.git
cd guppy
make
# optional:
make install   # installs into /usr/local/bin (or Cygwin equivalent)
Tested with:

Linux (gcc/clang)

macOS (clang)

Windows via Cygwin (gcc)

Quick start
Launch the REPL
bash
Copy code
guppy
Type help to see available commands. exit to quit.

Map an ISO and browse it
text
Copy code
guppy> use -i disc.iso /dev/b        # map an ISO image to a virtual device
guppy> mount /dev/b /                # mount it at root (or /mnt)
guppy> ls -l                         # list directory entries from the ISO
guppy> cat /HELLO.TXT                # print a file from the ISO (case/;1 trimmed)
Create a blank image, partition, and make an ext2 filesystem
text
Copy code
guppy> use -n image.img 20M /dev/a   # create 20 MB sparse image
guppy> gpt /dev/a --new rootfs linuxfs 19M
guppy> mkfs.ext2 /dev/a --part 1 --label rootfs
guppy> mount /dev/a --part 1 /       # mount ext2 at /
guppy> pwd
/
Guppy prints what it writes (e.g., GPT layout, superblock notes) so you can track changes.

How Guppy’s VFS works (the fun part)
Guppy keeps a mount table that maps paths → filesystem instances. When you run a command on a path, the VFS:

Finds the longest mountpoint prefix for that path (e.g., /mnt, /).

Hands the remainder of the path to the filesystem mounted there.

The filesystem (ISO9660, ext2, …) interprets its piece and does the work.

This is classic “overlay” behavior:

If you mount at /, that filesystem owns the root. Anything that might exist “under” / from other sources is hidden while the mount is present (same semantics as Unix).

Mounting at /mnt means / is still whatever is mounted at /, while /mnt/... is routed to the ISO (or whatever you mounted there).

What this means for ls /:

If an ISO is mounted at /, ls / should list ISO entries (e.g., HELLO.TXT) — not the synthetic mount table.

If nothing is mounted at /, ls / can default to showing synthetic mounts (helpful when you’re just wiring devices).

Guppy’s ls follows that rule: it checks “does this path belong to a mounted FS?” first, and only falls back to the synthetic mount listing when there isn’t one.

Current command set (early)
use — map an image file (or create) to /dev/* (e.g., /dev/a, /dev/b)

mbr, gpt — write partition tables (basic flows)

mkfs.ext2 — minimal ext2 writer (read-first, tiny-write later)

mount — mount a device (optionally a partition) at a path

ls — list directory entries (works on ISO mounts; ext2 browsing is WIP)

pwd — print current working directory

mkdir — currently used to establish a synthetic mountpoint (e.g., /mnt)

cp — copy file from ISO into ext2 root (MVP)

cat — print file contents via the VFS (MVP)

help, exit

Some commands are still minimal (e.g., ext2 browsing and mkdir-on-ext2). Expect rapid iteration.

Scripting
The tests/ directory contains example scripts. Run one with:

bash
Copy code
guppy path/to/script.script
Each line is a REPL command; Guppy stops on non-zero exit unless you handle it.

Status & Safety
Alpha quality. APIs and output may change.

Images only. Do not point Guppy at real disks unless you know exactly what you’re doing.

Expect partial implementations (especially filesystem writers); verify results with other tools when in doubt.

Roadmap (short list)
Ext2 directory reads for ls (not just ISO)

Relative paths and cd

Better error messages & diagnostics

Joliet/SVD support for nicer ISO names

More robust mkfs/ext2 metadata updates

Portable packaging

Contributing
Issues and PRs welcome. Small, focused changes are easiest to review:

Repro steps (scripts) are gold.

Keep output deterministic and readable.

Prefer tiny, well-named helpers over clever one-liners.

License
MIT (see SPDX headers in sources). If a top-level LICENSE file is missing, one will be added soon.
```