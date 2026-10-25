/* Read-only direct and indirect block reference validation. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include "fsck_ext2_blocks.h"
static uint32_t get32(const uint8_t *p) {
 return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
/* Return 1 for a new valid claim, 0 for invalid or already claimed. */
static int claim(fsck_block_context *ctx, uint32_t ino, uint32_t block, int *errors) {
 if(block>=ctx->count) {
  fprintf(stderr,"fsck.ext2: inode %" PRIu32 ": block %" PRIu32 " out of bounds\n",ino,block);
  ++*errors; return 0;
 }
 if(!(ctx->allocated[block>>3] & (1u<<(block&7)))) {
  fprintf(stderr,"fsck.ext2: inode %" PRIu32 ": block %" PRIu32 " marked free\n",ino,block);
  ++*errors;
 }
 if(ctx->owners[block]) {
  fprintf(stderr,"fsck.ext2: inode %" PRIu32 ": block %" PRIu32 " already owned by inode %" PRIu32 "\n",ino,block,ctx->owners[block]);
  ++*errors; return 0;
 }
 ctx->owners[block]=ino;
 return 1;
}
/* depth=0: data block; depth=1..3: pointer blocks. */
static void walk(fsck_block_context *ctx, uint32_t ino, uint32_t block, unsigned depth, int *errors) {
 if(!block || !claim(ctx,ino,block,errors)) return;
 if(!depth) return;
 uint8_t *buf=malloc(ctx->block_size);
 if(!buf) { fprintf(stderr,"fsck.ext2: indirect block allocation failed\n"); ++*errors; return; }
 if(!vblk_read_bytes(ctx->dev,(uint64_t)block*ctx->block_size,ctx->block_size,buf)) {
  fprintf(stderr,"fsck.ext2: inode %" PRIu32 ": indirect block %" PRIu32 " read failed\n",ino,block);
  ++*errors; free(buf); return;
 }
 for(uint32_t j=0;j<ctx->block_size/4;j++) {
  uint32_t child=get32(buf+4*j);
  if(child) walk(ctx,ino,child,depth-1,errors);
 }
 free(buf);
}
int fsck_check_direct(fsck_block_context *ctx, uint32_t ino, const uint8_t *inode, uint16_t type) {
 int errors=0;
 uint32_t size=get32(inode+4);
 if(type==0120000 && size<=60 && get32(inode+28)==0) return 0;
 if(type==0020000 || type==0060000 || type==0010000 || type==0140000) return 0;
 for(unsigned j=0;j<15;j++) {
  uint32_t block=get32(inode+40+4*j);
  if(block) walk(ctx,ino,block,j<12?0:j-11,&errors);
 }
 return errors;
}
