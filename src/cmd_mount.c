// src/cmd_mount.c
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdlib.h>   // free

#include "cmd.h"
#include "use.h"
#include "gpt.h"

static bool parse_dev_letter(const char *s, char *out) {
    if (!s || !*s) return false;
    if (strncmp(s, "/dev/", 5) == 0 && s[5] && !s[6]) { *out = (char)tolower((unsigned char)s[5]); return true; }
    if (!s[1] && isalpha((unsigned char)s[0]))        { *out = (char)tolower((unsigned char)s[0]); return true; }
    return false;
}

static void list_one(char letter, const char *path, bool verbose) {
    if (!verbose) { printf("/dev/%c  %s\n", letter, path); return; }

    printf("/dev/%c  %s\n", letter, path);

    GptHeader h;
    if (gpt_read_header(path, &h, 1)) {
        char disk_guid[37]; gpt_guid_to_str(h.disk_guid, disk_guid);
        printf("  scheme: GPT  disk-guid: %s  entries:%u size:%u  primary@LBA%llu backup@LBA%llu\n",
               disk_guid,
               h.num_part_entries, h.part_entry_size,
               (unsigned long long)h.current_lba,
               (unsigned long long)h.backup_lba);

        GptEntry *ents = NULL;
        if (!gpt_read_entries(path, &h, &ents)) { printf("  (failed to read GPT entries)\n"); return; }

        const size_t total = (size_t)h.num_part_entries * (size_t)h.part_entry_size;
        printf("  Idx   Start LBA       End LBA        Size     Type        Name\n");
        for (unsigned i = 0; i < h.num_part_entries; i++) {
            size_t off = (size_t)i * (size_t)h.part_entry_size;
            if (off + sizeof(GptEntry) > total) break;
            const GptEntry *e = (const GptEntry*)((const unsigned char*)ents + off);

            bool empty = true; for (int k=0;k<16;k++) if (e->type_guid[k]) { empty=false; break; }
            if (empty) continue;

            const char *alias = gpt_alias_for_type(e->type_guid);
            char type_guid[37]; gpt_guid_to_str(e->type_guid, type_guid);

            uint16_t u16name[36];
            memcpy(u16name, e->name_utf16, sizeof u16name);
            char name[128]; gpt_utf16le_to_utf8(u16name, 36, name, sizeof name);

            unsigned long long n_lba = (unsigned long long)(e->last_lba - e->first_lba + 1);
            double size_mb = (double)n_lba * 512.0 / (1024.0*1024.0);

            printf("  %3u  %12llu  %12llu  %8.1fMB  %-10s %s\n",
                   i+1,
                   (unsigned long long)e->first_lba,
                   (unsigned long long)e->last_lba,
                   size_mb,
                   alias ? alias : type_guid,
                   name[0] ? name : "");
        }
        free(ents);
    } else {
        printf("  scheme: (unknown / no GPT)\n");
    }
}

int cmd_mount(int argc, char **argv) {
    // mount                              -> list mapped devices (brief)
    // mount -v                           -> list mapped devices (verbose GPT)
    // mount /dev/a                       -> show one (verbose)
    // mount -i <image> /dev/a [--ro]     -> map image
    // mount -d /dev/a                    -> unmap
    bool verbose=false, do_map=false, do_unmap=false, ro=false;
    const char *img=NULL; char letter=0;

    for (int i=1;i<argc;i++){
        if (strcmp(argv[i], "-v")==0) { verbose=true; }
        else if (strcmp(argv[i], "-i")==0 && i+1<argc) { do_map=true; img=argv[++i]; }
        else if (strcmp(argv[i], "--ro")==0) { ro=true; }
        else if (strcmp(argv[i], "-d")==0) { do_unmap=true; }
        else if (!letter && parse_dev_letter(argv[i], &letter)) { /* ok */ }
        else if (!img && do_map) { img=argv[i]; }
        else { fprintf(stderr,"mount: unknown or misplaced arg: %s\n", argv[i]); return 2; }
    }

    if (do_map) {
        if (!img || !letter) { fprintf(stderr,"usage: mount -i <image> /dev/<a..z> [--ro]\n"); return 2; }
        if (!use_add(letter, img, ro)) { fprintf(stderr,"mount: failed to map /dev/%c\n", letter); return 1; }
        printf("Mapped /dev/%c -> %s%s\n", letter, img, ro ? " [ro]" : "");
        return 0;
    }

    if (do_unmap) {
        if (!letter) { fprintf(stderr,"usage: mount -d /dev/<a..z>\n"); return 2; }
        if (!use_rm(letter)) { fprintf(stderr,"mount: /dev/%c not mapped\n", letter); return 1; }
        printf("Unmapped /dev/%c\n", letter);
        return 0;
    }

    if (letter) {
        const char *path=NULL;
        if (!use_get(letter, &path)) { fprintf(stderr,"mount: /dev/%c not mapped\n", letter); return 1; }
        list_one(letter, path, true);
        return 0;
    }

    for (char c='a'; c<='z'; ++c) {
        const char *path=NULL;
        if (use_get(c, &path)) list_one(c, path, verbose);
    }
    return 0;
}
