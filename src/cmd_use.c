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

static void usage(void) {
    printf(
        "usage:\n"
        "  use                        # list registered block devices\n"
        "  use -i <image> <devname>   # attach <image> to <devname> and scan partitions\n"
        "  use --help                 # show this help\n"
    );
}

static void list_devices(void)
{
    if (g_vblk_count == 0) {
        printf("(no devices registered)\n");
        return;
    }

    for (int i = 0; i < g_vblk_count; ++i) {
        const vblk_t *p = &g_vblk[i];
        printf("%-10s %-24s start=%" PRIu64 " size=%" PRIu64 " LBAs\n",
               p->name,
               p->dev[0] ? p->dev : "-",
               p->lba_start,
               p->lba_size);
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
	
    if (par->lba_size) {
        DBG("%-10s %-24s base=%-6" PRIu64 " size=%" PRIu64 " LBAs",
            par->name, devkey, par->lba_start, par->lba_size);
    } else {
        DBG("%-10s %-24s base=%-6" PRIu64 " size=unknown",
            par->name, devkey, par->lba_start);
    }
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
