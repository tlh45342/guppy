// src/iso9660.c — Core ISO9660 (+ Joliet) reader, VFS-agnostic (no registration)

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

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

#ifdef ENABLE_FEATURE
static void trim_version_semicolon(char *s) {
    char *semi = strrchr(s, ';');
    if (semi) *semi = '\0';
}
#endif

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

/* ============================ Joliet & Names ============================ */

#ifdef ENABLE_FEATURE
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
#endif

/* ============================ ISO-sector Read Helpers ============================ */

/**
 *  name: iso_read_sector
 */
 
bool iso_read_sector(const iso9660_t *iso, uint32_t lba, void *dst)
{
    if (!iso || !iso->dev || !dst) return false;

    /* Be robust before mount sets dev->block_bytes=2048.
       Fall back to 512 if unset. */
    uint32_t dev_bs = iso->dev->block_bytes ? iso->dev->block_bytes : 512u;

    if (dev_bs == 2048u) {
        /* 1 device block == 2048 bytes */
        return vblk_read_blocks(iso->dev, lba, 1, dst);
    }

    /* If device block size divides 2048, read the ratio of blocks. */
    if ((2048u % dev_bs) == 0) {
        uint32_t ratio = ISO_SECTOR_SIZE / dev_bs;           /* e.g., 2048/512 = 4 */
        uint64_t dev_lba = (uint64_t)lba * ratio;            /* map ISO LBA -> device LBA */
        return vblk_read_blocks(iso->dev, dev_lba, ratio, dst);
    }

    /* Fallback: byte-addressed read if sizes don’t align cleanly. */
    return vblk_read_bytes(iso->dev, (uint64_t)lba * ISO_SECTOR_SIZE, ISO_SECTOR_SIZE, dst);
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

// little-endian 32-bit helper (local to this file)
static inline uint32_t iso_u32le(const uint8_t *p) {
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

/**
 * iso_mount
 * ----------
 * Probe 'dev' for an ISO-9660 filesystem by reading the Primary Volume
 * Descriptor at LBA 16 and parsing the root directory record. On success,
 * fills 'out' and marks the backing vblk as 2048-byte, read-only media.
 *
 * Returns: true on success, false on any failure (with DBG reason).
 */
bool iso_mount(vblk_t *dev, iso9660_t *out)
{
    if (!dev || !out) {
        DBG("iso_mount: invalid args dev=%p out=%p", (void*)dev, (void*)out);
        return false;
    }

    DBG("iso_mount: enter devkey='%s' lb_start=%llu lb_size=%llu dev.block_bytes=%u",
        dev->dev,
        (unsigned long long)dev->lba_start,
        (unsigned long long)dev->lba_size,
        dev->block_bytes);

    // Use a temporary iso context for sector reads (we only need ->dev)
    iso9660_t tmp = {0};
    tmp.dev = dev;

    // Read the Primary Volume Descriptor (PVD) at ISO LBA 16
    uint8_t pvd[ISO_SECTOR_SIZE];
    if (!iso_read_sector(&tmp, 16u, pvd)) {
        DBG("iso_mount: fail: iso_read_sector@16");
        return false;
    }
    DBG("iso_mount: read PVD@16 ok");

    // Validate PVD header
    const uint8_t type    = pvd[0];              // 1 = Primary Volume Descriptor
    const char   *ident   = (const char *)&pvd[1]; // "CD001"
    const uint8_t ver     = pvd[6];              // 1
    if (type != 1) {
        DBG("iso_mount: not a PVD (type=%u)", type);
        return false;
    }
    if (memcmp(ident, "CD001", 5) != 0) {
        DBG("iso_mount: bad magic: '%c%c%c%c%c'",
            pvd[1], pvd[2], pvd[3], pvd[4], pvd[5]);
        return false;
    }
    if (ver != 1) {
        DBG("iso_mount: unsupported PVD version=%u", ver);
        return false;
    }

    // Root Directory Record lives at byte offset 156 within the PVD
    const uint8_t *rr = &pvd[156];
    const uint8_t  rr_len = rr[0];
    if (rr_len < 34) { // minimum size of a directory record
        DBG("iso_mount: bad root dir record length=%u", rr_len);
        return false;
    }

    const uint32_t root_lba  = iso_u32le(&rr[2]);   // extent location (LE copy)
    const uint32_t root_size = iso_u32le(&rr[10]);  // extent size in bytes
    const uint8_t  flags     = rr[25];              // should have DIR bit set

    if ((flags & 0x02) == 0) {
        DBG("iso_mount: root record does not have DIR flag (flags=0x%02X)", flags);
        return false;
    }
    if (root_lba == 0 || root_size == 0) {
        DBG("iso_mount: invalid root extent lba=%u size=%u", root_lba, root_size);
        return false;
    }

    // Fill the outgoing iso context
    memset(out, 0, sizeof *out);
    out->dev        = dev;
    out->root_lba   = root_lba;
    out->root_size  = root_size;
    out->block_size = ISO_SECTOR_SIZE;

    // Log in the same style you were already using
    DBG("mount: Primary, root=[lba=%u size=%u] bs=%u", out->root_lba, out->root_size, out->block_size);

    // Adopt CD geometry on the vblk so subsequent reads can do "count=1"
    dev->block_bytes = ISO_SECTOR_SIZE;
    dev->ro          = true;
    DBG("mount: mounted; set vblk '%s' block_bytes=%u ro=%d", dev->dev, dev->block_bytes, dev->ro ? 1 : 0);

    DBG("iso_mount: success");
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
        // char dirpath[512];
		uint32_t file_lba = 0, file_sz = 0;
		uint8_t  flags    = 0;

		int found = iso_walk_component(iso, dir_lba, dir_sz, last,
									   &file_lba, &file_sz, &flags);
		if (found != 1) return false;          // 1=found, 0=not found, -1=error
		if (flags & 0x02) {                    // directory bit -> not a regular file
			// optional: only if your callers check errno
			errno = EISDIR;                    // requires <errno.h>
			return false;
		}
    }
    if (*last == '\0') return false; // path ends with '/'

	uint32_t file_lba = 0, file_sz = 0;
	uint8_t  flags    = 0;

	int found = iso_walk_component(iso, dir_lba, dir_sz, last,
								   &file_lba, &file_sz, &flags);
	if (found != 1) return false;          // 1=found, 0=not found, -1=error
	if (flags & 0x02) {                    // directory bit -> not a regular file
		// optional: only if your callers check errno
		errno = EISDIR;                    // requires <errno.h>
		return false;
	}

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

	uint32_t lba = 0, size = 0;
	uint8_t  flags = 0;

	int found = iso_walk_component(iso, dir_lba, dir_sz, last,
								   &lba, &size, &flags);
	if (found != 1) return false;

	*out_lba    = lba;
	*out_size   = size;
	*out_is_dir = (flags & 0x02) ? 1 : 0;
	
    return true;
}

/* ----- lookup: thin wrapper around the walker -------------------------- */
bool iso_lookup_dir(iso9660_t *iso,
                    const char *dirpath,
                    uint32_t *out_lba,
                    uint32_t *out_size)
{
    if (!iso || !dirpath || !out_lba || !out_size) return false;
    if (dirpath[0] != '/') return false;   // must be absolute

    uint32_t cur_lba  = iso->root_lba;
    uint32_t cur_size = iso->root_size;

    // Skip leading slashes; treat "/" as root.
    const char *p = dirpath;
    while (*p == '/') ++p;
    if (*p == '\0') { *out_lba = cur_lba; *out_size = cur_size; return true; }

    char comp[256];
    while (*p) {
        // Extract next path component (no leading '/')
        size_t j = 0;
        while (p[j] && p[j] != '/' && j + 1 < sizeof comp) { comp[j] = p[j]; ++j; }
        comp[j] = '\0';
        p += j;
        while (*p == '/') ++p; // collapse multiple '/'

        if (comp[0] == '\0' || (comp[0] == '.' && comp[1] == '\0')) continue;
        if (comp[0] == '.' && comp[1] == '.' && comp[2] == '\0') {
            // ISO9660 root has no parent; stay at root
            continue;
        }

        // Look up the next component inside the current directory
        uint32_t child_lba = 0, child_size = 0; uint8_t flags = 0;
        int found = iso_walk_component(iso, cur_lba, cur_size, comp, &child_lba, &child_size, &flags);
        if (found != 1) return false;          // not found or error
        if (!(flags & 0x02)) return false;     // must be a directory

        cur_lba  = child_lba;
        cur_size = child_size;
    }

    *out_lba  = cur_lba;
    *out_size = cur_size;
    return true;
}