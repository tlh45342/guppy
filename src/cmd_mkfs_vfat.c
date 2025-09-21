#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "fs_format.h"
#include "devmap.h"

// usage: mkfs_vfat /dev/a [-o lba] [-S bps] [-c spc] [-F 12|16|32] [-L label] [-n oem] [-v]
int cmd_mkfs_vfat(int argc, char **argv){
    if (argc < 2){
        printf("usage: mkfs_vfat /dev/X [-o lba] [-S bps] [-c spc] [-F 12|16|32] [-L label] [-n oem] [-v]\n");
        return 2;
    }
    const char *dev = argv[1];

    const char *mapped = devmap_resolve(dev);
    if (!mapped){
        printf("mkfs_vfat: device not mapped: %s\n", dev);
        return 1;
    }
    char path[512] = {0};
    strncpy(path, mapped, sizeof path - 1);

    mkfs_fat_opts_t opt = {
        .image_path    = path,
        .lba_offset    = 0,
        .bytes_per_sec = 512,
        .sec_per_clus  = 0,
        .fat_type      = -1,         // auto
        .label         = "NO NAME   ",
        .oem           = "MSWIN4.1", // VFAT-y default OEM
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
        else { printf("mkfs_vfat: bad flag '%s'\n", argv[i]); return 2; }
    }

    int rc = mkfs_fat_format(&opt);
    if (rc==0) printf("Formatted VFAT on %s (%s)\n", dev, path);
    return rc;
}
