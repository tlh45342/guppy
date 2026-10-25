// cmd_mbr.c - MBR bootstrap-area operations.
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "vblk.h"
#include "app_printf.h"

#define MBR_SECTOR_BYTES 512u
#define MBR_BOOT_BYTES   446u

static int mbr_install(const char *devname, const char *hostfile)
{
    vblk_t *vb;
    FILE *f;
    uint8_t sector[MBR_SECTOR_BYTES];
    uint8_t boot[MBR_BOOT_BYTES];
    long n;
    size_t boot_len;

    f = fopen(hostfile, "rb");
    if (!f) {
        fprintf(stderr, "mbr install: cannot open boot file '%s'\n", hostfile);
        return 1;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0) {
        fprintf(stderr, "mbr install: cannot determine size of '%s'\n", hostfile);
        fclose(f);
        return 1;
    }
    if ((unsigned long)n > MBR_BOOT_BYTES) {
        fprintf(stderr, "mbr install: boot code is %ld bytes; maximum is %u bytes\n",
                n, MBR_BOOT_BYTES);
        fclose(f);
        return 1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "mbr install: cannot rewind '%s'\n", hostfile);
        fclose(f);
        return 1;
    }
    boot_len = (size_t)n;
    if (boot_len && fread(boot, 1, boot_len, f) != boot_len) {
        fprintf(stderr, "mbr install: read error on '%s'\n", hostfile);
        fclose(f);
        return 1;
    }
    fclose(f);

    vb = vblk_open(devname);
    if (!vb) {
        fprintf(stderr, "mbr install: cannot open device '%s'\n", devname);
        return 1;
    }
    {
        uint32_t bsz = vb->block_bytes ? vb->block_bytes : MBR_SECTOR_BYTES;
        if (bsz != MBR_SECTOR_BYTES) {
            fprintf(stderr, "mbr install: %s has %u-byte logical blocks; 512 required\n",
                    devname, bsz);
            return 1;
        }
    }
    if (vblk_size_bytes(vb) < MBR_SECTOR_BYTES) {
        fprintf(stderr, "mbr install: device '%s' is smaller than one MBR sector\n", devname);
        return 1;
    }
    if (!vblk_read_blocks(vb, 0, 1, sector)) {
        fprintf(stderr, "mbr install: failed to read LBA 0 from %s\n", devname);
        return 1;
    }

    if (boot_len) memcpy(sector, boot, boot_len);
    if (boot_len < MBR_BOOT_BYTES)
        memset(sector + boot_len, 0, MBR_BOOT_BYTES - boot_len);
    /* sector[446..511] is deliberately preserved byte-for-byte. */

    if (!vblk_write_blocks(vb, 0, 1, sector)) {
        fprintf(stderr, "mbr install: failed to write LBA 0 to %s\n", devname);
        return 1;
    }
    if (!vblk_flush(vb)) {
        fprintf(stderr, "mbr install: flush failed on %s\n", devname);
        return 1;
    }

    app_printf("Installed %zu bytes of MBR boot code on %s; bytes 446..511 preserved\n",
               boot_len, devname);
    return 0;
}

int cmd_mbr(int argc, char **argv)
{
    if (argc == 4 && strcmp(argv[1], "install") == 0)
        return mbr_install(argv[2], argv[3]);

    fprintf(stderr, "usage: mbr install <device> <boot-file>\n");
    return 1;
}
