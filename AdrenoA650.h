/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ADRENO_A650_H
#define ADRENO_A650_H
#include "AdrenoHfiVotes.h"
#ifdef __cplusplus
extern "C" {
#endif
/* The A650 (SM8250, Xperia 1 II) GMU startup configuration: the GPU and GMU performance levels of AdrenoA650Source.h
 * voted on the RPMh rails as a6xx_gmu_rpmh_arc_votes_init does (gfx.lvl against mx.lvl for the GPU, cx.lvl against
 * mx.lvl for the GMU; the a650 has no gmxc rail), the fixed bandwidth table of a6xx_hfi.c a650_build_bw_table, no ACD
 * (the kona tree carries no acd levels and the a650 catalog entry has no ACD quirk) and IFPC as the caller asks (Linux
 * leaves it off for the a650, ADRENO_QUIRK_IFPC is not in its entry; the vendor kgsl turns it on). The rail aux data
 * are the cmd-db bytes the platform read, in the same form AdrenoHfiArcVotes takes; nothing here reads a device. */
typedef struct {const uint8_t *data;size_t bytes;} AdrenoA650Aux;
typedef struct {
    AdrenoA650Aux gfx,mx,cx;
    unsigned enable_ifpc;
} AdrenoA650VoteInputs;
/* Failure leaves the output unchanged. */
AdrenoHfiStartupStatus AdrenoA650BuildStartup(const AdrenoA650VoteInputs *,AdrenoHfiStartupConfig *);
/* The HFI GX_BW_PERF_VOTE indices for GPU level `perf` (1 = the lowest real level, clamped to the table): the level
 * index, and through `bandwidth` the bandwidth index, always 0 on the a650 since its table has the single off level
 * (a6xx_gmu_set_freq votes bw_index 0 without bcms). Returns the level index used. */
unsigned AdrenoA650PerfVote(unsigned perf,unsigned *bandwidth);
#ifdef __cplusplus
}
#endif
#endif
