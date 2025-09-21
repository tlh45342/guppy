// src/cmd_use.c
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <ctype.h>

#include "diskio.h"
#include "vblk.h"
#include "genhd.h"

static void usage(void) {
    printf(
        "usage:\n"
        "  use                        # list registered block devices\n"
        "  use -i <image> <devname>   # attach <image> to <devname> and scan partitions\n"
        "  use --help                 # show this help\n"
    );
}

static int is_parent_row(const vblk_t *e) { return e->part_index == -1; }

static void list_devices(void) {
    if (g_vblk_count == 0) { printf("(no devices registered)\n"); return; }

    for (int i = 0; i < g_vblk_count; ++i) {
        const vblk_t *p = &g_vblk[i];
        if (!is_parent_row(p)) continue;

        const char *devkey = p->dev[0] ? p->dev : p->name;
        if (p->lba_size) {
            printf("%-10s %-24s base=%-6" PRIu64 " size=%" PRIu64 " LBAs\n",
                   p->name, devkey, p->lba_start, p->lba_size);
        } else {
            printf("%-10s %-24s base=%-6" PRIu64 " size=unknown\n",
                   p->name, devkey, p->lba_start);
        }

        struct item { const vblk_t *row; int idx; } items[256];
        int n = 0;
        for (int j = 0; j < g_vblk_count && n < (int)(sizeof items / sizeof items[0]); ++j) {
            const vblk_t *e = &g_vblk[j];
            size_t plen = strlen(p->name);
            if (strncmp(e->name, p->name, plen) != 0) continue;
            if (e->name[plen] == '\0') continue; // parent
            const char *q = e->name + plen; int ok = 1;
            for (const char *t=q; *t; ++t) if (!isdigit((unsigned char)*t)) { ok = 0; break; }
            if (!ok) continue;
            int idx = 0; for (const char *t=q; *t; ++t) idx = idx*10 + (*t - '0');
            items[n].row = e; items[n].idx = idx; ++n;
        }
        for (int a=0; a<n; ++a)
            for (int b=a+1; b<n; ++b)
                if (items[b].idx < items[a].idx) { struct item tmp = items[a]; items[a]=items[b]; items[b]=tmp; }
        for (int k=0; k<n; ++k) {
            const vblk_t *e = items[k].row;
            printf("  %-8s start=%" PRIu64 " size=%" PRIu64 " LBAs\n",
                   e->name, e->lba_start, e->lba_size);
        }
    }
}

static int handle_use_attach(const char *image_path, const char *devname) {
    uint64_t img_bytes = 0;

    fprintf(stderr, "use: attaching %s -> %s ...\n", devname, image_path);
    if (!diskio_attach_image(devname, image_path, &img_bytes)) {
        fprintf(stderr, "use: FAILED attach (file missing/unreadable?)\n");
        return 0; // don't kill the REPL
    }
    fprintf(stderr, "use: attached %s -> %s (%" PRIu64 " bytes)\n",
            devname, image_path, img_bytes);

    vblk_t parent = (vblk_t){0};
    snprintf(parent.name, sizeof parent.name, "%s", devname);
    snprintf(parent.dev,  sizeof parent.dev,  "%.*s", (int)sizeof parent.dev - 1, devname);
    parent.part_index = -1;
    snprintf(parent.fstype, sizeof parent.fstype, "%s", "-");
    parent.lba_start = 0;
    parent.lba_size  = 0;

    fprintf(stderr, "use: registering parent vblk row ...\n");
    if (vblk_register(&parent) < 0) {
        fprintf(stderr, "use: FAILED registry full\n");
        return 0;
    }

    gendisk gd = (gendisk){0};
    snprintf(gd.name, sizeof gd.name, "%s", devname);
    gd.sector_size = 512;
    gd.size_bytes  = img_bytes;

    fprintf(stderr, "use: scanning partitions via add_disk('%s') ...\n", gd.name);
    int rc = add_disk(&gd);
    if (rc != 0) {
        fprintf(stderr, "use: scan FAILED rc=%d (you can run 'partscan --verify %s')\n", rc, devname);
        return 0;
    }

    printf("%-10s %-24s base=%-6" PRIu64 " size=%" PRIu64 " LBAs\n",
           parent.name, parent.dev, parent.lba_start, parent.lba_size ? parent.lba_size : 0);
    fflush(stdout);
    fflush(stderr);
    return 0;
}

int cmd_use(int argc, char **argv) {
    if (argc == 1) { list_devices(); return 0; }
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        usage(); return 0;
    }
    if (argc == 4 && strcmp(argv[1], "-i") == 0) {
        const char *image = argv[2];
        const char *dev   = argv[3];
        if (!image || !dev || image[0] == '\0' || dev[0] == '\0') { usage(); return 0; }
        return handle_use_attach(image, dev); // always returns 0
    }
    usage();
    return 0; // never kill the REPL on misuse
}
