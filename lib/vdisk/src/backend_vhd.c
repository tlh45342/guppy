#define _FILE_OFFSET_BITS 64
#include "vdisk_internal.h"

#include <stdint.h>
#include <string.h>
#include <time.h>

#define VHD_FOOTER_SIZE 512U
#define VHD_SECTOR_SIZE 512U
#define VHD_TYPE_FIXED  2U
#define VHD_EPOCH_DELTA 946684800ULL

static uint16_t be16(const unsigned char *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}
static uint32_t be32(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static uint64_t be64(const unsigned char *p) {
    return ((uint64_t)be32(p) << 32) | (uint64_t)be32(p + 4);
}
static void put16be(unsigned char *p, uint16_t v) {
    p[0] = (unsigned char)(v >> 8);
    p[1] = (unsigned char)v;
}
static void put32be(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}
static void put64be(unsigned char *p, uint64_t v) {
    put32be(p, (uint32_t)(v >> 32));
    put32be(p + 4, (uint32_t)v);
}

static uint32_t footer_checksum(const unsigned char footer[VHD_FOOTER_SIZE]) {
    uint32_t sum = 0;
    unsigned i;
    for (i = 0; i < VHD_FOOTER_SIZE; ++i) {
        if (i >= 64U && i < 68U) continue;
        sum += footer[i];
    }
    return ~sum;
}

static void geometry_for(uint64_t size_bytes, uint16_t *cylinders,
                         uint8_t *heads, uint8_t *sectors) {
    uint64_t total_sectors = size_bytes / VHD_SECTOR_SIZE;
    uint64_t cyl_times_heads;
    uint32_t h, spt;
    const uint64_t max_sectors = 65535ULL * 16ULL * 255ULL;

    if (total_sectors > max_sectors) total_sectors = max_sectors;

    if (total_sectors >= 65535ULL * 16ULL * 63ULL) {
        spt = 255U;
        h = 16U;
        cyl_times_heads = total_sectors / spt;
    } else {
        spt = 17U;
        cyl_times_heads = total_sectors / spt;
        h = (uint32_t)((cyl_times_heads + 1023ULL) / 1024ULL);
        if (h < 4U) h = 4U;
        if (cyl_times_heads >= (uint64_t)h * 1024ULL || h > 16U) {
            spt = 31U;
            h = 16U;
            cyl_times_heads = total_sectors / spt;
        }
        if (cyl_times_heads >= (uint64_t)h * 1024ULL) {
            spt = 63U;
            h = 16U;
            cyl_times_heads = total_sectors / spt;
        }
    }

    if (h == 0U) h = 1U;
    *cylinders = (uint16_t)(cyl_times_heads / h);
    *heads = (uint8_t)h;
    *sectors = (uint8_t)spt;
}

static uint32_t vhd_timestamp_now(void) {
    time_t now = time(NULL);
    uint64_t unix_secs;
    if (now <= 0) return 0;
    unix_secs = (uint64_t)now;
    if (unix_secs <= VHD_EPOCH_DELTA) return 0;
    unix_secs -= VHD_EPOCH_DELTA;
    return unix_secs > UINT32_MAX ? UINT32_MAX : (uint32_t)unix_secs;
}

static void make_uuid(unsigned char out[16], uint64_t size_bytes) {
    uint64_t x = ((uint64_t)vhd_timestamp_now() << 32) ^ size_bytes ^
                 0x4755505059564844ULL; /* "GUPPYVHD"-ish seed */
    unsigned i;
    if (x == 0) x = 0x9e3779b97f4a7c15ULL;
    for (i = 0; i < 16U; ++i) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        out[i] = (unsigned char)(x >> ((i & 7U) * 8U));
    }
    /* RFC 4122-looking random UUID bits; VHD treats this as opaque uniqueness. */
    out[6] = (unsigned char)((out[6] & 0x0fU) | 0x40U);
    out[8] = (unsigned char)((out[8] & 0x3fU) | 0x80U);
}

static int read_footer(FILE *fp, unsigned char footer[VHD_FOOTER_SIZE],
                       uint64_t *host_size_out) {
    uint64_t host_size;
    if (vdisk_host_get_size(fp, &host_size) != 0 || host_size < VHD_FOOTER_SIZE)
        return -1;
    if (vdisk_host_read(fp, host_size - VHD_FOOTER_SIZE,
                        footer, VHD_FOOTER_SIZE) != 0)
        return -1;
    if (host_size_out) *host_size_out = host_size;
    return 0;
}

