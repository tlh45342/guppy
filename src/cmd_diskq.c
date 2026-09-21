#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_printf.h"
#include "cmds.h"
#include "vdisk.h"

static uint32_t le32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
           ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static uint64_t le64(const unsigned char *p) {
    return (uint64_t)le32(p) | ((uint64_t)le32(p+4)<<32);
}
static void human(uint64_t n,char out[32]) {
    static const char *u[]={"B","KiB","MiB","GiB","TiB"};
    double v=(double)n; int i=0;
    while(v>=1024.0 && i<4){v/=1024.0;i++;}
    if(i==0) snprintf(out,32,"%" PRIu64 " B",n);
    else snprintf(out,32,"%.2f %s",v,u[i]);
}
static const char *fmt_upper(vdisk_format_t f) {
    switch(f) {
        case VDISK_FMT_RAW:return "RAW candidate";
        case VDISK_FMT_VDI:return "VDI";
        case VDISK_FMT_VMDK:return "VMDK";
        case VDISK_FMT_VHD:return "VHD";
        case VDISK_FMT_QCOW2:return "QCOW2";
        default:return "unknown";
    }
}
static void partition_layer(const char *path, vdisk_format_t fmt) {
    vdisk_t *d=NULL;
    unsigned char s0[512],s1[512];
    int mbr=0,gpt=0,protective=0,i;

    if(fmt!=VDISK_FMT_RAW && fmt!=VDISK_FMT_VDI && fmt!=VDISK_FMT_VHD && fmt!=VDISK_FMT_VMDK) {
        app_printf("\nPartition layer:\n");
        app_printf("  SKIP  container recognized but logical block access is not implemented\n");
        return;
    }
    if(vdisk_open(path,fmt,&d)!=0) {
        app_printf("\nPartition layer:\n  FAIL  could not open logical disk read-only\n");
        return;
    }
    if(vdisk_get_size(d)<512 || vdisk_read(d,0,s0,sizeof s0)!=0) {
        app_printf("\nPartition layer:\n  NONE  disk is too small or unreadable\n");
        vdisk_close(d); return;
    }
    mbr=(s0[510]==0x55 && s0[511]==0xaa);
    if(mbr) {
        for(i=0;i<4;i++) if(s0[446+i*16+4]==0xee) protective=1;
    }
    if(vdisk_get_size(d)>=1024 && vdisk_read(d,512,s1,sizeof s1)==0 &&
       memcmp(s1,"EFI PART",8)==0) gpt=1;

    app_printf("\nPartition layer:\n");
    if(gpt) {
        uint32_t hsz=le32(s1+12);
        uint64_t cur=le64(s1+24), backup=le64(s1+32);
        uint32_t count=le32(s1+80), esz=le32(s1+84);
        app_printf("  Type:              GPT%s\n",protective?" (protective MBR present)":"");
        app_printf("  Header size:       %" PRIu32 "\n",hsz);
        app_printf("  Current LBA:       %" PRIu64 "\n",cur);
        app_printf("  Backup LBA:        %" PRIu64 "\n",backup);
        app_printf("  Entry count:       %" PRIu32 "\n",count);
        app_printf("  Entry size:        %" PRIu32 "\n",esz);
        app_printf("  Sanity:            %s\n",
                   (hsz>=92 && hsz<=512 && cur==1 && esz>=128)?"PASS":"CHECK");
    } else if(mbr) {
        app_printf("  Type:              MBR\n");
        app_printf("  Signature:         PASS (55 AA)\n");
    } else {
        app_printf("  Type:              none detected\n");
    }
    vdisk_close(d);
}

