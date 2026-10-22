/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "AdrenoZap.h"
/* mdt_loader.c: ELF32 little endian, 32 byte program headers, segment type and flags as qcom_mdt_read_metadata and
 * mdt_phdr_valid read them: QCOM_MDT_TYPE_MASK bits 24..26 (2 = the hash segment), QCOM_MDT_RELOCATABLE bit 27. */
enum {
    ZapHeaderBytes=52,ZapPhdrBytes=32,ZapPtLoad=1,ZapTypeShift=24,ZapTypeMask=7,ZapTypeHash=2,ZapRelocatable=1u<<27,
    ZapPage=0x1000,ZapMaxHeaders=16
};
static uint32_t le32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint32_t le16(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8;}
static int loadable(uint32_t type,uint32_t flags,uint32_t mem_bytes)
{return type==ZapPtLoad && ((flags>>ZapTypeShift)&ZapTypeMask)!=ZapTypeHash && mem_bytes!=0;}
AdrenoZapStatus AdrenoZapPlanImage(const uint8_t *image,size_t bytes,uint64_t region_bytes,AdrenoZapPlan *plan)
{
    if(!plan || !image || !region_bytes)return AdrenoZapInvalid;
    if(bytes<ZapHeaderBytes)return AdrenoZapTruncated;
    if(image[0]!=0x7f || image[1]!='E' || image[2]!='L' || image[3]!='F' || image[4]!=1 || image[5]!=1)return AdrenoZapInvalid;
    const uint32_t phoff=le32(image+28),phentsize=le16(image+42),phnum=le16(image+44);
    if(phentsize!=ZapPhdrBytes || phnum<2 || phnum>ZapMaxHeaders)return AdrenoZapInvalid;
    if(phoff>bytes || (uint64_t)phnum*ZapPhdrBytes>bytes-phoff)return AdrenoZapTruncated;
    AdrenoZapPlan p={0};
    const uint8_t *ph=image+phoff;
    /* qcom_mdt_read_metadata: header 0 is never a load segment, its p_filesz is the ELF header plus the program headers */
    if(le32(ph)==ZapPtLoad)return AdrenoZapInvalid;
    p.header_bytes=le32(ph+16);
    if(p.header_bytes<phoff+phnum*ZapPhdrBytes || p.header_bytes>bytes)return AdrenoZapInvalid;
    unsigned hash=0;
    for(unsigned i=1;i<phnum && !hash;++i) {
        const uint8_t *h=ph+i*ZapPhdrBytes;
        if(le32(h)!=ZapPtLoad && ((le32(h+24)>>ZapTypeShift)&ZapTypeMask)==ZapTypeHash)hash=i;
    }
    if(!hash)return AdrenoZapNoHash;
    p.hash_offset=le32(ph+hash*ZapPhdrBytes+4);p.hash_bytes=le32(ph+hash*ZapPhdrBytes+16);
    if(!p.hash_bytes || p.hash_offset>bytes || p.hash_bytes>bytes-p.hash_offset)return AdrenoZapTruncated;
    p.metadata_bytes=p.header_bytes+p.hash_bytes;
    uint64_t low=UINT64_MAX,high=0;
    for(unsigned i=0;i<phnum;++i) {
        const uint8_t *h=ph+i*ZapPhdrBytes;
        const uint32_t type=le32(h),offset=le32(h+4),paddr=le32(h+12),file_bytes=le32(h+16),mem_bytes=le32(h+20),flags=le32(h+24);
        if(!loadable(type,flags,mem_bytes))continue;
        if(p.segment_count==8)return AdrenoZapNoSpace;
        if(file_bytes>mem_bytes || offset>bytes || file_bytes>bytes-offset)return AdrenoZapTruncated;
        if(flags&ZapRelocatable)p.relocatable=1;
        const uint64_t end=((uint64_t)paddr+mem_bytes+ZapPage-1)&~(uint64_t)(ZapPage-1);
        if(paddr<low)low=paddr;
        if(end>high)high=end;
        p.segments[p.segment_count++]=(AdrenoZapSegment){offset,file_bytes,mem_bytes,paddr};
    }
    if(!p.segment_count)return AdrenoZapNoSegments;
    /* qcom_mdt_load: a relocatable image lands at the carveout base, a fixed one must already lie inside it */
    p.load_base=low;p.load_bytes=high-low;
    if(p.load_bytes>region_bytes)return AdrenoZapTooLarge;
    for(unsigned i=0;i<p.segment_count;++i)
        if(p.segments[i].paddr-low+p.segments[i].mem_bytes>region_bytes)return AdrenoZapTooLarge;
    *plan=p;
    return AdrenoZapSuccess;
}
