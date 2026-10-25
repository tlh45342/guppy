/* Read-only ext2 directory-entry validation for direct directory blocks. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include "fsck_ext2_blocks.h"
static uint16_t r16(const uint8_t *p){return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static uint32_t r32(const uint8_t *p){return (uint32_t)r16(p)|((uint32_t)r16(p+2)<<16);}
int fsck_check_directory(fsck_block_context *c,uint32_t ino,const uint8_t *inode){
 int errors=0;
 uint8_t *buf=malloc(c->block_size);
 if(!buf)return 1;
 uint32_t size=r32(inode+4);
 for(unsigned j=0;j<12 && (uint64_t)j*c->block_size<size;j++){
  uint32_t b=r32(inode+40+4*j);
  if(!b||b>=c->count||!(c->allocated[b>>3]&(1u<<(b&7))))continue;
  if(!vblk_read_bytes(c->dev,(uint64_t)b*c->block_size,c->block_size,buf)){
   fprintf(stderr,"fsck.ext2: directory inode %" PRIu32 ": block read failed\n",ino);errors++;continue;
  }
  uint32_t pos=0;
  while(pos<c->block_size){
   if(pos+8>c->block_size){errors++;break;}
   uint32_t target=r32(buf+pos);
   uint16_t rec=r16(buf+pos+4);
   uint8_t len=buf[pos+6];
   if(rec<8||(rec&3)||rec>c->block_size-pos||len>rec-8){
    fprintf(stderr,"fsck.ext2: directory inode %" PRIu32 ": invalid entry at block %" PRIu32 " offset %" PRIu32 "\n",ino,b,pos);
    errors++;break;
   }
   if(target){
    if(target>c->inode_count||!(c->inode_allocated[(target-1)>>3]&(1u<<((target-1)&7)))){
     fprintf(stderr,"fsck.ext2: directory inode %" PRIu32 ": entry references unallocated inode %" PRIu32 "\n",ino,target);
     errors++;
    }else c->directory_refs[target]++;
   }
   pos+=rec;
  }
 }
 free(buf);
 return errors;
}
