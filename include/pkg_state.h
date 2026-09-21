#pragma once
#include <stddef.h>

typedef struct pkg_state_entry {
    char package[128];
    char version[128];
    char state[64];
} pkg_state_entry_t;

int pkg_state_lookup(const char *root, const char *package, pkg_state_entry_t *out);
int pkg_state_list(const char *root);

/* Transition one registered package to install ok installed. */
int pkg_state_set_installed(const char *root, const char *package);
