// src/cmd_use.c
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <ctype.h>

#include "diskio.h"
#include "vblk.h"
#include "genhd.h"
#include "debug.h"   // for DBG(...)

/* If DBG isn't provided by debug.h, default to no-op so builds still succeed. */
#ifndef DBG
#define DBG(fmt, ...) do { (void)0; } while (0)
#endif

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
	DBG("list_devices:");
	
    if (g_vblk_count == 0) { printf("(no devices registered)\n"); return; }

    for (int i = 0; i < g_vblk_count; ++i) {
        const vblk_t *p = &g_vblk[i];
        if (!is_parent_row(p)) continue;

        const char *devkey = p->dev[0] ? p->dev : p->name;
		
		#ifdef DEBUG
		if (g_debug_flags > 0){
			DBG("  debug: flags=0x%08" PRIx32, (uint32_t)g_debug_flags);
			if (p->lba_size) {
				DBG("  %-10s %-24s base=%-6" PRIu64 " size=%" PRIu64 " LBAs",
					   p->name, devkey, p->lba_start, p->lba_size);
			} else {
				DBG("  %-10s %-24s base=%-6" PRIu64 " size=unknown",
					   p->name, devkey, p->lba_start);
			}
		}
		#endif

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
            /* Only show child partition details when debugging */
            printf("  %-8s start=%" PRIu64 " size=%" PRIu64 " LBAs\n",
                e->name, e->lba_start, e->lba_size);
        }
    }
}

static int handle_use_attach(const char *image_path, const char *devname) {
    uint64_t img_bytes = 0;

    DBG("use: attaching %s -> %s ...", devname, image_path);
    if (!diskio_attach_image(devname, image_path, &img_bytes)) {
        DBG("use: FAILED attach (file missing/unreadable?)");
        return 0; // don't kill the REPL
    }
    DBG("use: attached %s -> %s (%" PRIu64 " bytes)", devname, image_path, img_bytes);

    vblk_t parent = (vblk_t){0};
    /* Make the vblk 'name' the full /dev path so vblk_open('/dev/…') matches */
    snprintf(parent.name, sizeof parent.name, "%s", devname);
    snprintf(parent.dev,  sizeof parent.dev,  "%.*s", (int)sizeof parent.dev - 1, devname);
    parent.part_index = -1;
    snprintf(parent.fstype, sizeof parent.fstype, "%s", "-");
    parent.lba_start = 0;
    /* Give the parent a real size so vblk_open will accept it (raw ISO has no partitions). */
    parent.lba_size  = img_bytes / 512;  /* total LBAs at 512B */

    DBG("use: registering parent vblk row ...");
    if (vblk_register(&parent) < 0) {
        DBG("use: FAILED registry full");
        return 0;
    }

    gendisk gd = (gendisk){0};
    /* Keep gendisk name consistent with vblk parent name for lookups/children */
    snprintf(gd.name, sizeof gd.name, "%s", devname);
    gd.sector_size = 512;
    gd.size_bytes  = img_bytes;

    DBG("use: scanning partitions via add_disk('%s') ...", gd.name);
    int rc = add_disk(&gd);
    if (rc != 0) {
        DBG("use: scan FAILED rc=%d (you can run 'partscan --verify %s')", rc, devname);
        return 0;
    }

    /* Re-read the parent row we just registered so size reflects any updates from add_disk() */
    const vblk_t *par = NULL;
    for (int i = 0; i < g_vblk_count; ++i) {
        if (strcmp(g_vblk[i].name, parent.name) == 0) { par = &g_vblk[i]; break; }
    }
    if (!par) par = &parent; /* fallback */

    const char *devkey = par->dev[0] ? par->dev : par->name;
	
	#ifdef DEBUG
	if (g_debug_flags > 0){
		if (par->lba_size) {
			DBG("%-10s %-24s base=%-6" PRIu64 " size=%" PRIu64 " LBAs",
				   par->name, devkey, par->lba_start, par->lba_size);
		} else {
			DBG("%-10s %-24s base=%-6" PRIu64 " size=unknown",
				par->name, devkey, par->lba_start);
		}
	}
	#endif
    fflush(stdout);
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
        return handle_use_attach(image, dev); // always returns 0 (don’t kill REPL)
    }
    usage();
    return 0; // never kill the REPL on misuse
}
