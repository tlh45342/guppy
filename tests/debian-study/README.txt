Guppy Debian package investigation
==================================

Purpose
-------
This directory is the growing reconnaissance project for real Debian .deb
packages.  It is intentionally separate from the pass/fail regression suite.
It scouts package metadata and maintainer-script behavior before DEBMIN4 trips
over it.

Nothing in this study executes a maintainer script.  It uses Guppy's existing
read-only repository, package lookup, and `pkg scripts` / `pkg script` support.

Current scope
-------------
The initial package inventory is the DEBMIN4 requested set plus dependencies
already observed or directly exposed by those package records.  Edit
known-packages.txt as the hunt expands.

Dash is the current DEBMIN4 blocker, so the first focused evidence section also
captures the complete real dash preinst/postinst/prerm/postrm bodies from the
Debian ISO.

Run from repository root:

    make study-debian

Default ISO:

    H:\debian-13.6.0-amd64-netinst.iso

Override when useful:

    make study-debian ISO=D:\path\to\debian.iso

Outputs
-------
    tests/debian-study/debian-study.log
        Full Guppy evidence, including focused script bodies.

    tests/debian-study/debian-study-summary.txt
        Human-readable inventory.

    tests/debian-study/debian-study-summary.csv
        Machine-readable inventory for comparison/growth.

The study is not part of `make test` or `make test-debian`; it is an
investigation tool, not an acceptance test.
