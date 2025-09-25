// src/iso9660_walk.c

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include "iso9660.h"  // public API
#include "vblk.h"     // vblk_t, vblk_read_blocks
#include "debug.h"    // DBG(...)

// --- drop-in replacement: src/iso9660.c ---
static inline uint32_t u32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void iso_normalize_name(const char *raw, int raw_len, char *out, size_t out_cap) {
    // Strip version suffix (";1") and lowercase A..Z
    size_t o = 0;
    for (int i = 0; i < raw_len && o + 1 < out_cap; i++) {
        char c = raw[i];
        if (c == ';') break;                   // stop at version delimiter
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        out[o++] = c;
    }
    out[o] = '\0';
}

static int names_equal_ci(const char *a, const char *b) {
    // case-insensitive compare (ASCII)
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
    }
    return *a == '\0' && *b == '\0';
}

/**
 * Scan a single ISO9660 directory (at dir_lba, length dir_size bytes) for one component name.
 * If found, outputs the child's extent LBA/size/flags and returns 1.
 * If not found, returns 0. On read/parse error, returns -1.
 */
int iso_walk_component(const iso9660_t  *iso,
                          uint32_t dir_lba,
                          uint32_t dir_size,
                          const char *want,
                          uint32_t *out_lba,
                          uint32_t *out_size,
                          uint8_t  *out_flags)
{
    if (!iso || !iso->dev || !want) return -1;

    const uint32_t bs = 2048u;

    uint8_t sec[2048u];
    uint32_t bytes_left = dir_size;
    uint32_t cur_lba = dir_lba;
    uint32_t off_in_dir = 0;

    while (bytes_left > 0) {
        if (!iso_read_sector(iso, cur_lba, sec)) {
            DBG("iso: read error dir_lba=%u (cur_lba=%u)", dir_lba, cur_lba);
            return -1;
        }

        uint32_t in = 0;
        while (in < bs && bytes_left > 0) {
            uint8_t rec_len = sec[in + 0];
            if (rec_len == 0) break; // end of records in this sector

            if (in + rec_len > bs) {
                DBG("iso: truncated dirent (rec_len=%u beyond sector)", (unsigned)rec_len);
                return -1;
            }

            const uint8_t *rec = &sec[in];

            // Parse fields by spec offsets (LE copy)
            uint32_t child_lba  = u32le(&rec[2]);   // extent location (LBA)
            uint32_t child_size = u32le(&rec[10]);  // data length (bytes)
            uint8_t  flags      = rec[25];          // file flags
            uint8_t  name_len   = rec[32];
            const char *name_ptr = (const char *)&rec[33];

            if (33u + name_len > in + rec_len) {
                DBG("iso: bad name_len=%u (rec_len=%u)", (unsigned)name_len, (unsigned)rec_len);
                return -1;
            }

            // Skip special names 0x00="." and 0x01=".."
            int is_dot    = (name_len == 1 && (unsigned char)name_ptr[0] == 0x00);
            int is_dotdot = (name_len == 1 && (unsigned char)name_ptr[0] == 0x01);

            char clean[256];
            iso_normalize_name(name_ptr, (int)name_len, clean, sizeof clean);

            // DEBUG: show what we parsed
            DBG("iso: dirent raw='%.*s' clean='%s' flags=0x%02X (%s) lba=%u size=%u rec_len=%u off=%u",
                (int)name_len, name_ptr,
                clean,
                (unsigned)flags, (flags & 0x02) ? "DIR" : "FILE",
                (unsigned)child_lba, (unsigned)child_size, (unsigned)rec_len, (unsigned)off_in_dir);

            if (!is_dot && !is_dotdot && names_equal_ci(clean, want)) {
                if (out_lba)   *out_lba   = child_lba;
                if (out_size)  *out_size  = child_size;
                if (out_flags) *out_flags = flags;
                return 1; // found
            }

            in += rec_len;
            off_in_dir += rec_len;
            if (bytes_left >= rec_len) bytes_left -= rec_len; else bytes_left = 0;
        }

        cur_lba++;
    }

    return 0; // not found
}