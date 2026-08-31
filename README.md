# Guppy

**Current version: 0.0.42**

**Guppy** is a scriptable disk- and filesystem-image construction tool
written in C. It is intended to make low-level image building practical
from Windows, Linux, and macOS without requiring the host operating
system to mount every filesystem being manipulated.

Guppy provides a small REPL/script environment, a virtual block-device
layer, partition-table support, a VFS router, filesystem
readers/writers, and local-host helpers. The same script can therefore
describe operations such as creating an image, partitioning it,
formatting a filesystem, mounting it inside Guppy, and copying files
into it.

Current development is focused heavily on bootable system-image
construction.

------------------------------------------------------------------------

## Current Highlights

-   Image creation with binary/decimal size syntax.
-   Virtual-disk container support through `libvdisk`:
    -   RAW images;
    -   fixed VDI images, including create/open/read/write/flush;
    -   case-insensitive `.vdi` format inference and explicit `-f vdi`;
    -   Guppy-created fixed VDI images have been boot-tested successfully in VirtualBox.
-   Virtual block devices such as `/dev/a`, `/dev/a1`, and `/dev/a2`.
-   MBR and GPT discovery and GPT creation.
-   GPT partition types currently used by Guppy include `linuxfs` and
    `biosboot`.
-   Raw writes to a device or partition.
-   VFS mount routing and current-working-directory support, including
    relative paths.
-   VFS metadata operations including `chmod` for changing file modes.
-   `date` command for displaying the current date/time.
-   EXT2 formatting and read/write support, including:
    -   multi-group filesystem formatting and allocation;
    -   group-aware inode and block allocation/reclamation;
    -   directory creation, lookup, enumeration, and metadata;
    -   regular-file creation and reads/writes;
    -   direct, single-indirect, and double-indirect block addressing;
    -   overwrite/truncate of existing regular files;
    -   regular-file removal with `rm`;
    -   data-block, indirect-block, and inode reclamation and reuse;
    -   filesystem block-size reporting through VFS/stat.
-   ISO9660 read support.
-   Rock Ridge/SUSP support:
    -   `NM` --- POSIX filenames.
    -   `PX` --- POSIX mode, UID, GID, and link count.
    -   `TF` --- Rock Ridge timestamps.
    -   `CE` --- SUSP continuation areas.
    -   `SL` --- symbolic-link targets.
-   Human-readable VFS listings with `ls -h`, including combined forms
    such as `ls -lah`.
-   VFS file inspection with `hexdump`, including byte-count and offset
    selection.
-   Runtime-selectable debugging; debug capability is compiled in but
    output is off until enabled.
-   Host/local helpers such as `lls`, `lcat`, and `lcp`.

The current EXT2 writer supports direct, single-indirect, and
double-indirect regular-file data. Triple-indirect block addressing is
not yet supported.

EXT2 overwrite and removal use real filesystem operations rather than
command-level delete/recreate shortcuts. Existing files can be truncated
and rewritten in place, and removed regular files release their inode
and allocated data/pointer blocks for later reuse.

------------------------------------------------------------------------

## Example

A small GPT + EXT2 image can be built with a Guppy script such as:

``` text
create disk.img 64M
use -i disk.img /dev/a
gpt init /dev/a
gpt add /dev/a --type biosboot --name bios --start 1M --size 1M
gpt add /dev/a --type linuxfs --name rootfs --start 2M --size 100%
partscan /dev/a
mkfs.ext2 /dev/a2
mount /dev/a2 /
mkdir /boot
lcp boot.bin /boot/boot.bin
ls -lah /boot
stat /boot/boot.bin
```

`lcp` deliberately means **local host -\> Guppy VFS**. The source
pathname follows the host OS rules; the destination pathname follows the
mounted filesystem rules.

A VFS-to-VFS copy can overwrite an existing regular file:

``` text
cp /iso/install.amd/vmlinuz /boot/vmlinuz
```

For EXT2, an existing destination is truncated and rewritten through the
existing inode rather than being silently refused or implemented as an
`rm` followed by a new create.

------------------------------------------------------------------------

## Virtual Disk Images and `libvdisk`

Guppy separates logical block-device access from the host-side disk-image
container. The `libvdisk` layer presents a virtual disk as a logical byte
array while format backends translate those accesses to the underlying host
file. Partitioning and filesystem code therefore do not need to know whether
the disk is RAW, VDI, or another future container format.

The current path is:

``` text
commands / VFS / GPT / filesystems
              |
             vblk
      (device + slice bounds)
              |
          libvdisk
              |
          RAW / VDI
              |
           host I/O
```

RAW remains the default for ordinary image names:

``` text
create disk.img 32M
create -f raw disk.img 32M
```

Fixed VDI images can be created explicitly:

``` text
create -f vdi disk.vdi 32M
```

The `.vdi` extension is also recognized when the format is not explicitly
specified:

``` text
create disk.vdi 32M
use -i disk.vdi /dev/a
```

