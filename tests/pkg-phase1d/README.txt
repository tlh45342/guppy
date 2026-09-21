Phase 1D - Debian control.tar.xz metadata

Run:
    O:\guppy\tests\pkg-phase1d>guppy control.script

This reads control.tar.xz directly from base-files on the mounted Debian ISO,
inventories the control archive, parses the Debian control file, and reports
dependency/package metadata.

Canonical image attach syntax is used:
    use -i H:\debian-13.6.0-amd64-netinst.iso /dev/b

No package database or dependency resolver is implemented in this phase.
This phase is read/inspection only.
