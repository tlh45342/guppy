# Guppy

**Current version: 0.0.47**

**Guppy** is a portable, scriptable environment for constructing, inspecting,
and manipulating offline disk and system images. It is written in C and treats
Windows as a first-class host alongside Linux and macOS.

Guppy can work from the virtual-disk container inward: create an image, define
partitions, create a filesystem, manipulate files and metadata, create offline
account state, install Debian packages into a target filesystem, and inspect
the resulting disk without requiring the host operating system to mount the
guest filesystem.

Current development is focused heavily on bootable system-image construction.

---

## Why Guppy Exists

Guppy began with a simple frustration: there is no conventional native-Windows
version of **libguestfs/guestfish**.

Guestfish gives Linux users an unusually useful scriptable environment for
inspecting and modifying guest disk images without booting the guest operating
system. It can work with Windows *guest* images, but that is different from
having a normal native Windows-host `guestfish.exe`. On Windows, a comparable
workflow generally means introducing a Linux environment such as a VM or WSL,
mounting images through other software, or assembling several utilities that
each solve one part of the problem.

Excellent Windows-capable tools already exist for individual layers.
`qemu-img` is very good at creating, converting, and inspecting virtual-disk
containers. VirtualBox provides `VBoxManage` for VirtualBox media and VM
management. Windows image-mounting and forensic utilities can expose many disk
formats to the host. Packer can automate machine-image creation, normally by
booting and provisioning a guest.

Guppy addresses a different workflow: **scriptable offline system-image
construction**. It operates directly on the image and does not require a guest
kernel to be running or Windows itself to understand and mount a guest
filesystem such as EXT2.

The project has grown beyond the frustration that started it. Guppy now has
reusable block-device, VFS, filesystem, package, account, virtual-disk, and
inspection machinery that can also be used by other projects. It is therefore
not intended to be a feature-for-feature replacement for guestfish, Packer,
`qemu-img`, `VBoxManage`, or a disk-mounting utility. It has become a tool of
its own.

---

## Experimental Software and Data Safety

**Guppy directly reads and writes disk images, partition tables, filesystems,
virtual-disk container metadata, files, and offline system state. Use it at
your own risk.**

Functionality is covered by an expanding regression suite and selected
interoperability tests, but coverage and maturity vary by feature. Passing
tests do not guarantee that a particular image, filesystem, container variant,
or failure case is safe.

**Keep backups and work on copies of important images. Do not make Guppy the
only custodian of valuable data.**

Guppy is not designed, validated, or certified for medical/life-support,
safety-critical, high-availability, or other applications in which corruption,
data loss, or incorrect behavior could cause injury, loss of life, or serious
damage.

---

## What Guppy Can Build

A Guppy script can describe a pipeline such as:

```text
virtual disk container
        |
        v
partition table
        |
        v
filesystem
        |
        v
files + metadata
        |
        v
offline users/accounts
        |
        v
Debian package payloads/state
        |
        v
inspection / validation
        |
        v
finished offline system image
```

For example:

```text
create --sparse deb-min.vdi 2G
use -i deb-min.vdi /dev/a
gpt init /dev/a
gpt add /dev/a --type linuxfs --name rootfs --start 2M --size 100%
partscan /dev/a

mkfs.ext2 /dev/a1
mount /dev/a1 /target
mkdir -p /target/boot

pkg install base-files.deb /target
useradd -R /target -m alice
lcp kernel /target/boot/kernel

diskq deb-min.vdi
```

Not every complete operating-system build implied by this example is a finished
Guppy milestone yet, but these are the layers the project is intentionally
bringing together.

---

## Current Highlights

- Scriptable REPL and `do` script execution.
- RAW virtual-disk images.
- VDI 1.1 fixed/full images: create/open/read/write/flush.
- VDI 1.1 dynamic/sparse images with allocation-on-write.
- Legacy fixed/full VHD images: create/open/read/write/flush.
- `diskq` read-only container inspection and structural sanity reporting.
- Recognition of VDI, VHD, VMDK, QCOW2, and RAW/plain-disk candidates; VDI
  and fixed VHD have logical block access.
- Virtual block devices and bounded partition slices such as `/dev/a` and
  `/dev/a1`.
- MBR/GPT discovery and GPT creation.
- EXT2 formatting and substantial read/write support.
- ISO9660 read support with Rock Ridge/SUSP support.
- VFS paths, current directory, file copy/removal, metadata, chmod, ownership,
  and symlinks.
- Host helpers including `lls`, `lcat`, and `lcp`.
- Offline `useradd` and `passwd` support for a mounted target root.
- Debian `.deb` inspection and offline `pkg install` support, including
  TAR/XZ payload extraction and dpkg-style state recording.
- Embedded XZ decoding through XZ Embedded; no external `xz` executable is
  required for the supported package path.
