accounts regression
===================

Exercises the offline DEB2 account bootstrap path on a mounted EXT2 target:

  useradd -R /target with explicit UID/GID and home creation
  /etc/passwd, /etc/group, /etc/shadow, /etc/gshadow creation
  home-directory ownership via VFS chown support
  passwd plaintext -> portable SHA-256 crypt ($5$)
  passwd --hash for a caller-supplied crypt hash
  automatic UID allocation and -M no-home mode
