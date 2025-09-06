; build a demo image
create demo.img --size 256MiB --mbr
# add a FAT32-LBA partition
part add demo.img --index 1 --type 0x0C --start 1MiB --size 32MiB
mbr print demo.img