// cmd_mount.c — guppy mount command (ext2 partitions and ISO9660 images)

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "devmap.h"
#include "mnttab.h"
#include "gpt.h"
#include "fs_probe.h"
#include "devutil.h"   // dev_split()

static void mount_usage(void) {
    printf("mount                             List mounts\n");
    printf("mount <dev> <mp> [--part N] [--fstype ext2|iso9660]\n");
    printf("Examples:\n");
    printf("  mount /dev/a1 /\n");
    printf("  mount /dev/a / --part 1 --fstype ext2\n");
    printf("  mount /dev/b /mnt --fstype iso9660\n");
}

/* ------- List mounts (pretty) ------- */
static void list_mounts_pretty(void) {
    const int n = mnttab_count();
    if (n <= 0) { puts("(no mounts)"); return; }

    printf("%-12s %-10s %-6s %-8s %s\n", "Mountpoint", "Device", "Part", "FStype", "Image");
    printf("%-12s %-10s %-6s %-8s %s\n", "----------", "------", "----", "------", "-----");

    for (int i = 0; i < n; ++i) {
        const MountEntry *m = mnttab_get(i);
        char base[32];
        int suffix_part = 0;
        const char *dev_for_img = m->dev;
        if (dev_split(m->dev, base, sizeof(base), &suffix_part)) dev_for_img = base;

        const char *img = devmap_resolve(dev_for_img);
        const char *fst = (m->fstype[0] ? m->fstype : "-");
        if (m->part_index > 0) {
            printf("%-12s %-10s %-6d %-8s %s\n",
                   m->mpoint, m->dev, m->part_index, fst, img ? img : "-");
        } else {
            if (suffix_part > 0) {
                printf("%-12s %-10s %-6d %-8s %s\n",
                       m->mpoint, m->dev, suffix_part, fst, img ? img : "-");
            } else {
                printf("%-12s %-10s %-6s %-8s %s\n",
                       m->mpoint, m->dev, "-", fst, img ? img : "-");
            }
        }
    }
}

/* ------- Tiny ISO probe (PVD @ LBA 16, id='CD001') ------- */
/* If you already have this in fs_probe.c, keep using that and drop this. */
static int probe_iso9660_magic_file(const char *img_path) {
    /* read 2048 bytes at offset 16 * 2048 */
    FILE *f = fopen(img_path, "rb");
    if (!f) return 0;
    const long off = 16L * 2048L;
    if (fseek(f, off, SEEK_SET) != 0) { fclose(f); return 0; }
    unsigned char buf[2048];
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    if (n < 7) return 0;
    /* Primary Volume Descriptor: type=1, id="CD001", version=1 */
    if (buf[0] == 1 &&
        buf[1]=='C' && buf[2]=='D' && buf[3]=='0' && buf[4]=='0' && buf[5]=='1' &&
        buf[6] == 1) {
        return 1;
    }
    return 0;
}

int cmd_mount(int argc, char **argv) {
    if (argc == 1) { list_mounts_pretty(); return 0; }

    if (argc >= 3) {
        const char *dev = argv[1];
        const char *mp  = argv[2];

        /* parse optional flags */
        int part_flag = 0;
        const char *fstype = "";
        for (int i = 3; i < argc; ++i) {
            if (strcmp(argv[i], "--part") == 0 && i + 1 < argc) {
                part_flag = atoi(argv[++i]);
            } else if (strcmp(argv[i], "--fstype") == 0 && i + 1 < argc) {
                fstype = argv[++i];
            }
        }

        /* split /dev/a1 into base + part_from_name */
        char base[32];
        int part_from_name = 0;
        if (!dev_split(dev, base, sizeof(base), &part_from_name)) {
            fprintf(stderr, "mount: '%s' is not a /dev/* device\n", dev);
            return 2;
        }

        /* resolve image by base (/dev/a) */
        const char *img = devmap_resolve(base);
        if (!img) {
            fprintf(stderr, "mount: unknown device %s (use -i <img> %s first)\n", base, base);
            return 2;
        }

        /* Decide fstype if omitted: try ext2 partition, else ISO9660 */
        int final_part = 0;
        int want_iso = 0;

        if (fstype && *fstype) {
            if (strcmp(fstype, "ext2") == 0) {
                /* ext2: pick final_part */
                final_part = part_flag ? part_flag : part_from_name;
                if (!final_part) {
                    int only = gpt_find_single_partition(img);
                    if (only > 0) final_part = only;
                }
                if (!final_part) {
                    fprintf(stderr, "mount: ambiguous/no partition on %s; pass --part N or use /dev/aN\n", base);
                    return 2;
                }
            } else if (strcmp(fstype, "iso9660") == 0) {
                want_iso = 1;
                /* For ISO, ignore any partition; warn if someone passed one */
                int suffix = part_from_name;
                if (part_flag || suffix) {
                    fprintf(stderr, "mount: ignoring partition for iso9660 (using whole device %s)\n", base);
                }
                final_part = 0;
            } else {
                fprintf(stderr, "mount: unsupported --fstype '%s' (try ext2 or iso9660)\n", fstype);
                return 2;
            }
        } else {
            /* Auto-probe */
            /* 1) If GPT has a single partition, prefer that (likely ext2) */
            int only = gpt_find_single_partition(img);
            if (only > 0) {
                final_part = only;
                fstype = "ext2";
            } else {
                /* 2) If the device name has a suffix, try that as ext2 */
                int sug = part_from_name ? part_from_name : part_flag;
                if (sug > 0) {
                    uint64_t start_lba=0, total_sectors=0;
                    if (gpt_get_partition(img, sug, &start_lba, &total_sectors)) {
                        uint64_t fs_off = start_lba * 512ull;
                        if (probe_ext2_magic(img, fs_off)) {
                            final_part = sug;
                            fstype = "ext2";
                        }
                    }
                }
                /* 3) Otherwise, see if the whole image looks like an ISO */
                if (!fstype || !*fstype) {
                    if (probe_iso9660_magic_file(img)) {
                        fstype = "iso9660";
                        want_iso = 1;
                        final_part = 0;
                    }
                }
            }
            if (!fstype || !*fstype) {
                fprintf(stderr, "mount: could not auto-detect FS; specify --fstype ext2|iso9660 (and --part if needed)\n");
                return 2;
            }
        }

        /* Record in mount table */
        if (!mnttab_add(dev, final_part, fstype, mp)) {
            fprintf(stderr, "mount: mount table full\n");
            return 2;
        }

        /* Pretty print */
        if (strcmp(fstype, "ext2") == 0) {
            printf("Mounted %s part=%d on %s (fstype=%s) -> %s\n", dev, final_part, mp, fstype, img);
        } else if (want_iso) {
            /* For ISO, show device w/out partition */
            printf("Mounted %s on %s (fstype=iso9660) -> %s\n", base, mp, img);
        } else {
            printf("Mounted %s on %s (fstype=%s) -> %s\n", dev, mp, fstype, img);
        }
        return 0;
    }

    mount_usage();
    return 2;
}
