#include <stdint.h>
#include "fs_probe.h"
#include "diskio.h"

bool probe_ext2_magic(const char *image_path, uint64_t fs_offset_bytes) {
    if (!image_path) return false;
    // Superblock is at +1024; magic is at +56 within the superblock.
    const uint64_t off = fs_offset_bytes + 1024 + 56;
    unsigned char buf[2];
    if (!file_pread(buf, 2, (size_t)off, image_path)) return false;
    const uint16_t magic = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8); // little-endian
    return magic == 0xEF53;
}

#include "vfs.h"
#include "vblk.h"

struct probe_ctx { vblk_t *dev; const filesystem_type_t *hit; };

static int probe_cb(const filesystem_type_t *fs, void *user) {
    struct probe_ctx *c = (struct probe_ctx*)user;
    if (!fs || !fs->probe) return 0;
    if (fs->probe(c->dev, NULL, 0)) { c->hit = fs; return 1; }  /* stop on first hit */
    return 0;
}

const filesystem_type_t *vfs_probe_any(vblk_t *dev) {
    struct probe_ctx c = { dev, NULL };
    (void)vfs_for_each_fs(probe_cb, &c);
    return c.hit;
}