Current VDI support targets the VDI 1.1 fixed-image layout. Guppy can create,
open, read, write, and flush these images. A Guppy-created fixed VDI has been
partitioned with GPT, had BIOS boot code written to virtual LBA 0, attached
directly to Oracle VirtualBox, and successfully BIOS-booted. This provides an
external interoperability test of both VDI creation and logical-sector
translation.

Dynamic/sparse VDI allocation is not yet implemented. VMDK, VHD, and QCOW2
are prospective additional `libvdisk` backends.

------------------------------------------------------------------------

## Bootable VDI Example

A small BIOS-bootable fixed VDI can be constructed entirely by Guppy:

``` text
create -f vdi test.vdi 32M
use -i test.vdi /dev/a

gpt init /dev/a
gpt add /dev/a --type linuxfs --name testpart --start 2M --size 24M
partscan /dev/a
gpt print /dev/a

write /dev/a boot0.bin
```

Here `write /dev/a boot0.bin` writes to virtual disk offset zero. For a VDI,
`libvdisk` translates that logical access to the correct data region in the
container; GPT and the command layer operate exactly as they do for a RAW
disk.

This exact workflow has been validated by booting the resulting VDI under
VirtualBox.

------------------------------------------------------------------------

## VFS and Local Namespaces

Guppy keeps host paths and mounted-filesystem paths conceptually
separate.

  -----------------------------------------------------------------------
  Command                             Namespace / purpose
  ----------------------------------- -----------------------------------
  `ls`                                Guppy VFS directory listing; `-h`
                                      provides human-readable sizes

  `cd` / `pwd`                        Guppy VFS current directory

  `cat`                               Read a Guppy VFS file

  `hexdump`                           Hex/ASCII inspection of a Guppy VFS
                                      file

  `stat`                              Metadata through `vfs_stat()`

  `chmod`                             Change permissions/mode on a Guppy
                                      VFS object

  `date`                              Display the current date/time

  `cp`                                Guppy VFS -\> Guppy VFS copy;
                                      existing regular files are
                                      overwritten

  `rm`                                Remove a Guppy VFS regular file

  `lls`                               Local/host directory listing

  `lcat`                              Local/host file display

  `lcp`                               Local/host file -\> Guppy VFS

  `write`                             Local/host file -\> raw block
                                      device/partition
  -----------------------------------------------------------------------

Relative VFS paths are resolved against Guppy's current working
directory.

For example:

``` text
cd /boot
ls -lah
stat vmlinuz
hexdump -n 128 vmlinuz
```

all refer to paths relative to `/boot`.

------------------------------------------------------------------------

## EXT2

Guppy's EXT2 implementation is intended to support actual image
construction rather than only filesystem inspection.

Current regular-file block addressing supports:

-   12 direct block pointers;
-   the single-indirect block;
-   the double-indirect block.

This is sufficient for multi-megabyte files used during current
bootable-image development. Triple-indirect addressing remains outside
the current implementation.

Existing regular files can be replaced normally:

``` text
cp /source/file /boot/file
```

If `/boot/file` already exists, EXT2 truncates it, releases its old data
and pointer blocks, and writes the replacement contents while retaining
the existing inode.

EXT2 permission bits can be changed through the VFS:

``` text
chmod 0755 /boot/file
```

Regular files can also be removed:

``` text
rm /boot/file
```

Removal updates the directory entry and releases the file's allocated
EXT2 blocks and inode. Reclaimed resources are available for subsequent
file creation.

`rm` currently targets regular files; directory removal is a separate
operation and is not implied by `rm`.

------------------------------------------------------------------------

## ISO9660 and Rock Ridge

ISO9660 volumes are read-only in Guppy. When SUSP/Rock Ridge is present,
Guppy prefers Rock Ridge metadata over the restricted ISO9660
presentation.

For example, a Debian ISO that would otherwise expose names such as:

``` text
INSTALL.AMD
VMLINUZ.
INITRD.GZ
README_M.TXT
```

can be presented using its Rock Ridge names:

``` text
install.amd
vmlinuz
initrd.gz
README.mirrors.txt
```

The Rock Ridge decoder is shared by lookup and directory enumeration, so
a name shown by `ls` is the same name accepted by `stat`, `cat`, `cp`,
`hexdump`, and other VFS consumers.

Supported Rock Ridge/SUSP records currently include `SP`, `NM`, `PX`,
`TF`, `CE`, and `SL`. Less-common relocation/device records can be added
as compatibility requirements arise.

------------------------------------------------------------------------

## File Inspection

Long listings can display human-readable sizes:

``` text
ls -lh /boot
ls -lah /boot
```

`ls -l` continues to report exact byte counts.

`hexdump` reads through the Guppy VFS, so it works with files from
mounted filesystems such as EXT2 and ISO9660:

``` text
hexdump /boot/vmlinuz
hexdump -n 128 /boot/vmlinuz
hexdump -s 0x40 -n 64 /boot/vmlinuz
```

