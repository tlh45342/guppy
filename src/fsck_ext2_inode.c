/* Phase 2: read-only allocated inode sanity checks. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include "fsck_ext2_inode.h"
static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1]<<8)); }
static uint32_t u32(const uint8_t *p) { return (uint32_t)u16(p) | ((uint32_t)u16(p+2)<<16); }
int fsck_ext2_check_inodes(vblk_t *dev, uint32_t group, uint32_t ipg,
                           uint32_t count, uint32_t isz, uint32_t bs,
                           uint32_t table, const uint8_t *bitmap, uint32_t first_nonreserved, fsck_block_context *blocks) {
    uint8_t *inode=malloc(isz);
    if(!inode) return -1;
    int errors=0;
    for(uint32_t i=0;i<count;i++) {
        if(!(bitmap[i>>3] & (1u<<(i&7)))) continue;
        uint64_t ino=(uint64_t)group*ipg+i+1;
        uint64_t off=(uint64_t)table*bs+(uint64_t)i*isz;
        if(!vblk_read_bytes(dev,off,isz,inode)) {
            fprintf(stderr,"fsck.ext2: inode %" PRIu64 ": read failed\n",ino);
            free(inode); return -1;
        }
        uint16_t mode=u16(inode), links=u16(inode+26);
        uint32_t deletion=u32(inode+20);
        uint16_t type=mode & 0170000;
        /* Reserved ext2 inodes can legitimately have no file type. */
        if(ino < first_nonreserved && ino != 2) continue;
        if(type!=0100000 && type!=0040000 && type!=0120000 &&
           type!=0020000 && type!=0060000 && type!=0010000 && type!=0140000) {
            fprintf(stderr,"fsck.ext2: inode %" PRIu64 ": invalid file type (mode=%06o)\n",ino,mode);
            errors++;
        }
        if(deletion) {
            fprintf(stderr,"fsck.ext2: inode %" PRIu64 ": allocated but deletion time is set\n",ino);
            errors++;
        }
        if(!links) {
            fprintf(stderr,"fsck.ext2: inode %" PRIu64 ": allocated but link count is zero\n",ino);
            errors++;
        }
        blocks->inode_links[ino]=links;
        blocks->inode_checked[ino]=1;
        errors+=fsck_check_direct(blocks,(uint32_t)ino,inode,type);
        if(type==0040000)errors+=fsck_check_directory(blocks,(uint32_t)ino,inode);
    }
    free(inode);
    return errors;
}
