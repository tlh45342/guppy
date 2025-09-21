// src/cmd_partscan.c
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "genhd.h"   // block_rescan(), struct gendisk
#include "vblk.h"    // g_vblk, g_vblk_count, vblk_by_name

// Return 1 if 'name' is a child of 'parent' (e.g., "/dev/a1" of "/dev/a")
static int is_child_of(const char *parent, const char *name) {
    size_t plen = strlen(parent);
    if (strncmp(name, parent, plen) != 0) return 0;
    const char *p = name + plen;
    if (*p == '\0') return 0; // exact same as parent
    // require the remainder to be all digits
    for (; *p; ++p) {
        if (!isdigit((unsigned char)*p)) return 0;
    }
    return 1;
}

static int child_index(const char *parent, const char *name) {
    size_t plen = strlen(parent);
    const char *p = name + plen;
    int idx = 0;
    while (*p) { idx = idx * 10 + (*p - '0'); ++p; }
    return idx;
}

static void list_children_sorted(const char *parent_name) {
    // collect children
    struct item { const vblk_t *row; int idx; } items[256];
    int n = 0;
    for (int i = 0; i < g_vblk_count && n < (int)(sizeof items / sizeof items[0]); ++i) {
        const vblk_t *e = &g_vblk[i];
        if (!is_child_of(parent_name, e->name)) continue;
        items[n].row = e;
        items[n].idx = child_index(parent_name, e->name);
        ++n;
    }
    if (n == 0) {
        printf("partscan: no partitions registered on %s\n", parent_name);
        return;
    }
    // simple sort by numeric suffix
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            if (items[j].idx < items[i].idx) {
                struct item tmp = items[i];
                items[i] = items[j];
                items[j] = tmp;
            }

    for (int i = 0; i < n; ++i) {
        printf("%s\n", items[i].row->name);
    }
}

static int usage(void) {
    fprintf(stderr,
        "usage:\n"
        "  partscan <parent>           # rescan partitions and list children\n"
        "  partscan --verify <parent>  # list currently registered children only\n");
    return 1;
}

int cmd_partscan(int argc, char **argv) {
    int commit = 1;            // default: do a rescan (commit)
    const char *parent = NULL;

    if (argc == 2) {
        parent = argv[1];
    } else if (argc == 3 && strcmp(argv[1], "--verify") == 0) {
        commit = 0;
        parent = argv[2];
    } else {
        return usage();
    }

    // Ensure parent exists in vblk registry
    const vblk_t *p = vblk_by_name(parent);
    if (!p) {
        fprintf(stderr, "partscan: parent '%s' not found (attach it first, e.g., `use -i <img> %s`)\n",
                parent, parent);
        return 1;
    }

    if (commit) {
        // Ask the block layer to probe and (re)register children
        int rc = block_rescan(parent);
        if (rc != 0) {
            fprintf(stderr, "partscan: rescan failed on %s\n", parent);
            return 1;
        }
    }

    // Show what we (now) have registered
    list_children_sorted(parent);
    return 0;
}
