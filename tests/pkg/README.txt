pkg regression
==============

This is the canonical default-suite regression for Guppy's Debian package
capability.  It replaces the old phase-oriented default entries:

  pkg-1a
  pkg-control
  pkg-depends
  pkg-state

The single synthetic control-test.deb fixture proves the complete current PKG
lifecycle without requiring an external Debian ISO:

  pkg info
  pkg contents
  pkg control
  pkg depends
  offline install into mounted EXT2
  payload extraction
  /var/lib/dpkg/status creation
  /var/lib/dpkg/info/<package>.list creation
  maintainer-script recording and executable mode

The older pkg-phase1* directories remain in the tree for development history,
focused debugging, and real-Debian tests, but they are no longer separate
default regression entries.
