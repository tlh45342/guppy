// src/vblk.c

#include <string.h>
#include "vblk.h"

/* Simple static registry; grow or make dynamic later if needed. */
#ifndef VBLK_MAX_ENTRIES
#define VBLK_MAX_ENTRIES 64
#endif

vblk_t g_vblk[VBLK_MAX_ENTRIES];
int    g_vblk_count = 0;

const vblk_t *vblk_by_name(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < g_vblk_count; ++i) {
        if (strcmp(g_vblk[i].name, name) == 0) {
            return &g_vblk[i];
        }
    }
    return NULL;  /* avoid “control reaches end of non-void function” */
}

int vblk_register(const vblk_t *entry) {
    if (!entry) return -1;
    if (g_vblk_count >= VBLK_MAX_ENTRIES) return -1;
    g_vblk[g_vblk_count] = *entry;  /* struct copy */
    return g_vblk_count++;
}

void vblk_clear(void) {
    g_vblk_count = 0;
}
