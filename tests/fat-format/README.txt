fat-format regression
=====================

Protects FAT/VFAT formatting on partition vblks.  The key invariant is that
mkfs.fat /dev/a1 and mkfs_vfat /dev/b1 format only the selected partition;
the parent GPT must remain readable and the child geometry must survive a
rescan.

FAT mounting is intentionally not claimed by this test.  The current FAT VFS
driver has probe support but no mount implementation; VFAT is also not
mountable yet.
