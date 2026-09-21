#pragma once
#include <stddef.h>

/* Apply the deliberately small, shell-free fresh-install pre-unpack subset.
 * Returns 0 when no preinst exists or every required action was understood.
 * Returns non-zero without unpacking when an action is unsupported. */
int pkg_preunpack_fresh(const char *package, const char *version,
                        const char *target_root,
                        const char *preinst, size_t preinst_len);
