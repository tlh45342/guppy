Guppy base-passwd controlled pre-unpack regression
==================================================

This test covers the audited Debian base-passwd 3.6.7 fresh-install preinst
semantics without executing /bin/sh.

The real Debian preinst embeds passwd.master and group.master in heredocs and,
on install, creates /etc/passwd and /etc/group only when they do not already
exist.  Guppy validates that script shape, extracts only those embedded data
blocks, writes them through the VFS, and otherwise continues to fail closed.

The second half verifies that existing account files are preserved.
