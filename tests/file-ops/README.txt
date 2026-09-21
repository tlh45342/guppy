file-ops regression
===================

Exercises the common script-facing file workflow on a writable EXT2 filesystem:

  mkdir -p
  cd / pwd
  echo > file
  cat
  stat
  cp file -> file
  cp file -> existing directory
  ls -l
  rm followed by recreation of the same pathname

The focused rm regression remains separate; rm is used here only as part of the
larger end-to-end file-operation workflow.

run_regression.py verifies stable semantic output rather than timestamps or
inode numbers.
