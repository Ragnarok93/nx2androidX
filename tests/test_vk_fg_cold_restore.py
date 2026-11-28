"""Exercise production cold transition rollback with a deterministic worker boundary."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class FGColdRestore(unittest.TestCase):
    def test_failed_replacement_restores_last_valid_scale(self):
        src = (ROOT / 'nxnative/dm/android/dm_framegen.c').read_text()
        body = src[src.index('static int fg_cold_advance('):src.index('static void full_barrier(')]
        code = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#define NFG 3
#define VK_TRUE 1
#define VK_SUCCESS 0
#define VLOG(...) do { if (0) printf(__VA_ARGS__); } while (0)
enum { FG_COLD_DRAIN=4,FG_COLD_BUILD,FG_COLD_RETRY };
typedef int VkFence;
typedef struct {uint64_t bytes;} lsfg_chain;
typedef struct {int pending;VkFence fence;} fg_slot;
typedef struct {unsigned active_slots;} graph;
typedef struct {int running,cancel;uint64_t epoch;} job;
typedef struct {unsigned applied,failed_requests;double backoff;} governor;
static lsfg_chain old={100},next={90},restored={100};
static struct {
    struct {unsigned phase,scale,failures,prior_scale,restoring;uint64_t limit,epoch,prior_limit;double retry_at;} cold;
    job cold_job;int mods,ext,out_view[3],flow,auto_applied,cold_init,requested_flow;
    lsfg_chain *chain;graph graph;fg_slot fr[NFG];
    uint64_t timing_epoch,count,active_published_ns;unsigned warm_streak;
    struct {unsigned issued;} pacer;
    struct {int valid;} timing_latest;
    governor shadow;unsigned shadow_rows[8];
} F;
static struct {int dev;} V;
static unsigned started,cancelled;
static int live=1,complete,fail;
static uint64_t cap=6,requested_bytes;
static float requested_scale;
static int wait(int d,int n,VkFence *f,int all,uint64_t timeout){(void)d;(void)n;(void)f;(void)all;assert(!timeout);return 0;}
static struct {int (*vkWaitForFences)(int,int,VkFence*,int,uint64_t);} vk={wait};
static void vk_fg_fence_retired(VkFence f){(void)f;}
static void lsfg_graph_owner_retire_slot(graph *g,unsigned i){g->active_slots&=~(1u<<i);}
static uint64_t vk_fg_transition_cap(int up){assert(!up);return cap;}
static uint64_t lsfg_chain_bytes(lsfg_chain *c){return c?c->bytes:0;}
static void fg_cancel_request(void){cancelled++;}
static int lsfg_graph_job_start_replace(job *j,int *mods,int ext,float scale,const int *out,uint64_t limit,uint64_t epoch,lsfg_chain *retire){
    (void)mods;(void)ext;(void)out;assert(!j->running);if(retire){assert(live==1);live=0;}else assert(!live);
    started++;requested_scale=scale;requested_bytes=limit;j->running=1;j->cancel=0;j->epoch=epoch;return 1;
}
static int lsfg_graph_job_start(job *j,int *mods,int ext,float scale,const int *out,uint64_t limit,uint64_t epoch){return lsfg_graph_job_start_replace(j,mods,ext,scale,out,limit,epoch,NULL);}
static void lsfg_graph_job_cancel(job *j){j->cancel=1;}
static int lsfg_graph_job_take(job *j,uint64_t epoch,lsfg_chain **out){
    if(!complete)return 0;
    assert(j->running&&!live);j->running=0;*out=NULL;
    if(!fail&&!j->cancel&&j->epoch==epoch){*out=requested_scale==.5f?&restored:&next;live=1;}
    return 1;
}
static void dm_fg_governor_reset(governor *g,unsigned scale,uint64_t epoch){(void)epoch;*g=(governor){.applied=scale};}
static void dm_fg_governor_preset(governor *g,int f){(void)g;(void)f;}
static int fg_cold_pressure_ok(double now){(void)now;return vk_fg_transition_cap(0)!=0;}
static int fg_cold_start_replace(void){
    return lsfg_graph_job_start_replace(&F.cold_job,&F.mods,F.ext,F.cold.scale/100.0f,F.out_view,F.cold.limit,F.cold.epoch,F.chain);
}
static int fg_cold_retry_start(void){
    return lsfg_graph_job_start(&F.cold_job,&F.mods,F.ext,F.cold.scale/100.0f,F.out_view,F.cold.limit,F.cold.epoch);
}
''' + body + r'''
int main(void){
    F.chain=&old;F.flow=50;F.timing_epoch=7;F.cold.phase=FG_COLD_DRAIN;
    F.cold.scale=45;F.cold.limit=100;F.cold.epoch=7;
    assert(!fg_cold_advance(1)&&!F.chain&&started==1&&!live);
    assert(requested_scale==.45f&&requested_bytes==100);
    complete=fail=1;
    assert(!fg_cold_advance(2)&&F.cold.phase==FG_COLD_RETRY);
    if(F.cold.scale!=50 || !F.cold.restoring){fprintf(stderr,"failed graph still retries requested scale %u\n",F.cold.scale);return 1;}
    assert(F.cold.limit==100&&F.cold.retry_at==4);
    assert(!fg_cold_advance(3)&&started==1);
    cap=0;assert(!fg_cold_advance(5)&&started==1);
    cap=6;complete=fail=0;
    assert(!fg_cold_advance(6)&&started==2&&requested_scale==.5f&&requested_bytes==100);
    complete=1;assert(fg_cold_advance(7)&&F.chain==&restored&&F.flow==50&&live==1);
    assert(!F.cold.phase&&F.cold_init&&F.shadow.backoff>=2.&&F.shadow.failed_requests>=1);
    // Pressure or a newer epoch before releasing the old graph cannot discard it.
    F.cold.phase=FG_COLD_DRAIN;F.cold.scale=45;F.cold.limit=100;F.cold.epoch=7;
    assert(fg_cold_advance(8)&&F.chain==&restored&&live==1&&cancelled==1);
}
'''
        with tempfile.TemporaryDirectory() as d:
            c=Path(d)/'cold.c';c.write_text(code);exe=c.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],check=True)
            run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=5)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
