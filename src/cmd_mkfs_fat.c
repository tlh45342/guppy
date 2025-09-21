#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "fs_format.h"
#include "devmap.h"

// usage: mkfs.fat /dev/a [-o lba] [-S bps] [-c spc] [-F 12|16|32] [-L label] [-n oem] [-v]
int cmd_mkfs_fat(int argc, char **argv){
    if (argc < 2){
        printf("usage: mkfs.fat /dev/X [-o lba] [-S bps] [-c spc] [-F 12|16|32] [-L label] [-n oem] [-v]\n");
        return 2;
    }
    const char *dev = argv[1];

    const char *mapped = devmap_resolve(dev);
    if (!mapped){
        printf("mkfs.fat: device not mapped: %s\n", dev);
        return 1;
    }
    char path[512] = {0};
    strncpy(path, mapped, sizeof path - 1);

    mkfs_fat_opts_t opt = {
        .image_path    = path,
        .lba_offset    = 0,
        .bytes_per_sec = 512,
        .sec_per_clus  = 0,
        .fat_type      = -1,
        .label         = "NO NAME   ",
        .oem           = "MSWIN4.1",
        .verbose       = 0
    };

    for (int i=2;i<argc;i++){
        if (!strcmp(argv[i],"-o") && i+1<argc) opt.lba_offset=(uint32_t)strtoul(argv[++i],NULL,0);
        else if (!strcmp(argv[i],"-S") && i+1<argc) opt.bytes_per_sec=(uint16_t)strtoul(argv[++i],NULL,0);
        else if (!strcmp(argv[i],"-c") && i+1<argc) opt.sec_per_clus=(uint8_t)strtoul(argv[++i],NULL,0);
        else if (!strcmp(argv[i],"-F") && i+1<argc) opt.fat_type=(int)strtoul(argv[++i],NULL,0);
        else if (!strcmp(argv[i],"-L") && i+1<argc) opt.label=argv[++i];
        else if (!strcmp(argv[i],"-n") && i+1<argc) opt.oem=argv[++i];
        else if (!strcmp(argv[i],"-v")) opt.verbose=1;
        else { printf("mkfs.fat: bad flag '%s'\n", argv[i]); return 2; }
    }

    int rc = mkfs_fat_format(&opt);
    if (rc==0) printf("Formatted FAT%s on %s (%s)\n",
                      (opt.fat_type<0?"(auto)":(opt.fat_type==32?"32":(opt.fat_type==16?"16":"12"))),
                      dev, path);
    return rc;
}
