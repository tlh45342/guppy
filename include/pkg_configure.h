#pragma once

/* Controlled, offline package configuration.
 *
 * Round 10 deliberately supports only configuration semantics that Guppy can
 * prove and perform safely.  Unsupported maintainer-script behavior fails
 * closed and leaves the package in "install ok unpacked" state.
 */
int pkg_configure_one(const char *package, const char *target_root);
