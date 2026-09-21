ext2-mount regression
=====================

Exercises the EXT2 filesystem creation/probe/mount path:

  create + GPT partition
  mkfs.ext2 with a volume label
  mount without -t (filesystem auto-probe)
  mount-table reporting
  basic write/read/stat through the mounted filesystem

This also protects mkfs.ext2 application-output routing through logfile.
