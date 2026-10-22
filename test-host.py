#!/usr/bin/env python3
"""build and run this kit's tests with a plain clang, no xcrun

the transport (AdrenoHfi.c) is AArch64 only, so on another host point CC at a cross compiler and RUN at qemu-user:
  CC          the compiler (default clang); CFLAGS_EXTRA/LDFLAGS_EXTRA are appended
  RUN         a runner prefix for the test binaries, e.g. qemu-aarch64
  OUT         build directory (default build/host-tests under this repository)
  SANITIZE    sanitizer flags (default -fsanitize=address,undefined; use -fsanitize=undefined -fsanitize-trap=undefined for a
              static cross build)
  GMU_FIRMWARE_A740, GMU_FIRMWARE_A650   the GMU firmware files for test_gmu and test_gmu_a650 (skipped without)
  ZAP_IMAGE       an assembled zap image for test_zap's real-image case (AdrenoFirmwareEmbed.py writes one)
  ADRENO_SOURCE   the pinned Linux tree (drivers/gpu/drm/msm under it) for the source oracles of test_startup and
                  test_votes; without it test_startup runs its software peer alone and test_votes is skipped, since it
                  links the oracle unconditionally"""
import os, re, shlex, subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent
out = Path(os.environ.get('OUT', ROOT / 'build/host-tests'))
out.mkdir(parents=True, exist_ok=True)
cc = shlex.split(os.environ.get('CC', 'clang'))
extra = shlex.split(os.environ.get('CFLAGS_EXTRA', ''))
ldextra = shlex.split(os.environ.get('LDFLAGS_EXTRA', ''))
run = shlex.split(os.environ.get('RUN', ''))
sanitize = shlex.split(os.environ.get('SANITIZE', '-fsanitize=address,undefined'))
flags = ['-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-sign-compare', '-Wno-unknown-warning-option',
         '-Wno-missing-designated-field-initializers', *sanitize, *extra, '-I', str(ROOT)]
source = os.environ.get('ADRENO_SOURCE')
msm = Path(source) / 'drivers/gpu/drm/msm' if source else None


