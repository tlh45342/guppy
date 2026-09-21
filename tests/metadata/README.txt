metadata regression
===================

Protects EXT2 permission metadata needed by offline system construction.

Phase 1 creates a file and chmods it executable (0755).
Phase 2 starts a fresh Guppy process, reattaches/remounts the image, verifies
0755 survived on disk, then changes it to 0600 and verifies the second change.

This specifically protects executable-bit persistence needed for installed
programs and scripts.
