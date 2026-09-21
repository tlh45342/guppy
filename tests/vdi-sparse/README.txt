VDI sparse/dynamic regression
=============================

Creates a 64 MiB logical VDI with --sparse.  The VDI starts with only its
header/block-map metadata.  Guest 1 MiB blocks are allocated on first write.

The test then drives ordinary Guppy disk construction through that backend:
GPT, EXT2, file creation, close, fresh-process reopen, remount, and persistence.

The regression harness also checks that the host VDI remains smaller than its
64 MiB logical capacity after this modest workload.  This distinguishes the
dynamic allocation path from the fixed/full VDI baseline.