int cmd_diskq(int argc,char **argv) {
    vdisk_info_t q;
    char host[32],logical[32];

    if(argc!=2 || !strcmp(argv[1],"-h") || !strcmp(argv[1],"--help")) {
        app_printf("Usage: diskq <host-disk-image>\n");
        app_printf("Read-only container and partition sanity report.\n");
        return argc==2?0:2;
    }
    if(vdisk_inspect(argv[1],&q)!=0) {
        fprintf(stderr,"diskq: cannot inspect '%s'\n",argv[1]);
        return 2;
    }
    human(q.host_size_bytes,host);
    human(q.logical_size_bytes,logical);

    app_printf("diskq: %s\n",argv[1]);
    app_printf("\nContainer layer:\n");
    app_printf("  Detected format:   %s\n",fmt_upper(q.format));
    app_printf("  Variant:           %s\n",q.variant[0]?q.variant:"unknown");
    app_printf("  Host file size:    %s (%" PRIu64 " bytes)\n",host,q.host_size_bytes);

    if(q.format==VDISK_FMT_RAW) {
        app_printf("  Note:              no recognized container signature; RAW is a candidate\n");
        app_printf("  Container sanity:  N/A (RAW has no container metadata)\n");
    } else if(!q.supported) {
        app_printf("  Support:           recognized, detailed validation not implemented\n");
        app_printf("  Container sanity:  RECOGNIZED ONLY\n");
    } else if(q.format==VDISK_FMT_VDI) {
        app_printf("  Logical size:      %s (%" PRIu64 " bytes)\n",logical,q.logical_size_bytes);
        app_printf("  Sector size:       %" PRIu32 "\n",q.sector_size);
        app_printf("  Block size:        %" PRIu32 "\n",q.block_size);
        app_printf("  Blocks:            %" PRIu32 "\n",q.blocks_count);
        app_printf("  Blocks allocated:  %" PRIu32 "\n",q.blocks_allocated);
        app_printf("  Block map offset:  0x%08" PRIx64 "\n",q.blocks_offset);
        app_printf("  Data offset:       0x%08" PRIx64 "\n",q.data_offset);
        app_printf("  Map allocated:     %" PRIu32 "\n",q.map_allocated_entries);
        app_printf("  Map unallocated:   %" PRIu32 "\n",q.map_unallocated_entries);
        app_printf("  Map invalid:       %" PRIu32 "\n",q.map_invalid_entries);
        app_printf("  Map duplicates:    %" PRIu32 "\n",q.map_duplicate_entries);
        app_printf("  Checks:            %u PASS / %u FAIL\n",q.checks_passed,q.checks_failed);
        app_printf("  Container sanity:  %s\n",q.structurally_valid?"PASS":"FAIL");
    } else if(q.format==VDISK_FMT_VHD) {
        app_printf("  Logical size:      %s (%" PRIu64 " bytes)\n",logical,q.logical_size_bytes);
        app_printf("  Sector size:       %" PRIu32 "\n",q.sector_size);
        app_printf("  Disk type:         %" PRIu32 " (fixed)\n",q.image_type);
        app_printf("  Checks:            %u PASS / %u FAIL\n",q.checks_passed,q.checks_failed);
        app_printf("  Container sanity:  %s\n",q.structurally_valid?"PASS":"FAIL");
    } else if(q.format==VDISK_FMT_VMDK) {
        app_printf("  Logical size:      %s (%" PRIu64 " bytes)\n",logical,q.logical_size_bytes);
        app_printf("  Sector size:       %" PRIu32 "\n",q.sector_size);
        app_printf("  Layout:            descriptor + flat extent\n");
        app_printf("  Checks:            %u PASS / %u FAIL\n",q.checks_passed,q.checks_failed);
        app_printf("  Container sanity:  %s\n",q.structurally_valid?"PASS":"FAIL");
    }

    partition_layer(argv[1],q.format);
    app_printf("\nResult: %s\n",
               (q.format==VDISK_FMT_RAW)?"RAW/PLAIN DISK CANDIDATE":
               (!q.supported)?"FORMAT RECOGNIZED; VALIDATION PENDING":
               q.structurally_valid?"CONTAINER VALID":"CONTAINER INVALID");
    return (q.supported && q.format!=VDISK_FMT_RAW && !q.structurally_valid)?2:0;
}
