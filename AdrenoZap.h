/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ADRENO_ZAP_H
#define ADRENO_ZAP_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* The zap shader load plan of qcom_mdt_load (drivers/soc/qcom/mdt_loader.c) for one in-image ELF: the metadata
 * qcom_scm_pas_init_image takes (the ELF header with its program headers, then the hash segment), the carveout span
 * qcom_scm_pas_mem_setup takes when a segment is relocatable, and the loadable segments to copy into that carveout
 * before qcom_scm_pas_auth_and_reset. The image is the .mbn (a740) or the .mdt with its .bNN segments laid out at
 * their p_offset (a650, as AdrenoFirmwareEmbed.py assembles it). Only structure and ranges are checked here: the
 * TrustZone calls, the copy and the authenticity of the shader are the board's. */
typedef enum {
    AdrenoZapSuccess,AdrenoZapInvalid,AdrenoZapTruncated,AdrenoZapNoHash,AdrenoZapNoSegments,AdrenoZapTooLarge,
    AdrenoZapNoSpace
} AdrenoZapStatus;
typedef struct {
    uint32_t offset,file_bytes,mem_bytes;   /* file_bytes from the image at offset, zero filled to mem_bytes */
    uint64_t paddr;                         /* p_paddr; load at paddr - load_base within the carveout */
} AdrenoZapSegment;
typedef struct {
    uint32_t header_bytes;      /* program header 0's p_filesz: the ELF header and program headers */
    uint32_t hash_offset,hash_bytes;
    uint32_t metadata_bytes;    /* header_bytes + hash_bytes, what init_image reads */
    uint64_t load_base;         /* lowest loadable p_paddr */
    uint64_t load_bytes;        /* page rounded span of the loadable segments, what mem_setup covers */
    unsigned relocatable;       /* QCOM_MDT_RELOCATABLE on a loadable segment: mem_setup, then load at the carveout */
    unsigned segment_count;
    AdrenoZapSegment segments[8];
} AdrenoZapPlan;
/* region_bytes is the carveout's size; a plan whose segments do not fit it is refused. No output on failure. */
AdrenoZapStatus AdrenoZapPlanImage(const uint8_t *image,size_t bytes,uint64_t region_bytes,AdrenoZapPlan *plan);
#ifdef __cplusplus
}
#endif
#endif
