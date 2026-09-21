NTFS Windows external probe
===========================

Purpose
-------
This is an external interoperability checkpoint for Guppy's NTFS formatter.
It builds a small fixed VHD containing:

    fixed VHD
      -> GPT
        -> Microsoft Basic Data partition
          -> current Guppy NTFS formatter

The automated part verifies the VHD footer, GPT entry, partition-relative NTFS
boot sector, backup boot sector, and MFT records 0..8 before Windows is asked to
inspect the result.

Run
---
From the Guppy repository root:

    make -C tests/ntfs-windows test

The test intentionally RETAINS:

    tests/ntfs-windows/ntfs-windows.vhd

Windows step
------------
1. Open Disk Management as Administrator.
2. Action -> Attach VHD.
3. Browse to tests\\ntfs-windows\\ntfs-windows.vhd.
4. Attach it read/write (do not select Read-only unless you specifically want
   a non-writing probe).
5. Record what Windows reports for the partition/filesystem.

Do NOT initialize, format, or repair the disk if Windows offers to do so.  We
want Windows' first opinion of Guppy's filesystem unchanged.

If Windows assigns a drive letter and recognizes NTFS, report that before
running CHKDSK.  We can then choose the safest next diagnostic command.

Cleanup
-------
Detach the VHD from Disk Management first, then:

    make -C tests/ntfs-windows clean

This removes ntfs-windows.vhd and ntfs-windows.log.
