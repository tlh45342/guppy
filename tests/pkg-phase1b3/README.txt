Phase 1B.3 - vendored XZ Embedded

No host liblzma is required.

Run:
  guppy.exe iso.script

Acceptance:
  - Guppy builds on Windows without lzma.h and without -llzma.
  - pkg info reports data.tar.xz.
  - pkg contents decodes it and inventories the real base-files payload.
  - script reaches Logging stopped.

Vendored code is upstream XZ Embedded from the user-supplied
xz-embedded-master.zip. See lib/xz/COPYING (0BSD).
CRC64 support is enabled because normal XZ streams commonly use CRC64.
