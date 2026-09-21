VDI fixed/full regression
=========================

This is the baseline regression for Guppy's VDI backend before sparse/dynamic
allocation is introduced.

It deliberately spans two Guppy processes.

Phase 1:
  * creates a 20 MiB .vdi (current fixed/full VDI)
  * attaches it through libvdisk
  * creates GPT + an EXT2 partition
  * mounts the filesystem and writes a known file

Phase 2:
  * starts a fresh Guppy process
  * reopens the same VDI
  * rescans GPT
  * remounts EXT2
  * verifies the file and metadata survived

This test should remain valid after sparse VDI is added; the fixed/full path
must continue to work as the simple reference implementation.
