/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* The zap load plan over a synthetic ELF shaped like a650_zap.mdt plus its .b02, and over the real image when one is
 * given on the command line (the firmware is not in Git). No TrustZone call, no copy. */
#include "AdrenoZap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void put32(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static void put16(uint8_t *p,unsigned v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void phdr(uint8_t *image,unsigned i,uint32_t type,uint32_t offset,uint32_t paddr,uint32_t filesz,uint32_t memsz,uint32_t flags)
{
    uint8_t *h=image+52+32*i;
    put32(h,type);put32(h+4,offset);put32(h+8,paddr);put32(h+12,paddr);put32(h+16,filesz);put32(h+20,memsz);put32(h+24,flags);put32(h+28,0x1000);
}
/* the a650 shape: header 0x94 bytes, the hash at 0x1000, one relocatable load segment at 0x3000 for paddr 0x5000 */
static size_t synthetic(uint8_t *image)
{
    memset(image,0,0x4000);
    memcpy(image,"\x7f" "ELF\x01\x01\x01",7);
    put32(image+28,52);put16(image+42,32);put16(image+44,3);
    phdr(image,0,0,0,0,0x94,0,0x07000000);
    phdr(image,1,0,0x1000,0x6000,0x1b38,0x2000,0x02200000);
    phdr(image,2,1,0x3000,0x5000,0x68c,0x68c,0x08000007);
    for(unsigned i=0;i<0x68c;++i)image[0x3000+i]=(uint8_t)i;
    return 0x368c;
}
static void check_plan(const AdrenoZapPlan *p)
{
    CHECK(p->header_bytes==0x94 && p->hash_offset==0x1000 && p->hash_bytes==0x1b38 && p->metadata_bytes==0x94+0x1b38);
    CHECK(p->segment_count==1 && p->relocatable && p->load_base==0x5000 && p->load_bytes==0x1000);
    CHECK(p->segments[0].offset==0x3000 && p->segments[0].file_bytes==0x68c && p->segments[0].mem_bytes==0x68c && p->segments[0].paddr==0x5000);
}
int main(int argc,char **argv)
{
    uint8_t *image=malloc(0x4000);CHECK(image);
    const size_t bytes=synthetic(image);
    AdrenoZapPlan plan;memset(&plan,0xa5,sizeof plan);
    CHECK(AdrenoZapPlanImage(image,bytes,0x2000,&plan)==AdrenoZapSuccess);
    check_plan(&plan);
    /* refusals leave the plan alone */
    AdrenoZapPlan before;memset(&before,0x5a,sizeof before);AdrenoZapPlan after=before;unsigned refusals=0;
#define REFUSE(status,expr) do {memcpy(&after,&before,sizeof after);CHECK((expr)==(status) && !memcmp(&before,&after,sizeof before));++refusals;}while(0)
    REFUSE(AdrenoZapInvalid,AdrenoZapPlanImage(NULL,bytes,0x2000,&after));
    REFUSE(AdrenoZapInvalid,AdrenoZapPlanImage(image,bytes,0,&after));
    CHECK(AdrenoZapPlanImage(image,bytes,0x2000,NULL)==AdrenoZapInvalid);++refusals;
    REFUSE(AdrenoZapTruncated,AdrenoZapPlanImage(image,51,0x2000,&after));
    REFUSE(AdrenoZapTruncated,AdrenoZapPlanImage(image,0x1000,0x2000,&after));           /* hash segment outside */
    REFUSE(AdrenoZapTruncated,AdrenoZapPlanImage(image,0x3000+0x100,0x2000,&after));     /* load segment outside */
    REFUSE(AdrenoZapTooLarge,AdrenoZapPlanImage(image,bytes,0x800,&after));
    image[0]=0x7e;REFUSE(AdrenoZapInvalid,AdrenoZapPlanImage(image,bytes,0x2000,&after));synthetic(image);
    put16(image+42,56);REFUSE(AdrenoZapInvalid,AdrenoZapPlanImage(image,bytes,0x2000,&after));synthetic(image);
    put32(image+52,1);REFUSE(AdrenoZapInvalid,AdrenoZapPlanImage(image,bytes,0x2000,&after));synthetic(image);   /* header 0 loadable */
    put32(image+52+32+24,0x01000000);REFUSE(AdrenoZapNoHash,AdrenoZapPlanImage(image,bytes,0x2000,&after));synthetic(image);
    put32(image+52+64+20,0);REFUSE(AdrenoZapNoSegments,AdrenoZapPlanImage(image,bytes,0x2000,&after));synthetic(image);
    put32(image+52+64+16,0x700);REFUSE(AdrenoZapTruncated,AdrenoZapPlanImage(image,bytes,0x2000,&after));synthetic(image);  /* filesz > memsz */
    /* a fixed (non relocatable) image keeps its addresses; one past the carveout is refused */
    put32(image+52+64+24,0x00000007);
    CHECK(AdrenoZapPlanImage(image,bytes,0x8000,&plan)==AdrenoZapSuccess && !plan.relocatable && plan.load_base==0x5000);
    put32(image+52+64+8,0x1000);put32(image+52+64+12,0x1000);put32(image+52+64+20,0x3000);put32(image+52+64+16,0x68c);
    phdr(image,1,0,0x1000,0x6000,0x1b38,0x2000,0x02200000);
    CHECK(AdrenoZapPlanImage(image,bytes,0x2000,&plan)==AdrenoZapTooLarge);++refusals;
    synthetic(image);
    unsigned real=0;
    for(int i=1;i<argc;++i) {
        FILE *f=fopen(argv[i],"rb");CHECK(f && !fseek(f,0,SEEK_END));
        long size=ftell(f);CHECK(size>0 && !fseek(f,0,SEEK_SET));
        uint8_t *data=malloc((size_t)size);CHECK(data && fread(data,1,(size_t)size,f)==(size_t)size);fclose(f);
        CHECK(AdrenoZapPlanImage(data,(size_t)size,0x2000,&plan)==AdrenoZapSuccess);
        check_plan(&plan);
        /* the segment bytes are the ones AdrenoFirmwareEmbed.py put at their p_offset */
        CHECK(plan.segments[0].offset+plan.segments[0].file_bytes<=(size_t)size);
        free(data);++real;
    }
    free(image);
    printf("PASS: 1 a650-shaped zap plan, 1 fixed-address plan, %u refusals, %u real images\n",refusals,real);
    return 0;
}