static int fixed_footer_valid(const unsigned char f[VHD_FOOTER_SIZE],
                              uint64_t host_size, uint64_t *logical_out) {
    uint64_t current_size;
    if (memcmp(f, "conectix", 8) != 0) return 0;
    if (be32(f + 60) != VHD_TYPE_FIXED) return 0;
    if (be64(f + 16) != UINT64_MAX) return 0;
    if (be32(f + 12) != 0x00010000U) return 0;
    if (be32(f + 64) != footer_checksum(f)) return 0;
    current_size = be64(f + 48);
    if (current_size == 0 || (current_size % VHD_SECTOR_SIZE) != 0) return 0;
    if (host_size != current_size + VHD_FOOTER_SIZE) return 0;
    if (logical_out) *logical_out = current_size;
    return 1;
}

static int vhd_probe(FILE *fp) {
    unsigned char f[VHD_FOOTER_SIZE];
    uint64_t host_size;
    if (read_footer(fp, f, &host_size) != 0) return 0;
    return fixed_footer_valid(f, host_size, NULL);
}

static int vhd_open(vdisk_t *d) {
    unsigned char f[VHD_FOOTER_SIZE];
    uint64_t host_size, logical_size;
    if (!d || !d->fp) return -1;
    if (read_footer(d->fp, f, &host_size) != 0) return -1;
    if (!fixed_footer_valid(f, host_size, &logical_size)) return -1;

    d->size_bytes = logical_size;
    d->host_size_bytes = host_size;
    d->data_offset = 0;
    d->block_size = VHD_SECTOR_SIZE;
    d->block_extra = 0;
    d->blocks_offset = 0;
    {
        uint64_t sectors = logical_size / VHD_SECTOR_SIZE;
        d->blocks_count = sectors > UINT32_MAX ? UINT32_MAX : (uint32_t)sectors;
        d->blocks_allocated = d->blocks_count;
    }
    d->image_type = VHD_TYPE_FIXED;
    return 0;
}

static int build_footer(unsigned char f[VHD_FOOTER_SIZE], uint64_t size_bytes) {
    uint16_t cylinders;
    uint8_t heads, sectors;
    unsigned char uuid[16];

    if (size_bytes == 0 || (size_bytes % VHD_SECTOR_SIZE) != 0) return -1;
    memset(f, 0, VHD_FOOTER_SIZE);
    memcpy(f, "conectix", 8);
    put32be(f + 8, 0x00000002U);       /* reserved feature bit */
    put32be(f + 12, 0x00010000U);      /* VHD format 1.0 */
    put64be(f + 16, UINT64_MAX);       /* no dynamic header for fixed VHD */
    put32be(f + 24, vhd_timestamp_now());
    memcpy(f + 28, "GUPP", 4);
    put32be(f + 32, 0x00010000U);
    memcpy(f + 36, "Wi2k", 4);
    put64be(f + 40, size_bytes);
    put64be(f + 48, size_bytes);
    geometry_for(size_bytes, &cylinders, &heads, &sectors);
    put16be(f + 56, cylinders);
    f[58] = heads;
    f[59] = sectors;
    put32be(f + 60, VHD_TYPE_FIXED);
    make_uuid(uuid, size_bytes);
    memcpy(f + 68, uuid, sizeof uuid);
    f[84] = 0;                         /* saved state */
    put32be(f + 64, footer_checksum(f));
    return 0;
}

