// src/iso9660.c — Core ISO9660 (+ Joliet) reader, VFS-agnostic (no registration)

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include "iso9660.h"  // public API
#include "vblk.h"     // vblk_t, vblk_read_blocks
#include "debug.h"    // DBG(...)

/* ============================ Config & Debug Helpers ============================ */

#ifndef ISO_MAX_BS
#define ISO_MAX_BS 4096u
#endif

#ifndef ISO_TRACE_LIMIT
#define ISO_TRACE_LIMIT (1u<<20)  /* 1 MiB safety */
#endif

/* ============================ Small Helpers ============================ */

static inline uint16_t rd_le16(const void *p) {
    const uint8_t *q = (const uint8_t *)p;
    return (uint16_t)q[0] | ((uint16_t)q[1] << 8);
}
static inline uint32_t rd_le32(const void *p) {
    const uint8_t *q = (const uint8_t *)p;
    return (uint32_t)q[0] | ((uint32_t)q[1] << 8) | ((uint32_t)q[2] << 16) | ((uint32_t)q[3] << 24);
}
static inline int is_dot_special(const uint8_t *fi, uint8_t fi_len) {
    return (fi_len == 1) && (fi[0] == 0x00 || fi[0] == 0x01);  // "." (0x00) / ".." (0x01)
}
static inline int to_upper_ascii(int c) {
    return (c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : c;
}
static void trim_version_semicolon(char *s) {
    char *semi = strrchr(s, ';');
    if (semi) *semi = '\0';
}

/* ============================ On-Disk Structures ============================ */

#pragma pack(push,1)
typedef struct {
    uint8_t  len_dr;          // record length
    uint8_t  ext_attr_len;
    uint32_t extent_lba_le;
    uint32_t extent_lba_be;
    uint32_t data_len_le;
    uint32_t data_len_be;
    uint8_t  rec_time[7];
    uint8_t  flags;           // 0x02 = directory
    uint8_t  unit_size;
    uint8_t  gap_size;
    uint16_t vol_seq_le;
    uint16_t vol_seq_be;
    uint8_t  fi_len;          // file identifier length
    // followed by FI bytes (fi_len), then optional padding if fi_len is odd
} iso_dirrec_t;
#pragma pack(pop)

static bool iso_dirrec_fits(const uint8_t *blk, uint32_t bs, uint32_t off) {
    if (off + sizeof(iso_dirrec_t) > bs) return false;
    const iso_dirrec_t *dr = (const iso_dirrec_t *)(blk + off);
    uint8_t len = dr->len_dr;
    if (len == 0) return true; /* padding at end of sector */
    uint8_t        fiLen = dr->fi_len;
    uint32_t need = (uint32_t)sizeof(*dr) + (uint32_t)fiLen + ((fiLen & 1u) ? 1u : 0u);
    if (need > len) return false;
    return (off + len) <= bs;
}

/* ============================ Joliet & Names ============================ */

static int is_joliet_svd(const uint8_t *sec /* 2048 bytes */) {
    if (sec[0] != 2) return 0;                // Type 2 = SVD
    if (memcmp(sec + 1, "CD001", 5) != 0) return 0;
    if (sec[6] != 1) return 0;
    const uint8_t *esc = sec + 88;            // escape seq
    return (esc[0] == 0x25 && esc[1] == 0x2F &&
            (esc[2] == 0x40 || esc[2] == 0x43 || esc[2] == 0x45));
}

static size_t ucs2be_to_utf8(const uint8_t *in, size_t in_len, char *out, size_t out_cap) {
    size_t oi = 0;
    for (size_t i = 0; i + 1 < in_len; i += 2) {
        unsigned ch = ((unsigned)in[i] << 8) | (unsigned)in[i+1];
        if (ch == 0) break;
        if (ch < 0x80) {
            if (oi + 1 >= out_cap) break;
            out[oi++] = (char)ch;
        } else if (ch < 0x800) {
            if (oi + 2 >= out_cap) break;
            out[oi++] = (char)(0xC0 | (ch >> 6));
            out[oi++] = (char)(0x80 | (ch & 0x3F));
        } else {
            if (oi + 3 >= out_cap) break;
            out[oi++] = (char)(0xE0 | (ch >> 12));
            out[oi++] = (char)(0x80 | ((ch >> 6) & 0x3F));
            out[oi++] = (char)(0x80 | (ch & 0x3F));
        }
    }
    if (oi < out_cap) out[oi] = '\0';
    return oi;
}

static void decode_dir_name(const uint8_t *fi, uint8_t fi_len, int use_joliet,
                            char *out, size_t out_cap)
{
    if (is_dot_special(fi, fi_len)) { // "." / ".."
        snprintf(out, out_cap, (fi[0] == 0x00) ? "." : "..");
        return;
    }
    if (use_joliet) {
        (void)ucs2be_to_utf8(fi, fi_len, out, out_cap);
    } else {
        size_t n = (fi_len < out_cap - 1) ? fi_len : (out_cap - 1);
        memcpy(out, fi, n); out[n] = '\0';
        for (char *p = out; *p; ++p) *p = (char)to_upper_ascii((unsigned char)*p);
        trim_version_semicolon(out);
    }
}

static int name_matches(const char *decoded, const char *want, int use_joliet) {
    if (use_joliet) {
        return strcmp(decoded, want) == 0; // Joliet: case-preserving, exact
    } else {
        char w[256];
        size_t wl = strlen(want);
        if (wl >= sizeof(w)) wl = sizeof(w) - 1;
        memcpy(w, want, wl); w[wl] = '\0';
        for (char *p = w; *p; ++p) *p = (char)to_upper_ascii((unsigned char)*p);
        return strcmp(decoded, w) == 0;
    }
}

/* ============================ ISO-sector Read Helpers ============================ */

/* Read one ISO sector (iso->block_size bytes) at ISO-LBA 'lba' into dst. */
static bool iso_read_sector(const iso9660_t *iso, uint32_t lba, void *dst) {
    uint32_t bs = iso->block_size ? iso->block_size : 2048u;
    uint32_t count512 = bs / 512u;
    if (count512 == 0 || (bs % 512u) != 0) return false;
    /* Map 1 ISO-LBA to N 512-byte blocks */
    return vblk_read_blocks(iso->dev, lba * count512, count512, dst);
}

/* ============================ Block I/O Wrapper ============================ */

static bool read_blocks(iso9660_t *iso, uint32_t lba, uint32_t count, void *dst) {
    /* Read 'count' ISO sectors (each iso->block_size bytes) */
    for (uint32_t i = 0; i < count; ++i) {
        if (!iso_read_sector(iso, lba + i, (uint8_t*)dst + (i * iso->block_size))) return false;
    }
    return true;
}

/* ============================ Public API ============================ */

bool iso_mount(vblk_t *dev, iso9660_t *out) {
    if (!dev || !out) return false;

    uint8_t sec[2048];

    /* Bootstrap read of PVD @ ISO-LBA 16 using assumed 2048 bytes,
       because we don't know block_size yet. */
    {
        iso9660_t tmp = { .dev = dev, .block_size = 2048, .use_joliet = 0 };
        if (!iso_read_sector(&tmp, 16, sec)) return false;
    }

    if (!(sec[0] == 1 && memcmp(sec + 1, "CD001", 5) == 0 && sec[6] == 1)) return false;

    const uint16_t bs = rd_le16(sec + 128); // logical block size (LE)
    out->dev         = dev;
    out->block_size  = bs ? bs : 2048;
    out->use_joliet  = 0;

    if (out->block_size > ISO_MAX_BS) return false;

    // PVD root record at byte 156
    const uint8_t *root = sec + 156;
    out->pvd_lba   = 16;
    out->root_lba  = rd_le32(root + 2);
    out->root_size = rd_le32(root + 10);

    // Probe SVDs (17..19) for Joliet
    for (int l = 17; l <= 19; ++l) {
        if (!iso_read_sector(out, (uint32_t)l, sec)) break;
        if (is_joliet_svd(sec)) {
            const uint8_t *svd_root = sec + 156;
            out->root_lba   = rd_le32(svd_root + 2);
            out->root_size  = rd_le32(svd_root + 10);
            out->use_joliet = 1;
            break;
        }
    }

    DBG("mount: %s, root=[lba=%u size=%u] bs=%u",
        out->use_joliet ? "Joliet" : "Primary", out->root_lba, out->root_size, out->block_size);
    return true;
}

static int walk_component(iso9660_t *iso,
                          uint32_t cur_lba, uint32_t cur_size,
                          const char *comp,
                          uint32_t *out_lba, uint32_t *out_size, int *out_is_dir)
{
    const uint32_t bs = iso->block_size;
    if (bs > ISO_MAX_BS) return 0;

    uint8_t buf[ISO_MAX_BS];
    uint32_t remaining = cur_size;
    uint32_t lba = cur_lba;

    while (remaining > 0) {
        if (!read_blocks(iso, lba, 1, buf)) return 0;

        uint32_t off = 0;
        while (off + sizeof(iso_dirrec_t) <= bs) {
            if (!iso_dirrec_fits(buf, bs, off)) break;

            const iso_dirrec_t *dr = (const iso_dirrec_t *)(buf + off);
            if (dr->len_dr == 0) break; /* padding */

            const uint8_t *fi = (const uint8_t *)dr + sizeof(*dr);
            const uint8_t  fi_len = dr->fi_len;

            if (!is_dot_special(fi, fi_len)) {
                char name[256];
                decode_dir_name(fi, fi_len, iso->use_joliet, name, sizeof(name));
                const int is_dir = (dr->flags & 0x02) ? 1 : 0;

                if (name_matches(name, comp, iso->use_joliet)) {
                    *out_lba    = rd_le32(&dr->extent_lba_le);
                    *out_size   = rd_le32(&dr->data_len_le);
                    *out_is_dir = is_dir;
                    return 1;
                }
            }

            off += dr->len_dr;
        }

        lba++;
        remaining = (remaining > bs) ? (remaining - bs) : 0;
    }
    return 0;
}

bool iso_lookup_dir(iso9660_t *iso, const char *path,
                    uint32_t *out_lba, uint32_t *out_size)
{
    if (!iso || !path || !out_lba || !out_size) return false;
    if (path[0] != '/') return false;

    if (path[1] == '\0') {
        *out_lba = iso->root_lba;
        *out_size = iso->root_size;
        return true;
    }

    uint32_t cur_lba  = iso->root_lba;
    uint32_t cur_size = iso->root_size;

    const char *p = path + 1;
    char comp[256];

    while (*p) {
        size_t ci = 0;
        while (*p && *p != '/' && ci < sizeof(comp) - 1) comp[ci++] = *p++;
        comp[ci] = '\0';
        while (*p == '/') p++;

        uint32_t next_lba, next_size; int is_dir;
        if (!walk_component(iso, cur_lba, cur_size, comp, &next_lba, &next_size, &is_dir))
            return false;
        if (!is_dir && *p) return false; // file in the middle of path
        cur_lba = next_lba; cur_size = next_size;
    }

    *out_lba  = cur_lba;
    *out_size = cur_size;
    return true;
}

bool iso_list_dir(iso9660_t *iso, uint32_t dir_lba, uint32_t dir_size,
                  iso_dirent_cb cb, void *user)
{
    if (!iso || !cb) return false;

    const uint32_t bs = iso->block_size;
    if (bs > ISO_MAX_BS) return false;

    uint8_t buf[ISO_MAX_BS];
    uint32_t remaining = dir_size;
    uint32_t lba = dir_lba;

    while (remaining > 0) {
        if (!read_blocks(iso, lba, 1, buf)) return false;

        uint32_t off = 0;
        while (off + sizeof(iso_dirrec_t) <= bs) {
            if (!iso_dirrec_fits(buf, bs, off)) break;

            const iso_dirrec_t *dr = (const iso_dirrec_t *)(buf + off);
            if (dr->len_dr == 0) break;

            const uint8_t *fi = (const uint8_t *)dr + sizeof(*dr);
            const uint8_t  fi_len = dr->fi_len;

            if (!is_dot_special(fi, fi_len)) {
                char name[256];
                decode_dir_name(fi, fi_len, iso->use_joliet, name, sizeof(name));
                const int is_dir = (dr->flags & 0x02) ? 1 : 0;
                cb(name, is_dir, user);
            }

            off += dr->len_dr;
        }

        lba++;
        remaining = (remaining > bs) ? (remaining - bs) : 0;
    }
    return true;
}

bool iso_read_file_by_path(iso9660_t *iso,
                           const char *path,
                           void *out_buf,
                           uint32_t out_cap,
                           uint32_t *out_len)
{
    if (out_len) *out_len = 0;
    if (!iso || !path || !out_buf) return false;
    if (path[0] != '/') return false;

    uint32_t dir_lba = iso->root_lba;
    uint32_t dir_sz  = iso->root_size;

    const char *slash = strrchr(path + 1, '/');
    const char *last  = (slash ? slash + 1 : path + 1);

    if (slash) {
        char dirpath[512];
        size_t n = (size_t)(slash - path);
        if (n >= sizeof(dirpath)) return false;
        memcpy(dirpath, path, n);
        dirpath[n] = '\0';
        if (!iso_lookup_dir(iso, dirpath, &dir_lba, &dir_sz)) return false;
    }
    if (*last == '\0') return false; // path ends with '/'

    uint32_t file_lba = 0, file_sz = 0; int is_dir = 0;
    if (!walk_component(iso, dir_lba, dir_sz, last, &file_lba, &file_sz, &is_dir)) return false;
    if (is_dir) return false;

    const uint32_t bs = iso->block_size;
    if (bs > ISO_MAX_BS) return false;

    uint32_t remaining = file_sz;
    uint32_t lba = file_lba;
    uint32_t written = 0;

    while (remaining > 0) {
        uint8_t blk[ISO_MAX_BS];
        if (!read_blocks(iso, lba, 1, blk)) return false;
        uint32_t chunk = (remaining > bs) ? bs : remaining;

        if (written + chunk > out_cap) return false;
        memcpy((uint8_t*)out_buf + written, blk, chunk);
        written   += chunk;
        remaining -= chunk;
        lba++;
    }

    if (out_len) *out_len = written;
    return true;
}

bool iso_stat_path(iso9660_t *iso, const char *path,
                   uint32_t *out_lba, uint32_t *out_size, int *out_is_dir)
{
    if (!iso || !path || !out_lba || !out_size || !out_is_dir) return false;
    if (path[0] != '/') return false;

    uint32_t dir_lba = iso->root_lba;
    uint32_t dir_sz  = iso->root_size;

    const char *slash = strrchr(path + 1, '/');
    const char *last  = (slash ? slash + 1 : path + 1);

    if (slash) {
        char dirpath[512];
        size_t n = (size_t)(slash - path);
        if (n >= sizeof(dirpath)) return false;
        memcpy(dirpath, path, n);
        dirpath[n] = '\0';
        if (!iso_lookup_dir(iso, dirpath, &dir_lba, &dir_sz)) return false;
    }
    if (*last == '\0') return false;

    uint32_t lba=0, size=0; int is_dir=0;
    if (!walk_component(iso, dir_lba, dir_sz, last, &lba, &size, &is_dir)) return false;

    *out_lba = lba;
    *out_size = size;
    *out_is_dir = is_dir;
    return true;
}
