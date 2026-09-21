Private Guppy real-Debian planner test.

Run from this directory:

    make clean
    make test

External prerequisite:

    H:\debian-13.6.0-amd64-netinst.iso

This version does NOT use debmin3.vdi.
It creates pkg-real-debian.vdi itself, formats it EXT2, installs the
known runtime seed packages, then performs repository lookup, planning,
and maintainer-script inspection.

Generated files removed by make clean:

    pkg-real-debian.vdi
    guppy-pkg-real-debian.log
