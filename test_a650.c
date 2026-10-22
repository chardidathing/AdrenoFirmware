/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* The a650 startup configuration over synthetic cmd-db rail data and the software GMU peer of test_startup.c. */
#include "AdrenoA650.h"
#include "AdrenoA650Source.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void put16(uint8_t *p,unsigned n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
/* rails as the kona AOP publishes them: ascending corners, zero first (the values here are a fixture, not the phone's) */
static uint8_t gfx[2*8],mx[2*8],cx[2*8];
static void rails(void)
{
    const unsigned g[8]={0,16,48,64,128,192,256,320},m[8]={0,16,48,64,128,192,256,320},c[8]={0,16,48,64,128,192,256,320};
    for(unsigned i=0;i<8;++i){put16(gfx+2*i,g[i]);put16(mx+2*i,m[i]);put16(cx+2*i,c[i]);}
}
static uint32_t *shared;static AdrenoHfi hfi;
static struct {unsigned count,ids[8],words[8];uint32_t messages[8][159];} peer;
static int notify(void *cookie)
{
    CHECK(cookie==&peer && peer.count<8);
    unsigned at=shared[16];uint32_t header=shared[1024+at];unsigned n=(header>>8)&255;
    unsigned index=peer.count++;peer.ids[index]=header&255;peer.words[index]=n-1;
    for(unsigned i=1;i<n;++i)peer.messages[index][i-1]=shared[1024+((at+i)&1023)];
    shared[16]=shared[17];
    volatile uint32_t *q=shared+18;unsigned w=q[11];
    shared[2048+w]=(3u<<8)|126u;shared[2048+((w+1)&1023)]=header;shared[2048+((w+2)&1023)]=0;shared[2048+((w+3)&1023)]=0xfafafafa;
    q[11]=(w+4)&1023;
    return 0;
}
static uint64_t now_value=1000;
static int now(void *cookie,uint64_t *value){CHECK(cookie==&peer);*value=now_value;return 0;}
static int wait_us(void *cookie,unsigned us){CHECK(cookie==&peer);now_value+=us;return 0;}
static int health(void *cookie){CHECK(cookie==&peer);return 0;}
int main(void)
{
    rails();
    AdrenoA650VoteInputs in={{gfx,sizeof gfx},{mx,sizeof mx},{cx,sizeof cx},0};
    AdrenoHfiStartupConfig c;memset(&c,0xa5,sizeof c);
    CHECK(AdrenoA650BuildStartup(&in,&c)==AdrenoHfiStartupSuccess);
    /* four GPU levels from the kona tree with their corners voted on gfx/mx, two GMU levels on cx/mx */
    CHECK(c.gpu_count==4 && c.gpu[0].hz==0 && c.gpu[1].hz==290000000 && c.gpu[2].hz==381000000 && c.gpu[3].hz==480000000);
    CHECK(c.gpu[0].vote==0 && c.gpu[1].vote==((64u<<16)|(3u<<8)|3u) && c.gpu[2].vote==((128u<<16)|(4u<<8)|4u) && c.gpu[3].vote==((192u<<16)|(5u<<8)|5u));
    CHECK(c.gmu_count==2 && c.gmu[1].hz==200000000 && c.gmu[1].vote==((48u<<16)|(2u<<8)|2u) && c.gmu[0].vote==0);
    for(unsigned i=4;i<16;++i)CHECK(!c.gpu[i].hz && !c.gpu[i].vote);
    /* a650_build_bw_table */
    CHECK(c.bandwidth_count==1 && c.ddr_count==3 && c.ddr_wait_mask==1 && c.cnoc_count==1 && c.cnoc_wait_mask==1);
    CHECK(c.ddr_addresses[0]==0x50000 && c.ddr_addresses[1]==0x50004 && c.ddr_addresses[2]==0x5007c && !c.ddr_addresses[3]);
    CHECK(c.ddr_data[0][0]==0x40000000 && c.ddr_data[0][1]==0x40000000 && c.ddr_data[0][2]==0x40000000 && !c.ddr_data[1][0]);
    CHECK(c.cnoc_addresses[0]==0x500a4 && c.cnoc_data[0][0]==0x40000000 && c.cnoc_data[1][0]==0x60000001);
    CHECK(!c.acd_enable_mask && !c.acd_count && !c.enable_ifpc);
    /* the perf vote names the level and bandwidth 0 */
    unsigned bw=7;
    CHECK(AdrenoA650PerfVote(0,&bw)==1 && bw==0 && AdrenoA650PerfVote(2,&bw)==2 && AdrenoA650PerfVote(9,NULL)==3);
    /* the whole startup over the software peer: perf table, bw table, core start, start (no ACD stage), ifpc when asked */
    CHECK(!posix_memalign((void **)&shared,4096,16384));
    for(unsigned ifpc=0;ifpc<2;++ifpc) {
        in.enable_ifpc=ifpc;
        CHECK(AdrenoA650BuildStartup(&in,&c)==AdrenoHfiStartupSuccess && c.enable_ifpc==ifpc);
        memset(&peer,0,sizeof peer);now_value=1000;
        CHECK(AdrenoHfiInitializeStopped(&hfi,shared,16384,0x60005000,&peer,notify)==AdrenoHfiSuccess);
        AdrenoHfiStartup state={0};AdrenoHfiStartupResult result;
        AdrenoHfiStartupServices services={&peer,now,wait_us,health,5,10,100,500};
        CHECK(AdrenoHfiStartupRun(&state,&hfi,&c,&services,&result)==AdrenoHfiStartupSuccess);
        CHECK(peer.count==4u+ifpc && peer.ids[0]==4 && peer.ids[1]==3 && peer.ids[peer.count-2]==14 && peer.ids[peer.count-1]==10);
        if(ifpc)CHECK(peer.ids[2]==11 && peer.messages[2][0]==9 && peer.messages[2][1]==1 && peer.messages[2][2]==0x1680);
        /* the perf table on the wire: counts, then {vote, acd 0xffffffff, kHz} per GPU level and {vote, kHz} per GMU level */
        CHECK(peer.words[0]==58 && peer.messages[0][0]==4 && peer.messages[0][1]==2);
        CHECK(peer.messages[0][2+3]==c.gpu[1].vote && peer.messages[0][3+3]==0xffffffff && peer.messages[0][4+3]==290000);
        CHECK(peer.messages[0][50+2]==c.gmu[1].vote && peer.messages[0][51+2]==200000);
        CHECK(peer.words[1]==159 && peer.messages[1][0]==1 && peer.messages[1][1]==1 && peer.messages[1][2]==3);
    }
    free(shared);
    /* refusals: a rail the levels do not fit, missing data, an ifpc flag out of range */
    AdrenoHfiStartupConfig before;memset(&before,0x5a,sizeof before);AdrenoHfiStartupConfig after=before;
    uint8_t low[2*3];put16(low,0);put16(low+2,16);put16(low+4,48);
    AdrenoA650VoteInputs bad=in;bad.gfx=(AdrenoA650Aux){low,sizeof low};bad.enable_ifpc=0;
    CHECK(AdrenoA650BuildStartup(&bad,&after)==AdrenoHfiStartupInvalid && !memcmp(&before,&after,sizeof before));
    bad=in;bad.cx=(AdrenoA650Aux){NULL,0};
    CHECK(AdrenoA650BuildStartup(&bad,&after)==AdrenoHfiStartupInvalid && !memcmp(&before,&after,sizeof before));
    bad=in;bad.enable_ifpc=2;
    CHECK(AdrenoA650BuildStartup(&bad,&after)==AdrenoHfiStartupInvalid && !memcmp(&before,&after,sizeof before));
    CHECK(AdrenoA650BuildStartup(NULL,&after)==AdrenoHfiStartupInvalid && AdrenoA650BuildStartup(&in,NULL)==AdrenoHfiStartupInvalid);
    CHECK(ADRENO_A650_ZAP_PAS_ID==13 && ADRENO_A650_ZAP_REGION_BYTES==0x2000 && ADRENO_A650_CHIP_ID==0x06050000);
    printf("PASS: a650 startup configuration from synthetic rails, 2 startup sequences over the software peer, 5 refusals\n");
    return 0;
}