def build(name, sources, defines=(), plain=()):
    """plain sources are the pinned Linux oracles: built without the sanitizer, whose shift checks trip on their u16
    promotions (the kit code stays fully instrumented)"""
    binary = out / name
    objects = []
    for source in plain:
        obj = out / (Path(source).stem + '.o')
        subprocess.run([*cc, *[f for f in flags if not f.startswith('-fsanitize')], *defines, '-c', str(source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    cmd = [*cc, *flags, *defines, *[str(s) for s in sources], *objects, *ldextra, '-o', str(binary)]
    print('build', name)
    subprocess.run(cmd, check=True)
    return binary


def check(binary, args, expected):
    result = subprocess.run([*run, str(binary), *args], check=True, capture_output=True, text=True, timeout=600)
    print(result.stdout, end='')
    if expected not in result.stdout:
        raise SystemExit('expected coverage absent from ' + binary.name)


def function(text, signature):
    start = text.index(signature)
    at = text.index('{', start) + 1
    depth = 1
    while depth:
        if text[at] == '{':
            depth += 1
        elif text[at] == '}':
            depth -= 1
        at += 1
    return text[start:at]


def startup_oracle():
    """the pinned a6xx_hfi.c startup functions behind the AdrenoHfiStartupConfig, as UniXNU's check-adreno-hfi-startup.py"""
    hfi = (msm / 'adreno/a6xx_hfi.c').read_text()
    header = (msm / 'adreno/a6xx_hfi.h').read_text()
    prelude = '''#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "AdrenoHfiStartup.h"
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef uint64_t u64;typedef int32_t s32;
typedef int spinlock_t;typedef int atomic_t;
#define __packed __attribute__((packed))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define DRM_DEV_ERROR(...) ((void)0)
''' + header + '''
#define GMU_IDLE_STATE_IFPC 3
struct adreno_gpu {int unused;};
struct a6xx_gmu {
 int legacy,nr_gpu_freqs,nr_gmu_freqs,idle_level;void *dev;
 u64 gpu_freqs[16],gmu_freqs[4];u32 gx_arc_votes[16],cx_arc_votes[4];
 struct a6xx_hfi_acd_table acd_table;struct a6xx_hfi_msg_bw_table bw;
};
struct a6xx_gpu {struct adreno_gpu base;struct a6xx_gmu gmu;};
static int adreno_is_a8xx(const struct adreno_gpu *gpu){(void)gpu;return 0;}
static int a8xx_hfi_send_perf_table(struct a6xx_gmu *gmu){(void)gmu;return -1;}
static int a6xx_hfi_start_v1(struct a6xx_gmu *gmu,int boot){(void)gmu;(void)boot;return -1;}
static unsigned *sink_count,*sink_ids,*sink_words,fail_at;static u32 (*sink_data)[159];
static int a6xx_hfi_send_msg(struct a6xx_gmu *g,int id,void *msg,u32 bytes,u32 *output,u32 capacity) {
 (void)g;(void)output;(void)capacity;unsigned i=(*sink_count)++;
 if(i>=8 || bytes<4 || bytes>640 || bytes%4)return -1;
 sink_ids[i]=(unsigned)id;sink_words[i]=bytes/4-1;memcpy(sink_data[i],(u32 *)msg+1,bytes-4);
 return fail_at==i+1?-1:0;
}
static int a6xx_hfi_send_bw_table(struct a6xx_gmu *g) {
 return a6xx_hfi_send_msg(g,HFI_H2F_MSG_BW_TABLE,&g->bw,sizeof(g->bw),NULL,0);
}
#define IFPC_LONG_HYST 0x1680
'''
    functions = ['static int a6xx_hfi_send_perf_table(', 'static int a6xx_hfi_feature_ctrl_msg(', 'static int a6xx_hfi_enable_ifpc(',
                 'static int a6xx_hfi_enable_acd(', 'static int a6xx_hfi_send_start(', 'static int a6xx_hfi_send_core_fw_start(',
                 'int a6xx_hfi_start(']
    wrapper = '''
int source_startup(const AdrenoHfiStartupConfig *c,unsigned fail,unsigned *count,unsigned *ids,unsigned *words,u32 data[8][159]) {
 struct a6xx_gpu gpu={0};struct a6xx_gmu *g=&gpu.gmu;
 g->nr_gpu_freqs=c->gpu_count;g->nr_gmu_freqs=c->gmu_count;
 for(unsigned i=0;i<c->gpu_count;++i){g->gpu_freqs[i]=c->gpu[i].hz;g->gx_arc_votes[i]=c->gpu[i].vote;}
 for(unsigned i=0;i<c->gmu_count;++i){g->gmu_freqs[i]=c->gmu[i].hz;g->cx_arc_votes[i]=c->gmu[i].vote;}
 g->bw.bw_level_num=c->bandwidth_count;g->bw.cnoc_cmds_num=c->cnoc_count;g->bw.ddr_cmds_num=c->ddr_count;
 g->bw.cnoc_wait_bitmask=c->cnoc_wait_mask;g->bw.ddr_wait_bitmask=c->ddr_wait_mask;
 memcpy(g->bw.cnoc_cmds_addrs,c->cnoc_addresses,sizeof(c->cnoc_addresses));memcpy(g->bw.cnoc_cmds_data,c->cnoc_data,sizeof(c->cnoc_data));
 memcpy(g->bw.ddr_cmds_addrs,c->ddr_addresses,sizeof(c->ddr_addresses));memcpy(g->bw.ddr_cmds_data,c->ddr_data,sizeof(c->ddr_data));
 g->acd_table.version=c->acd_version;g->acd_table.enable_by_level=c->acd_enable_mask;g->acd_table.stride=c->acd_stride;g->acd_table.num_levels=c->acd_count;
 memcpy(g->acd_table.data,c->acd_data,sizeof(c->acd_data));g->idle_level=c->enable_ifpc?GMU_IDLE_STATE_IFPC:0;
 *count=0;sink_count=count;sink_ids=ids;sink_words=words;sink_data=data;fail_at=fail;
 return a6xx_hfi_start(g,0);
}
'''
    oracle = out / 'original-startup-oracle.c'
    oracle.write_text(prelude + '\n'.join(function(hfi, f) for f in functions) + wrapper)
    return oracle


def votes_oracle():
    """the pinned a6xx_gmu.c ARC and BCM arithmetic, as UniXNU's check-adreno-hfi-votes.py"""
    gmu = (msm / 'adreno/a6xx_gmu.c').read_text()
    hfi = (msm / 'adreno/a6xx_hfi.c').read_text()
    gmuh = (msm / 'adreno/a6xx_gmu.h').read_text()
    tcs = (Path(source) / 'include/soc/qcom/tcs.h').read_text()
    tcs = tcs[tcs.index('#define BCM_TCS_CMD_COMMIT_MASK'):tcs.index('#endif')]
    bcm = gmuh[gmuh.index('struct a6xx_bcm {'):gmuh.index('\n};', gmuh.index('struct a6xx_bcm {')) + 3]
    db = gmu[gmu.index('struct bcm_db {'):gmu.index('\n};', gmu.index('struct bcm_db {')) + 3]
    prelude = '''#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "AdrenoHfiVotes.h"
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef uint64_t u64;typedef int32_t s32;
typedef uint16_t __le16;typedef uint32_t __le32;typedef int spinlock_t;typedef int atomic_t;
#define __packed __attribute__((packed))
#define BIT(n) (1u<<(n))
#define GENMASK(h,l) ((UINT32_MAX>>(31-(h)))& (UINT32_MAX<<(l)))
static u32 u32_encode_bits(u32 value,u32 mask){return (value<<__builtin_ctz(mask))&mask;}
#define IS_ERR(p) ((p)==NULL)
#define PTR_ERR(p) (-1)
#define EINVAL 22
#define DRM_DEV_ERROR(...) ((void)0)
#define dev_err(...) ((void)0)
#define le16_to_cpu(x) (x)
#define le32_to_cpu(x) (x)
#define do_div(n,d) ((n)/=(d))
#define clamp(v,lo,hi) ((v)<(lo)?(lo):(v)>(hi)?(hi):(v))
#define GMU_MAX_BCMS 3
''' + tcs + '\n' + (msm / 'adreno/a6xx_hfi.h').read_text() + '\n' + bcm + '\n' + db + '''
struct device {int unused;};struct adreno_gpu {int unused;};
struct a6xx_info {const struct a6xx_bcm *bcms;};
struct a6xx_gmu {struct device *dev;unsigned nr_gpu_bws;u32 gpu_bw_table[16],gpu_ib_votes[16][3];};
static int adreno_is_a7xx(const struct adreno_gpu *g){(void)g;return 1;}
static int adreno_is_a8xx(const struct adreno_gpu *g){(void)g;return 0;}
static const u32 *opp_levels;static u16 rails[2][256];static size_t rail_sizes[2];
static struct bcm_db aux[3];static u32 addrs[3],cn0;
static const void *cmd_db_read_aux_data(const char *name,size_t *count) {
 if(name[0]=='p' || name[0]=='s'){unsigned i=name[0]=='s';*count=rail_sizes[i];return rails[i];}
 unsigned i=(unsigned)(name[0]-'0');*count=sizeof(aux[i]);return aux+i;
}
static u32 cmd_db_read_addr(const char *name){return name[0]=='C'?cn0:addrs[name[0]-'0'];}
static unsigned a6xx_gmu_get_arc_level(struct device *dev,unsigned long freq){(void)dev;return opp_levels[freq];}
'''
    wrapper = '''
int source_arc(const u32 *levels,unsigned count,const u8 *p,size_t pn,const u8 *s,size_t sn,u32 *votes) {
 unsigned long freq[16];opp_levels=levels;rail_sizes[0]=pn;rail_sizes[1]=sn;
 memcpy(rails[0],p,pn);memcpy(rails[1],s,sn);for(unsigned i=0;i<count;++i)freq[i]=i;
 return a6xx_gmu_rpmh_arc_votes_init(NULL,votes,freq,(int)count,"p","s");
}
int source_bandwidth(const u32 *bw,unsigned count,const AdrenoHfiBcm *b,unsigned n,u32 address,u32 out[159]) {
 struct a6xx_bcm policy[4]={0};struct a6xx_info info={policy};struct a6xx_gmu g={0};
 struct adreno_gpu gpu={0};char names[3][2]={{'0',0},{'1',0},{'2',0}};
 for(unsigned j=0;j<n;++j){policy[j]=(struct a6xx_bcm){names[j],b[j].buswidth,b[j].fixed,b[j].perfmode,b[j].perfmode_kbps};
 aux[j]=(struct bcm_db){b[j].unit,b[j].width,b[j].vcd,0};addrs[j]=b[j].address;}
 cn0=address;g.nr_gpu_bws=count;memcpy(g.gpu_bw_table,bw,count*4);
 int status=a6xx_gmu_rpmh_bw_votes_init(&gpu,&info,&g);if(status)return status;
 struct a6xx_hfi_msg_bw_table msg={0};a6xx_generate_bw_table(&info,&g,&msg);
 memcpy(out,(u8 *)&msg+4,sizeof(msg)-4);return 0;
}
'''
    oracle = out / 'original-votes-oracle.c'
    oracle.write_text(prelude + function(gmu, 'static int a6xx_gmu_rpmh_arc_votes_init(') + '\n' +
                      function(gmu, 'static int a6xx_gmu_rpmh_bw_votes_init(') + '\n' +
                      function(hfi, 'static void a6xx_generate_bw_table(') + wrapper)
    return oracle


hfi = [ROOT / 'AdrenoHfi.c']
startup = hfi + [ROOT / 'AdrenoHfiStartup.c']
check(build('test-hfi', hfi + [ROOT / 'test_hfi.c']), [], 'modern HFI')
if msm:
    check(build('test-startup', startup + [ROOT / 'test_startup.c'], ['-DSOURCE_ORACLE'], [startup_oracle()]), [], 'four startup profiles')
    check(build('test-votes', [ROOT / 'AdrenoHfiVotes.c', *startup, ROOT / 'test_votes.c'], (), [votes_oracle()]), [], 'PASS')
else:
    check(build('test-startup', startup + [ROOT / 'test_startup.c']), [], 'four startup profiles')
    print('skip test-votes: set ADRENO_SOURCE to the pinned Linux tree for its oracle')
for name, test, expected in [('GMU_FIRMWARE_A740', 'test_gmu.c', '15-record GMU plan'), ('GMU_FIRMWARE_A650', 'test_gmu_a650.c', '16-record a650 GMU plan')]:
    gmu = os.environ.get(name)
    if gmu:
        check(build(Path(test).stem.replace('_', '-'), [ROOT / 'AdrenoFirmware.c', ROOT / test]), [gmu], expected)
    else:
        print('skip', test, 'set', name, 'to the GMU firmware file')
check(build('test-a650', startup + [ROOT / 'AdrenoHfiVotes.c', ROOT / 'AdrenoA650.c', ROOT / 'test_a650.c']), [],
      'a650 startup configuration from synthetic rails, 2 startup sequences over the software peer, 5 refusals')
zap = os.environ.get('ZAP_IMAGE')
check(build('test-zap', [ROOT / 'AdrenoZap.c', ROOT / 'test_zap.c']), [zap] if zap else [],
      '1 a650-shaped zap plan, 1 fixed-address plan, 14 refusals, %d real images' % (1 if zap else 0))
print('PASS: kit tests built and run with', ' '.join(cc), 'under', ' '.join(run) or 'the host')
