Guppy rm regression
===================

This test exercises EXT2 unlink through the public rm command.

It verifies that Guppy can:
  * create and read a regular file;
  * remove that file through rm;
  * recreate the same pathname after removal;
  * remove the recreated file again; and
  * preserve an unrelated neighboring file and directory listing.

The command intentionally remains file-only: cmd_rm refuses directories.
Directory removal belongs to a separate rmdir operation if/when Guppy adds one.
