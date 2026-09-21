Phase 1B.2 - XZ + TAR inventory

Run from O:\guppy\tests\pkg-phase1b2:
    guppy.exe iso.script

Acceptance:
  1. pkg info reports data.tar.xz.
  2. pkg contents no longer returns "compressed data archives are not enabled".
  3. The real base-files data.tar.xz inventory is printed.
  4. logfile reaches "Logging stopped." (script does not stop on rc=3).

This pass uses liblzma (-llzma), the reference XZ Utils decoder library.
If Windows link fails with "cannot find -llzma", stop there and report that
exact error; do not change package logic. We can then vendor XZ Embedded as
a separate portability step.