static int vhd_create_fixed(vdisk_t *d, uint64_t size_bytes) {
    unsigned char footer[VHD_FOOTER_SIZE];
    if (!d || !d->fp) return -1;
    if (build_footer(footer, size_bytes) != 0) return -1;

    /* Materialize the fixed guest-data area, then append its 512-byte footer. */
    if (vdisk_host_seek(d->fp, size_bytes - 1U) != 0 || fputc(0, d->fp) == EOF)
        return -1;
    if (vdisk_host_write(d->fp, size_bytes, footer, sizeof footer) != 0 ||
        fflush(d->fp) != 0)
        return -1;

    d->size_bytes = size_bytes;
    d->host_size_bytes = size_bytes + VHD_FOOTER_SIZE;
    d->data_offset = 0;
    d->block_size = VHD_SECTOR_SIZE;
    d->block_extra = 0;
    d->blocks_offset = 0;
    {
        uint64_t sectors = size_bytes / VHD_SECTOR_SIZE;
        d->blocks_count = sectors > UINT32_MAX ? UINT32_MAX : (uint32_t)sectors;
        d->blocks_allocated = d->blocks_count;
    }
    d->image_type = VHD_TYPE_FIXED;
    return 0;
}

static int bounds_ok(const vdisk_t *d, uint64_t off, size_t len) {
    return d && off <= d->size_bytes && (uint64_t)len <= d->size_bytes - off;
}

static int vhd_read(vdisk_t *d, uint64_t off, void *buf, size_t len) {
    if (!bounds_ok(d, off, len)) return -1;
    return vdisk_host_read(d->fp, off, buf, len);
}

static int vhd_write(vdisk_t *d, uint64_t off, const void *buf, size_t len) {
    if (!d || !d->writable || !bounds_ok(d, off, len)) return -1;
    return vdisk_host_write(d->fp, off, buf, len);
}

static int vhd_flush(vdisk_t *d) { return fflush(d->fp) == 0 ? 0 : -1; }
static void vhd_close(vdisk_t *d) { (void)d; }

const vdisk_driver_t vdisk_vhd_driver = {
    VDISK_FMT_VHD, "vhd", vhd_probe, vhd_open, vhd_create_fixed, NULL,
    vhd_read, vhd_write, vhd_flush, vhd_close
};

int vdisk_vhd_inspect(FILE *fp, vdisk_info_t *info) {
    unsigned char f[VHD_FOOTER_SIZE];
    uint64_t host_size, current_size, original_size, data_offset;
    uint32_t features, version, type, checksum;
    uint16_t cylinders;
    uint8_t heads, sectors;

#define CHECK(c) do { if (c) info->checks_passed++; else info->checks_failed++; } while (0)

    if (!fp || !info) return -1;
    memset(info, 0, sizeof(*info));
    info->format = VDISK_FMT_VHD;
    if (read_footer(fp, f, &host_size) != 0) return -1;

    features = be32(f + 8);
    version = be32(f + 12);
    data_offset = be64(f + 16);
    original_size = be64(f + 40);
    current_size = be64(f + 48);
    cylinders = be16(f + 56);
    heads = f[58];
    sectors = f[59];
    type = be32(f + 60);
    checksum = be32(f + 64);

    info->host_size_bytes = host_size;
    info->logical_size_bytes = current_size;
    info->data_offset = 0;
    info->image_type = type;
    info->sector_size = VHD_SECTOR_SIZE;
    info->block_size = VHD_SECTOR_SIZE;
    if (current_size / VHD_SECTOR_SIZE <= UINT32_MAX) {
        info->blocks_count = (uint32_t)(current_size / VHD_SECTOR_SIZE);
        info->blocks_allocated = info->blocks_count;
    }

    if (type == VHD_TYPE_FIXED) {
        strcpy(info->variant, "fixed/full");
        info->supported = true;
    } else if (type == 3U) {
        strcpy(info->variant, "dynamic");
        info->supported = false;
    } else if (type == 4U) {
        strcpy(info->variant, "differencing");
        info->supported = false;
    } else {
        strcpy(info->variant, "unknown");
        info->supported = false;
    }

    CHECK(memcmp(f, "conectix", 8) == 0);
    CHECK(features == 0x00000002U);
    CHECK(version == 0x00010000U);
    CHECK(type == VHD_TYPE_FIXED ? data_offset == UINT64_MAX : 1);
    CHECK(current_size != 0 && (current_size % VHD_SECTOR_SIZE) == 0);
    CHECK(original_size == current_size);
    CHECK(cylinders != 0 && heads != 0 && sectors != 0);
    CHECK(checksum == footer_checksum(f));
    if (type == VHD_TYPE_FIXED)
        CHECK(host_size == current_size + VHD_FOOTER_SIZE);

    info->structurally_valid = (info->checks_failed == 0);
    return 0;
#undef CHECK
}
