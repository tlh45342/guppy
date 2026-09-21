Phase 1E - Debian-compatible installed-package state

Run:
    O:\guppy\tests\pkg-phase1e>guppy install-state.script

Writes /var/lib/dpkg/status and /var/lib/dpkg/info/<package>.* state.
Maintainer scripts are stored but NOT executed.

Current deliberate limitation: status is append-only in Phase 1E; reinstall/update stanza replacement comes later.
