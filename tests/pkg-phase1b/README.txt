Phase 1B path-namespace probe

New convention:
  /path/...             Guppy VFS (default)
  local:path/...        host/local filesystem
  local:H:\path\foo     Windows host path

Primary test:
  guppy iso.script

Expected:
  pkg info opens base-files directly through the mounted ISO VFS and prints:
      Source:          vfs
  pkg contents is expected to stop cleanly with the actual compressed
  data.tar.* member name. Compression support is the next phase.

The implementation deliberately does NOT refactor cp/lcp/lcat/lls yet.
For this first slice, pkg proves the namespace convention.

Implementation note:
  Existing .deb/tar parsing is seek-oriented around FILE*. VFS package input
  is therefore copied to tmpfile() behind gu_path_open_seekable_read().
  This keeps the parser single-sourced and lets us prove the namespace now.
  It can later become a true generic seekable-reader abstraction without
  changing the pkg command syntax.
