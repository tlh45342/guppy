Phase 1A permanent regression test

Purpose:
  Prove the basic Debian package/ar parser still recognizes a .deb and can
  inventory its data archive without depending on the Debian ISO.

Run directly:
  guppy test.script

Normally run through:
  O:\guppy\tests>make test-pkg-1a
or:
  O:\guppy\tests>make test
