utility-output regression
=========================

Protects logfile routing for older utility commands plus the host/VFS bridge:

  version, date
  lcat, lls
  lcp local -> mounted EXT2
  sha256 and hexdump through VFS
  raw write to a registered block device
  logfile -a append behavior
  do-script blank/comment handling

The test intentionally uses two Guppy invocations so logfile append is proven
across process boundaries.
