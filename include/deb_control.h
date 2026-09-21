#pragma once
#include <stddef.h>

typedef struct deb_control {
    char package[128];
    char version[128];
    char architecture[64];
    char essential[32];
    char priority[64];
    char section[128];
    char maintainer[256];
    char installed_size[64];
    char pre_depends[2048];
    char depends[4096];
    char recommends[2048];
    char suggests[2048];
    char description[4096];
} deb_control_t;

/* Parse the RFC822-like Debian binary package control file. */
int deb_control_parse(const char *text, size_t len, deb_control_t *out);