This is useful when constructing images because source and destination
files can be inspected without mounting the image through the host
operating system.

------------------------------------------------------------------------

## Debugging

Debug support is compiled into normal Guppy builds, but the runtime flag
mask starts at zero.

Examples:

``` text
debug iso on
debug vfs on
debug all on
debug all off
```

This avoids requiring a separate debug executable while keeping ordinary
runs quiet.

------------------------------------------------------------------------

## Size Syntax

Guppy accepts human-readable sizes.

  Suffix                Meaning
  --------------------- ---------------------------
  `K`                   KiB
  `M`                   MiB
  `G`                   GiB
  `KiB`, `MiB`, `GiB`   explicit IEC binary units
  `KB`, `MB`, `GB`      decimal SI units
  no suffix             bytes

Examples:

``` text
create disk.img 64M
create disk.img 1G
create disk.img 512KiB
create disk.img 64MB
```

The older `--size` form remains supported:

``` text
create disk.img --size 64MiB
create disk.img --size=64MiB
```

GPT size/start specifications use the same notation and also support
sectors (`s`) and percentages where appropriate:

``` text
gpt add /dev/a --type linuxfs --name rootfs --start 2M --size 62M
gpt add /dev/a --type linuxfs --name rootfs --start 4096s --size 100%
```

------------------------------------------------------------------------

## Raw Writes

Guppy can place a host file directly into a registered block device or
partition:

``` text
write /dev/a1 boot1.bin
write /dev/a1 payload.bin 4K
```

The optional offset is relative to the selected device or partition.
Guppy bounds checks the write against that device's size.

------------------------------------------------------------------------

## Building

Guppy is written in C11 and is normally built with GCC/Clang-compatible
tools.

### Windows

A MinGW-w64 toolchain is recommended. Guppy is routinely built from
Windows `cmd.exe` using GNU `make` and GCC.

Typical requirements:

``` text
gcc
make
```

Build:

``` text
make
```

Install:

``` text
make install
```

The current Windows install convention is the user's local binary
directory:

``` text
%USERPROFILE%\.local\bin
```

### Linux / macOS

Build with:

``` sh
make
```

The default Unix install prefix remains `/usr/local` unless overridden.

------------------------------------------------------------------------

## Scripting

Commands can be fed through stdin:

``` text
guppy < build.script
```

or executed from inside Guppy:

``` text
do build.script
```

Lines beginning with the script comment marker are ignored by the script
runner.

------------------------------------------------------------------------

## Project Status

### 0.0.42

Version 0.0.42 consolidates recent VFS/EXT2 repairs and introduces the first
`libvdisk` container backend work. RAW image access is routed through the
virtual-disk layer, and fixed VDI images can now be created, opened, read,
written, and flushed. A Guppy-created VDI has been independently boot-tested
under VirtualBox. The release also cleans stale source comments and records
deliberately deferred work in `FUTURE.md`.

### 0.0.41

Version 0.0.41 added the `date` and `chmod` commands and continued the
EXT2/VFS work used by the current bootable-system-image project.

Guppy is under active development. The VFS and filesystem layers are
intentionally being advanced in small, testable rounds: make an
operation correct, prove it against a real image, then build the next
layer.

Recent EXT2 work has established multi-group formatting and allocation,
large regular-file I/O through double-indirect blocks, persistent removal,
block/inode reclamation and reuse, overwrite through real truncate semantics,
and VFS permission changes with `chmod`.

As an independent interoperability check, a separately written x86 BIOS
bootloader has successfully parsed a Guppy-created GPT disk, located the
EXT2 root filesystem, traversed `/boot`, resolved direct, single-indirect,
and double-indirect file blocks, and read file data. The same boot path has
also located the Debian Linux kernel and initrd stored by Guppy and validated
the Linux kernel boot header. This provides a useful independent check that
the on-disk structures produced by Guppy are usable outside Guppy itself.

Current bootable-image work is progressing from filesystem correctness into
Linux kernel/initrd loading and boot-protocol handoff.

Current areas of work include:

-   broader EXT2 filesystem operations beyond regular-file
    create/read/write/remove;
-   triple-indirect EXT2 block addressing if image workloads require it;
-   broader Rock Ridge compatibility where real-world media requires it;
-   dynamic/sparse VDI allocation;
-   VMDK and other additional virtual-disk container backends;
-   additional filesystem formats;
-   continued strengthening of VFS/filesystem error propagation and
    validation.

------------------------------------------------------------------------

## Future Work

Deliberately deferred features and known limitations are tracked in
`FUTURE.md`. Keeping them there avoids leaving ambiguous placeholder comments
throughout otherwise functional code.

------------------------------------------------------------------------

## License

Guppy is released under the **MIT License**.

Copyright (c) 2026 Tom Hamilton.

See `LICENSE` for the license text.

------------------------------------------------------------------------

## Author

**Tom Hamilton**
