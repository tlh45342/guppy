#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "blkdevmap.h"

typedef struct {
    char name[32];
    vblk_t *blk;
} entry_t;

#define MAX_DEVS 64
static entry_t g_table[MAX_DEVS];
static int g_count = 0;

vblk_t *blkdevmap_get(const char *name) {
    for (int i = 0; i < g_count; i++) {
        if (strcmp(g_table[i].name, name) == 0) {
            return g_table[i].blk;
        }
    }
    return NULL;
}

int blkdevmap_add(const char *name, vblk_t *blk) {
    if (!name || !blk) return -1;

    // Replace if already exists
    for (int i = 0; i < g_count; i++) {
        if (strcmp(g_table[i].name, name) == 0) {
            g_table[i].blk = blk;
            return 0;
        }
    }

    if (g_count >= MAX_DEVS) return -1;

    snprintf(g_table[g_count].name, sizeof(g_table[g_count].name), "%s", name);
    g_table[g_count].blk = blk;
    g_count++;
    return 0;
}

int blkdevmap_remove(const char *name) {
    for (int i = 0; i < g_count; i++) {
        if (strcmp(g_table[i].name, name) == 0) {
            // shift down
            for (int j = i; j < g_count - 1; j++) {
                g_table[j] = g_table[j + 1];
            }
            g_count--;
            return 0;
        }
    }
    return -1;
}

void blkdevmap_foreach(blkdevmap_enum_fn fn, void *arg) {
    for (int i = 0; i < g_count; i++) {
        fn(g_table[i].name, g_table[i].blk, arg);
    }
}
