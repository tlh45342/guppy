## README.md

## WARNING

This is SO alpha.  This is very immature.

## INSTALLATION

```bash
cd /opt
git clone https://github.com/tlh45342/guppy.git
cd guppy
make ; make install
```

## WARNING

This has NOT been tested hardly at all.  Use at your own risk.  Do NOT use this on a production system.



guppy create image.img --size 20MiB

guppy gpt init disk.img --entries 128 --sector 512

# 512 MiB EFI System Partition (FAT32)
guppy gpt add disk.img --type esp --name "EFI System" --start 1MiB --size 512MiB --align 1MiB

# Rest of disk as Linux filesystem
guppy gpt add disk.img --type linuxfs --name "rootfs" --start 513MiB --size 100%