- Runtime-selectable debugging.
- A growing scripted regression suite; the 0.0.44 development checkpoint has
  17 default regression groups after introduction of `diskq`.

More detailed implementation notes have moved to the `docs/` directory so the
README can remain an introduction rather than becoming the entire manual.

---

## Quick Example

```text
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

`lcp` means **local host -> Guppy VFS**. The source pathname follows host OS
rules; the destination follows the mounted guest-filesystem rules.

---

## Virtual Disk Images

`libvdisk` separates logical block access from the host-side image container:

```text
commands / VFS / GPT / filesystems
              |
             vblk
      (device + slice bounds)
              |
          libvdisk
              |
       RAW / VDI / ...
              |
           host I/O
```

Examples:

```text
create disk.img 32M
create -f vdi fixed.vdi 32M
create --sparse sparse.vdi 2G
```

Fixed VDI images created by Guppy have been independently boot-tested under
VirtualBox. Sparse VDI support is implemented and covered by Guppy regression
tests; broader external interoperability testing remains useful.

`diskq` inspects the container independently of the filename extension where a
format has a recognizable signature:

```text
diskq sparse.vdi
diskq random.dsk
```

RAW has no identifying container signature, so `diskq` deliberately reports a
RAW/plain-disk **candidate** rather than pretending RAW can be positively
identified.

See `docs/VDISK.md`.

---

## Filesystems and VFS

Guppy does not ask Windows to mount EXT2. The filesystem is accessed through
Guppy's own block/VFS/filesystem layers.

Current EXT2 work includes multi-group allocation, directories, regular files,
direct/single-indirect/double-indirect data, truncate/overwrite, removal and
reclamation, chmod, UID/GID ownership, special device nodes, and persistent
metadata. Triple-indirect regular-file addressing is not yet implemented.

EXT2 character and block device nodes can be created in mounted filesystems with:

```text
mknod PATH c MAJOR MINOR
mknod PATH b MAJOR MINOR
```

For example, `mknod /target/dev/null c 1 3` creates a real EXT2 character-device
inode. `stat` reports the node type and its major/minor numbers.

ISO9660 is read-only. Rock Ridge/SUSP support currently includes `SP`, `NM`,
`PX`, `TF`, `CE`, and `SL`.

FAT/VFAT formatting exists, but FAT/VFAT mounting is not currently a completed
feature.

See `docs/FILESYSTEMS.md`.

---

## Offline Package and Account Construction

Guppy's `pkg` command is intentionally an **offline image-construction**
facility rather than a complete replacement for Debian `dpkg`.

Current work can inspect `.deb` metadata and payloads and install supported
packages into a mounted target root, recording dpkg-style state. Maintainer
scripts are recorded but are not executed.

`useradd -R` and `passwd -R` can construct account state inside an offline
target filesystem. This allows image construction to proceed without booting
the target operating system.

See `docs/OFFLINE-SYSTEM.md`.

---

## Building

Guppy is written in C11 and is normally built with GCC/Clang-compatible tools.

### Windows

A MinGW-w64 toolchain is recommended. Guppy is routinely built from Windows
`cmd.exe` using GNU `make` and GCC.

```text
make
make test
make install
```

The current Windows install convention is:

```text
%USERPROFILE%\.local\bin
```

### Linux / macOS

```sh
make
make test
```

The default Unix install prefix remains `/usr/local` unless overridden.

---

## Scripting

Commands can be fed through stdin:

```text
guppy < build.script
```

or executed inside Guppy:

```text
do build.script
```

The scriptable interface is a core part of Guppy's design: image builds should
be repeatable, reviewable, and suitable for source control rather than
depending on a sequence of GUI mounting operations.

---

## Project Status

### 0.0.44

0.0.44 expands Guppy from filesystem/image manipulation toward offline
system-image construction and richer virtual-disk support. It adds functional
`useradd` and `passwd`, EXT2 ownership persistence, offline package regression
coverage, fixed/full and dynamic/sparse VDI support, and the first `diskq`
container-inspection/validation framework.

The default regression suite currently contains 17 groups, including fixed and
sparse VDI persistence and `diskq` structural auditing.

Current areas of work include:

- sparse VDI torture and external interoperability testing;
- fixed VHD external interoperability testing and hardening;
- a simple/full VMDK path before sparse VMDK;
- MBR/legacy partition command completion;
- FAT/VFAT mount support;
- continued VFS/filesystem error and metadata hardening;
- package dependency/order work for increasingly complete offline Debian
  construction.

Detailed historical and implementation status is in `docs/PROJECT-STATUS.md`
and deliberately deferred work remains in `FUTURE.md`.

---

## License

Guppy is released under the **MIT License**.

Copyright (c) 2026 Tom Hamilton.

See `LICENSE` for the license text.

---

## Author

**Tom Hamilton**
