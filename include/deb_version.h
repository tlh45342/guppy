#pragma once

/* Debian version ordering compatible with dpkg's epoch:upstream-revision model. */
int deb_version_compare(const char *a, const char *b);
int deb_version_satisfies(const char *have, const char *op, const char *want);
