// cmd_write.c - write a host file into a registered block device/partition.
//
// Usage:
//   write <dev> <host-file> [offset]
//
// The offset is relative to the start of <dev> and defaults to zero.
// Size syntax follows Guppy's normal byte-size syntax (e.g. 512, 1K, 1MiB).

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>

#include "vblk.h"
#include "diskio.h"
#include "helper.h"

int cmd_write(int argc, char **argv)
{
    const char *devname;
    const char *hostfile;
    uint64_t rel_off = 0;
    vblk_t *vb;
    FILE *f = NULL;
    unsigned char buf[64 * 1024];
    uint64_t file_size = 0;
    uint64_t part_size;
    uint64_t abs_off;
    const char *key;
    uint64_t done = 0;

    if (argc != 3 && argc != 4) {
        fprintf(stderr, "usage: write <dev> <host-file> [offset]\n");
        return 1;
    }

    devname = argv[1];
    hostfile = argv[2];

    if (argc == 4) {
        int ok = 0;
        rel_off = parse_size(argv[3], &ok);
        if (!ok) {
            fprintf(stderr, "write: invalid offset '%s'\n", argv[3]);
            return 1;
        }
    }

    vb = vblk_open(devname);
    if (!vb) {
        fprintf(stderr, "write: cannot open device '%s'\n", devname);
        return 1;
    }

    f = fopen(hostfile, "rb");
    if (!f) {
        fprintf(stderr, "write: cannot open host file '%s'\n", hostfile);
        return 1;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "write: cannot seek host file '%s'\n", hostfile);
        fclose(f);
        return 1;
    }
    {
        long n = ftell(f);
        if (n < 0) {
            fprintf(stderr, "write: cannot determine size of '%s'\n", hostfile);
            fclose(f);
            return 1;
        }
        file_size = (uint64_t)n;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "write: cannot rewind host file '%s'\n", hostfile);
        fclose(f);
        return 1;
    }

    part_size = vb->lba_size * 512ull;
    if (rel_off > part_size || file_size > part_size - rel_off) {
        fprintf(stderr,
                "write: file does not fit in %s (offset=%" PRIu64
                ", file=%" PRIu64 ", device=%" PRIu64 " bytes)\n",
                devname, rel_off, file_size, part_size);
        fclose(f);
        return 1;
    }

    key = vb->dev[0] ? vb->dev : vb->name;
    abs_off = vb->lba_start * 512ull + rel_off;

    while (done < file_size) {
        size_t want = sizeof buf;
        uint64_t remain = file_size - done;
        if (remain < want) want = (size_t)remain;

        size_t got = fread(buf, 1, want, f);
        if (got != want) {
            fprintf(stderr, "write: read error on '%s'\n", hostfile);
            fclose(f);
            return 1;
        }
        if (!diskio_pwrite(key, abs_off + done, buf, (uint32_t)got)) {
            fprintf(stderr, "write: device write failed on %s at +%" PRIu64 "\n",
                    devname, rel_off + done);
            fclose(f);
            return 1;
        }
        done += (uint64_t)got;
    }

    fclose(f);
    printf("Wrote %" PRIu64 " bytes from %s to %s at +%" PRIu64 "\n",
           file_size, hostfile, devname, rel_off);
    return 0;
}
