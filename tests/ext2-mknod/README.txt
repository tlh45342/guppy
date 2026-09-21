ext2-mknod regression
=====================

Exercises Guppy's VFS-backed EXT2 mknod support for character and block devices.
It creates /dev/console (1:0), /dev/null (1:3), /dev/zero (1:5), and a block
example (8:0), reopens the image in a second Guppy process, and checks stat.

verify.py independently parses the EXT2 directory and inode structures to verify
dirent type, inode type, encoded major/minor, and zero allocated data blocks.

Run:
  make test
  make clean
