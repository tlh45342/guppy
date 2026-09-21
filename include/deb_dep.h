#pragma once
#include <stddef.h>

#define DEB_DEP_MAX_GROUPS 128
#define DEB_DEP_MAX_ALTS   8

typedef struct deb_dep_alt {
    char name[128];
    char arch_qual[32];
    char op[4];
    char version[128];
    char restrictions[256];
} deb_dep_alt_t;

typedef struct deb_dep_group {
    unsigned alt_count;
    deb_dep_alt_t alt[DEB_DEP_MAX_ALTS];
} deb_dep_group_t;

typedef struct deb_dep_list {
    unsigned group_count;
    deb_dep_group_t group[DEB_DEP_MAX_GROUPS];
} deb_dep_list_t;

/* Parse a Debian relationship field: comma-separated groups, | alternatives. */
int deb_dep_parse(const char *text, deb_dep_list_t *out, char *err, size_t errsz);
