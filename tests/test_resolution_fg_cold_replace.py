"""Test actual cold transition and worker with fake Vulkan fence/allocator boundaries."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ANDROID = ROOT / 'nxnative/dm/android'

class ColdReplace(unittest.TestCase):
    def test_drain_destroy_then_build_and_retry(self):
        runtime = (ANDROID / 'dm_framegen.c').read_text()
        advance = runtime[runtime.index('static int fg_cold_advance('):runtime.index('static void full_barrier(')]
        source = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <sched.h>
#define VK_VERSION_1_0 1
typedef uint64_t VkImage, VkImageView, VkCommandBuffer, VkQueryPool, VkFence;
typedef struct {uint32_t width,height;} VkExtent2D;
#define VK_TRUE 1
#define VK_SUCCESS 0
#include "lsfg/lsfg_graph_job.h"
#include "gfx_vk_fg_governor.h"
#define VLOG(...) ((void)0)
enum {NFG=3, FG_COLD_DRAIN=4, FG_COLD_BUILD, FG_COLD_RETRY};
typedef struct {VkFence fence;int pending;} fg_slot;
typedef struct {unsigned active_slots;} owner;
static struct {
    lsfg_graph_job cold_job;
    struct {unsigned phase,scale,failures,prior_scale,restoring;uint64_t limit,epoch,prior_limit;double retry_at;} cold;
    int cold_init,flow,auto_applied,requested_flow;
    lsfg_chain *chain;lsfg_modules mods;VkExtent2D ext;VkImageView out_view[3];
    owner graph;fg_slot fr[NFG];
    uint64_t timing_epoch,count,active_published_ns;unsigned warm_streak;
    struct {size_t issued;} pacer;
    dm_fg_timing timing_latest;dm_fg_governor shadow;unsigned shadow_rows[8];
} F;
struct lsfg_chain {uint64_t bytes;};
static atomic_int live, entered, release_build, fail_build;
static unsigned signalled, forgotten, cancelled;
static uint64_t cap=6;
static struct {int dev;} V;
static int wait_fences(int dev,unsigned n,const VkFence *f,int all,uint64_t timeout) {
    (void)dev;assert(n==1&&all&&timeout==0);
    return signalled&(1u<<(*f-1))?VK_SUCCESS:1;
}
static struct {int (*vkWaitForFences)(int,unsigned,const VkFence *,int,uint64_t);} vk={wait_fences};
static void vk_fg_fence_retired(VkFence f){forgotten|=1u<<(f-1);}
static void lsfg_graph_owner_retire_slot(owner *o,unsigned i){o->active_slots&=~(1u<<i);}
static void fg_cancel_request(void){++cancelled;}
static uint64_t vk_fg_transition_cap(int upward){assert(!upward);return cap;}
static lsfg_chain *make_chain(uint64_t bytes){
    lsfg_chain *p=malloc(sizeof *p);assert(p);p->bytes=bytes;
    assert(atomic_fetch_add(&live,1)==0);return p;
}
uint64_t lsfg_chain_bytes(const lsfg_chain *p){return p?p->bytes:0;}
void lsfg_chain_free(lsfg_chain *p){if(p){assert(atomic_fetch_sub(&live,1)==1);free(p);}}
lsfg_chain *lsfg_chain_build_cancelable(const lsfg_modules *mods,VkExtent2D e,float f,
        const VkImageView v[LSFG_MAX_GENERATIONS],uint64_t bytes,const atomic_int *cancel){
    assert(mods==&F.mods&&e.width==1920&&e.height==1080&&f==(F.cold.restoring?.50f:.45f)&&bytes==100);
    assert(v[0]==1&&v[1]==2&&v[2]==3);
    assert(atomic_load(&live)==0); // old graph must already be destroyed
    atomic_store(&entered,1);
    while(!atomic_load(&release_build))sched_yield();
    if(atomic_load(&fail_build)||atomic_load(cancel))return NULL;
    return make_chain(F.cold.restoring?100:90);
}
static void wait_entered(void){while(!atomic_load(&entered))sched_yield();}
static void finish_build(void){atomic_store(&release_build,1);while(!atomic_load(&F.cold_job.done))sched_yield();}
static int fg_cold_pressure_ok(double now){(void)now;return vk_fg_transition_cap(0)!=0;}
static int fg_cold_start_replace(void){
    return lsfg_graph_job_start_replace(&F.cold_job,&F.mods,F.ext,F.cold.scale/100.0f,F.out_view,F.cold.limit,F.cold.epoch,F.chain);
}
static int fg_cold_retry_start(void){
    return lsfg_graph_job_start(&F.cold_job,&F.mods,F.ext,F.cold.scale/100.0f,F.out_view,F.cold.limit,F.cold.epoch);
}
''' + advance + r'''
static void reset_request(void){
    F.chain=make_chain(100);F.flow=50;F.requested_flow=-1;F.timing_epoch=7;
    F.ext=(VkExtent2D){1920,1080};F.out_view[0]=1;F.out_view[1]=2;F.out_view[2]=3;
    F.cold.phase=FG_COLD_DRAIN;F.cold.scale=45;F.cold.limit=100;F.cold.epoch=7;
    F.graph.active_slots=3;F.fr[0]=(fg_slot){1,1};F.fr[1]=(fg_slot){2,1};
    cap=6;signalled=forgotten=0;atomic_store(&entered,0);atomic_store(&release_build,0);
}
int main(void){
    reset_request();
    signalled=1;assert(!fg_cold_advance(1)&&F.chain&&F.graph.active_slots==2&&forgotten==1);
    signalled=3;assert(!fg_cold_advance(2)&&!F.chain&&F.cold.phase==FG_COLD_BUILD);
    wait_entered();assert(!atomic_load(&live)&&!fg_cold_advance(3));
    finish_build();assert(fg_cold_advance(4)&&F.chain&&F.flow==45&&F.cold_init&&!F.cold.phase);
    assert(F.timing_epoch==8&&F.count==0&&F.warm_streak==0&&lsfg_chain_bytes(F.chain)==90);
    lsfg_chain_free(F.chain);F.chain=NULL;
    reset_request();cap=0;assert(fg_cold_advance(5)&&F.chain&&!F.cold.phase&&cancelled==1);
    lsfg_chain_free(F.chain);F.chain=NULL;
    reset_request();signalled=3;atomic_store(&fail_build,1);
    assert(!fg_cold_advance(6));wait_entered();finish_build();
    assert(!fg_cold_advance(7)&&F.cold.phase==FG_COLD_RETRY&&!F.chain&&F.cold.retry_at==9);
    assert(F.cold.restoring&&F.cold.scale==50&&F.cold.limit==100);
    cap=0;assert(!fg_cold_advance(10)&&!F.cold_job.running);
    cap=6;atomic_store(&entered,0);atomic_store(&release_build,0);atomic_store(&fail_build,0);
    assert(!fg_cold_advance(11));wait_entered();finish_build();
    assert(fg_cold_advance(12)&&F.chain&&F.flow==50);
    assert(F.shadow.failed_requests==1&&F.shadow.backoff>=4.0&&lsfg_chain_bytes(F.chain)==100);
    lsfg_chain_free(F.chain);F.chain=NULL;assert(!atomic_load(&live));
    return 0;
}'''
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / 'case.c'
            p.write_text(source)
            exe = p.with_suffix('')
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pthread',
                '-I', str(ANDROID), str(p), '-lm', '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True, timeout=10)
