#pragma once

/* Build a glibc 1.1 ld.so.cache for an offline amd64 target root.
 * This is a native Guppy implementation; it never executes host ldconfig. */
int pkg_ldconfig_run(const char *target_root);

/* Inspect and structurally validate the target cache. */
int pkg_ldconfig_print_cache(const char *target_root);
