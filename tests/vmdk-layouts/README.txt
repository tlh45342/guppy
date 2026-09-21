VMDK layout scaffold tests
==========================

These tests lock down the public VMDK type vocabulary before the remaining
layouts are implemented:

  flat
  monolithic-flat
  monolithic-sparse
  split-flat
  split-sparse

The four non-flat layouts are intentionally expected to return rc=2 with the
current implementation.  They are parser/API fence posts, not fake support.
