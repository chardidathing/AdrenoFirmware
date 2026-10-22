/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "AdrenoA650.h"
#include "AdrenoA650Source.h"
AdrenoHfiStartupStatus AdrenoA650BuildStartup(const AdrenoA650VoteInputs *in,AdrenoHfiStartupConfig *out)
{
    if(!in || !out || in->enable_ifpc>1)return AdrenoHfiStartupInvalid;
    AdrenoHfiStartupConfig c={0};uint32_t votes[16],levels[16];
    for(unsigned i=0;i<ADRENO_A650_GPU_LEVELS;++i)levels[i]=adreno_a650_gpu_levels[i].level;
    if(AdrenoHfiArcVotes(levels,ADRENO_A650_GPU_LEVELS,in->gfx.data,in->gfx.bytes,in->mx.data,in->mx.bytes,votes,16))
        return AdrenoHfiStartupInvalid;
    c.gpu_count=ADRENO_A650_GPU_LEVELS;
    for(unsigned i=0;i<c.gpu_count;++i)c.gpu[i]=(AdrenoHfiPerformanceLevel){adreno_a650_gpu_levels[i].hz,votes[i]};
    for(unsigned i=0;i<ADRENO_A650_GMU_LEVELS;++i)levels[i]=adreno_a650_gmu_levels[i].level;
    if(AdrenoHfiArcVotes(levels,ADRENO_A650_GMU_LEVELS,in->cx.data,in->cx.bytes,in->mx.data,in->mx.bytes,votes,16))
        return AdrenoHfiStartupInvalid;
    c.gmu_count=ADRENO_A650_GMU_LEVELS;
    for(unsigned i=0;i<c.gmu_count;++i)c.gmu[i]=(AdrenoHfiPerformanceLevel){adreno_a650_gmu_levels[i].hz,votes[i]};
    /* a650_build_bw_table, as the generated header carries it: the off level only, so the GMU never changes DDR votes */
    c.bandwidth_count=ADRENO_A650_BW_LEVELS;
    c.cnoc_count=ADRENO_A650_CNOC_COMMANDS;c.cnoc_wait_mask=ADRENO_A650_CNOC_WAIT_MASK;
    c.cnoc_addresses[0]=ADRENO_A650_CNOC_ADDRESS;
    c.cnoc_data[0][0]=adreno_a650_cnoc_data[0];c.cnoc_data[1][0]=adreno_a650_cnoc_data[1];
    c.ddr_count=ADRENO_A650_DDR_COMMANDS;c.ddr_wait_mask=ADRENO_A650_DDR_WAIT_MASK;
    for(unsigned j=0;j<ADRENO_A650_DDR_COMMANDS;++j) {
        c.ddr_addresses[j]=adreno_a650_ddr_addresses[j];
        c.ddr_data[0][j]=adreno_a650_ddr_data[j];
    }
    c.enable_ifpc=in->enable_ifpc;
    if(AdrenoHfiStartupValidate(&c))return AdrenoHfiStartupInvalid;
    *out=c;return AdrenoHfiStartupSuccess;
}
unsigned AdrenoA650PerfVote(unsigned perf,unsigned *bandwidth)
{
    if(perf<1)perf=1;
    if(perf>=ADRENO_A650_GPU_LEVELS)perf=ADRENO_A650_GPU_LEVELS-1;
    if(bandwidth)*bandwidth=0;
    return perf;
}
