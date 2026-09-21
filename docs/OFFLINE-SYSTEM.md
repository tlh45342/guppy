# Offline System Construction

Guppy's longer-term image-building goal is to construct useful guest system
state without booting the target operating system.

## Debian packages

`pkg` can inspect Debian `.deb` archives and install supported payloads into a
mounted target root. The current path includes ar/deb parsing, control metadata,
dependency parsing, TAR extraction, and XZ decoding through the bundled XZ
Embedded decoder.

Installation records dpkg-style state such as `/var/lib/dpkg/status` and
package file lists. Maintainer scripts are recorded but are not executed.

This is intentionally not yet a full `dpkg` implementation.

## Accounts

`useradd -R <root> ...` and `passwd -R <root> ...` operate on an offline target
root. They can create/update passwd, shadow, group, and gshadow state and use
EXT2 ownership support for home directories.

This matters because a system image can acquire Unix account state before the
guest kernel has ever booted.

## Why this layer matters

Together with virtual-disk, partition, filesystem, and file operations, these
commands move Guppy beyond a disk mounting utility. The intended workflow is
repeatable offline **system-image construction**.
