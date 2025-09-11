// src/gpt_get_partition.c
#include <stdio.h>
#include <stdlib.h>
#include "gpt.h"
#include "diskio.h"

bool gpt_get_partition(const char *image_path, int part_index,
                       uint64_t *start_lba, uint64_t *total_sectors)
{
    if (!image_path || part_index <= 0 || !start_lba || !total_sectors)
        return false;

    GptHeader hdr;
    if (!gpt_read_header(image_path, &hdr, 1))  // LBA1 = primary GPT header
        return false;

    GptEntry *entries = NULL;
    if (!gpt_read_entries(image_path, &hdr, &entries))
        return false;

    bool ok = false;
    if (part_index <= (int)hdr.num_part_entries) {
        GptEntry *e = &entries[part_index - 1];
        if (e->first_lba && e->last_lba && e->first_lba <= e->last_lba) {
            *start_lba     = e->first_lba;
            *total_sectors = e->last_lba - e->first_lba + 1;
            ok = true;
        }
    }

    free(entries);
    return ok;
}