// include/devmap.h
#pragma once
#include <stdint.h>
#include <stdbool.h>

/* Existing */
bool        devmap_add(const char *dev, const char *image_path);
const char *devmap_resolve(const char *dev);            // returns path or NULL
void        devmap_list(void);

/* New: expose the mapped LBA range (in 512-byte LBAs) */
bool devmap_query_range(const char *dev, uint64_t *out_lba_base, uint64_t *out_lba_count);

/* Optional: all-in-one getter */
static inline bool devmap_get(const char *dev,
                              const char **out_path,
                              uint64_t *out_lba_base,
                              uint64_t *out_lba_count)
{
    if (out_path) *out_path = devmap_resolve(dev);
    if (out_lba_base || out_lba_count) {
        if (!devmap_query_range(dev, out_lba_base, out_lba_count)) return false;
    }
    return (out_path ? (*out_path != NULL) : true);
}
