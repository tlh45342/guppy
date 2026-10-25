/* Read-only ext2 phase-1 geometry and allocation checker. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include "vblk.h"
#include "ext2.h"
#include "fsck_ext2_inode.h"

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (uint16_t)p[1]<<8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)rd16(p) | (uint32_t)rd16(p+2)<<16; }
static int bit(const uint8_t *p,uint32_t i) { return (p[i>>3]>>(i&7))&1; }
static int powerof(uint32_t n,uint32_t base){if(!n)return 0;while(n%base==0)n/=base;return n==1;}
static int has_backup(uint32_t g,uint32_t rocompat){return !(rocompat&1u)||g==0||g==1||powerof(g,3)||powerof(g,5)||powerof(g,7);}
static void mark_meta(fsck_block_context *r,uint64_t block){if(block<r->count)r->owners[block]=UINT32_MAX;}
static int readat(vblk_t *d,uint64_t o,uint32_t n,void *p) { return vblk_read_bytes(d,o,n,p)?0:-1; }

int ext2_fsck_phase1(vblk_t *dev) {
    uint8_t sb[1024];
    if(readat(dev,1024,1024,sb)) { fprintf(stderr,"fsck.ext2: cannot read superblock\n"); return 2; }
    if(rd16(sb+56)!=0xEF53) { fprintf(stderr,"fsck.ext2: bad magic\n"); return 2; }
    uint32_t blocks=rd32(sb+4), inodes=rd32(sb), first=rd32(sb+20);
    uint32_t logsz=rd32(sb+24), bpg=rd32(sb+32), ipg=rd32(sb+40);
    uint32_t freeb=rd32(sb+12), freei=rd32(sb+16);
    uint32_t rev=rd32(sb+76);
    uint32_t isz=rev?rd16(sb+88):128;
    if(logsz>2 || !bpg || !ipg || !blocks || !inodes || isz<128 || isz>4096 || (isz&(isz-1))) {
        fprintf(stderr,"fsck.ext2: invalid filesystem geometry\n"); return 2;
    }
    uint32_t bs=1024u<<logsz;
    uint64_t bytes=vblk_size_bytes(dev);
    if((uint64_t)blocks*bs>bytes || first>=blocks || bpg>bs*8u || ipg>bs*8u) {
        fprintf(stderr,"fsck.ext2: filesystem exceeds device or bitmap capacity\n"); return 2;
    }
    uint32_t groups=(uint32_t)(((uint64_t)blocks-first+bpg-1)/bpg);
    uint32_t igroups=(inodes+ipg-1)/ipg;
    if(!groups || groups>65535 || igroups>groups) {
        fprintf(stderr,"fsck.ext2: inconsistent group counts\n"); return 2;
    }
    uint32_t gdblocks=(uint32_t)(((uint64_t)groups*32+bs-1)/bs);
    uint32_t itblocks=(uint32_t)(((uint64_t)ipg*isz+bs-1)/bs);
    uint8_t *bb=malloc(bs),*ib=malloc(bs),*gd=malloc((size_t)groups*32);
    if(!bb||!ib||!gd){ free(bb);free(ib);free(gd);return 2; }
    uint64_t gdoff=(uint64_t)(first+1)*bs;
    if(readat(dev,gdoff,groups*32,gd)) {
        fprintf(stderr,"fsck.ext2: cannot read group descriptors\n");
        free(bb);free(ib);free(gd);return 2;
    }
    fsck_block_context refs={0};
    refs.count=blocks;
    refs.inode_count=inodes;
    refs.inode_allocated=calloc(((size_t)inodes+7)/8,1);
    refs.directory_refs=calloc((size_t)inodes+1,sizeof(uint32_t));
    refs.inode_links=calloc((size_t)inodes+1,sizeof(uint16_t));
    refs.inode_checked=calloc((size_t)inodes+1,1);
    refs.block_size=bs;
    refs.dev=dev;
    refs.allocated=calloc(((size_t)blocks+7)/8,1);
    refs.owners=calloc((size_t)blocks,sizeof(*refs.owners));
    if(!refs.allocated||!refs.owners||!refs.inode_allocated||!refs.directory_refs||!refs.inode_links||!refs.inode_checked){
        free(refs.allocated);free(refs.owners);free(refs.inode_allocated);free(refs.directory_refs);free(refs.inode_links);free(refs.inode_checked);
        free(bb);free(ib);free(gd);return 2;
    }
    for(uint32_t g=0;g<groups;g++){
        uint32_t imap=rd32(gd+(size_t)g*32+4);
        uint64_t base=(uint64_t)g*ipg;
        uint32_t n=base>=inodes?0:(uint32_t)((inodes-base)>ipg?ipg:(inodes-base));
        if(!n)continue;
        if(readat(dev,(uint64_t)imap*bs,bs,ib)){
            fprintf(stderr,"fsck.ext2: cannot load inode bitmap for group %" PRIu32 "\n",g);
            free(refs.allocated);free(refs.owners);free(refs.inode_allocated);free(refs.directory_refs);free(refs.inode_links);free(refs.inode_checked);
            free(bb);free(ib);free(gd);return 2;
        }
        for(uint32_t i=0;i<n;i++)if(bit(ib,i)){
            uint32_t index=(uint32_t)base+i;
            refs.inode_allocated[index>>3]|=(uint8_t)(1u<<(index&7));
        }
    }
    /* Load all group block bitmaps before checking cross-group references. */
    for(uint32_t g=0;g<groups;g++){
        uint64_t start=(uint64_t)first+(uint64_t)g*bpg;
        uint32_t nblk=(uint32_t)(((uint64_t)blocks-start)>bpg?bpg:((uint64_t)blocks-start));
        uint32_t bmap=rd32(gd+(size_t)g*32);
        if(bmap<start||bmap>=start+nblk||readat(dev,(uint64_t)bmap*bs,bs,bb)){
            fprintf(stderr,"fsck.ext2: cannot load group %" PRIu32 " block bitmap\n",g);
            free(refs.allocated);free(refs.owners);free(bb);free(ib);free(gd);return 2;
        }
        for(uint32_t i=0;i<nblk;i++)if(bit(bb,i)){
            uint32_t b=(uint32_t)(start+i);
            refs.allocated[b>>3]|=(uint8_t)(1u<<(b&7));
        }
    }
    /* Reserve ext2 metadata before inode traversal (including cross-group pointers). */
    uint32_t rocompat=rd32(sb+100);
    uint32_t reserved_gdt=rev?rd16(sb+206):0;
    for(uint32_t g=0;g<groups;g++){
        uint64_t start=(uint64_t)first+(uint64_t)g*bpg;
        uint64_t end=start+bpg<blocks?start+bpg:blocks;
        const uint8_t *p=gd+(size_t)g*32;
        mark_meta(&refs,rd32(p));mark_meta(&refs,rd32(p+4));
        for(uint32_t j=0;j<itblocks;j++)mark_meta(&refs,(uint64_t)rd32(p+8)+j);
        if(has_backup(g,rocompat)){
            uint64_t span=1+(uint64_t)gdblocks+reserved_gdt;
            for(uint64_t j=0;j<span && start+j<end;j++)mark_meta(&refs,start+j);
        }
    }
    unsigned errors=0;
    uint64_t totalb=0,totali=0;
    printf("fsck.ext2: blocks=%" PRIu32 " inodes=%" PRIu32 " blocksize=%" PRIu32 " groups=%" PRIu32 "\n",blocks,inodes,bs,groups);
    for(uint32_t g=0;g<groups;g++){
        uint64_t start=(uint64_t)first+(uint64_t)g*bpg;
        uint32_t nblk=(uint32_t)(((uint64_t)blocks-start)>bpg?bpg:((uint64_t)blocks-start));
        uint64_t remainino=(uint64_t)g*ipg>=inodes?0:(uint64_t)inodes-(uint64_t)g*ipg;
        uint32_t nino=(uint32_t)(remainino>ipg?ipg:remainino);
        const uint8_t *p=gd+(size_t)g*32;
        uint32_t bmap=rd32(p),imap=rd32(p+4),itable=rd32(p+8);
        if(bmap<start||bmap>=start+nblk||imap<start||imap>=start+nblk||
           itable<start||((uint64_t)itable+itblocks)>start+nblk) {
            fprintf(stderr,"fsck.ext2: group %" PRIu32 ": metadata block out of group bounds\n",g);
            errors++;continue;
        }
        if(readat(dev,(uint64_t)bmap*bs,bs,bb)||readat(dev,(uint64_t)imap*bs,bs,ib)){
            fprintf(stderr,"fsck.ext2: group %" PRIu32 ": bitmap read failed\n",g);errors++;continue;
        }
        uint32_t first_nonreserved=rev?rd32(sb+84):11;
        int inode_errors=fsck_ext2_check_inodes(dev,g,ipg,nino,isz,bs,itable,ib,first_nonreserved,&refs);
        if(inode_errors<0) { errors++; } else { errors+=(unsigned)inode_errors; }
        uint32_t fb=0,fi=0;
        for(uint32_t i=0;i<nblk;i++)fb+=!bit(bb,i);
        for(uint32_t i=0;i<nino;i++)fi+=!bit(ib,i);
        totalb+=fb;totali+=fi;
        if(fb!=rd16(p+12)) {fprintf(stderr,"fsck.ext2: group %" PRIu32 ": free blocks bitmap=%" PRIu32 " descriptor=%u\n",g,fb,(unsigned)rd16(p+12));errors++;}
        if(fi!=rd16(p+14)) {fprintf(stderr,"fsck.ext2: group %" PRIu32 ": free inodes bitmap=%" PRIu32 " descriptor=%u\n",g,fi,(unsigned)rd16(p+14));errors++;}
        uint64_t meta[]={bmap,imap,itable};
        for(unsigned k=0;k<3;k++){
            uint32_t span=k==2?itblocks:1;
            for(uint32_t j=0;j<span;j++){
                uint64_t blk=meta[k]+j;
                if(blk>=start && blk<start+nblk && !bit(bb,(uint32_t)(blk-start))){
                    fprintf(stderr,"fsck.ext2: group %" PRIu32 ": metadata block %" PRIu64 " marked free\n",g,blk);errors++;
                }
            }
        }
        /* Full-superblock groups are mandatory when sparse_super is disabled. */
        uint32_t rocompat=rd32(sb+100);
        if(!(rocompat&1u)){
            for(uint32_t j=0;j<1+gdblocks && j<nblk;j++){
                if(!bit(bb,j)){fprintf(stderr,"fsck.ext2: group %" PRIu32 ": superblock/GDT block %" PRIu64 " marked free\n",g,start+j);errors++;}
            }
        }
        if(g==0){
            uint32_t reserved=rev?rd32(sb+84):11;
            if(reserved>nino+1)reserved=nino+1;
            for(uint32_t i=0;i+1<reserved;i++)if(!bit(ib,i)){
                fprintf(stderr,"fsck.ext2: reserved inode %" PRIu32 " marked free\n",i+1);errors++;
            }
        }
    }
    if(totalb!=freeb){fprintf(stderr,"fsck.ext2: superblock free blocks=%" PRIu32 " bitmap=%" PRIu64 "\n",freeb,totalb);errors++;}
    if(totali!=freei){fprintf(stderr,"fsck.ext2: superblock free inodes=%" PRIu32 " bitmap=%" PRIu64 "\n",freei,totali);errors++;}
    uint32_t checked_links=0;
    for(uint32_t ino=1;ino<=inodes;ino++)if(refs.inode_checked[ino]){
        checked_links++;
        if(refs.directory_refs[ino]!=refs.inode_links[ino]){
            fprintf(stderr,"fsck.ext2: inode %" PRIu32 ": link count=%u directory references=%" PRIu32 "\n",
                    ino,(unsigned)refs.inode_links[ino],refs.directory_refs[ino]);
            errors++;
        }
    }
    printf("fsck.ext2: checked directory references for %" PRIu32 " inodes\n",checked_links);
    uint64_t unowned=0,owned=0,metadata=0;
    for(uint32_t b=first;b<blocks;b++)if(bit(refs.allocated,b)){
        if(refs.owners[b]==UINT32_MAX)metadata++;
        else if(refs.owners[b])owned++;
        else {
            if(unowned<16)fprintf(stderr,"fsck.ext2: allocated block %" PRIu32 " has no identified owner\n",b);
            unowned++;
        }
    }
    if(unowned){fprintf(stderr,"fsck.ext2: %" PRIu64 " allocated blocks unaccounted for\n",unowned);errors++;}
    printf("fsck.ext2: block ownership: data=%" PRIu64 " metadata=%" PRIu64 " unaccounted=%" PRIu64 "\n",owned,metadata,unowned);
    printf("fsck.ext2: free blocks=%" PRIu64 " free inodes=%" PRIu64 " errors=%u (read-only)\n",totalb,totali,errors);
    free(refs.allocated);free(refs.owners);
    free(refs.inode_allocated);free(refs.directory_refs);free(refs.inode_links);free(refs.inode_checked);
    free(bb);free(ib);free(gd);
    return errors?1:0;
}

int cmd_fsck_ext2(int argc,char **argv){
    if(argc!=2){fprintf(stderr,"usage: fsck.ext2 <device>\n");return 2;}
    vblk_t *dev=vblk_open(argv[1]);
    if(!dev){fprintf(stderr,"fsck.ext2: unknown device %s\n",argv[1]);return 2;}
    return ext2_fsck_phase1(dev);
}
