"""Exercise actual measured upward admission and cold reservation boundaries."""
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

class FGUpwardRecovery(unittest.TestCase):
    def test_warm_or_cold_selection_and_rollback_reservation(self):
        android=ROOT/'nxnative/dm/android'
        src=(android/'dm_framegen.c').read_text()
        def function(name):
            start=src.index(name)
            return src[start:src.index('\n}',start)+2]
        body='\n'.join(function(n) for n in ('static lsfg_graph_budget fg_chain_reservation(',
            'static void fg_upward_poll(','static int fg_cold_pressure_ok(',
            'static int fg_cold_start_replace(','static int fg_cold_retry_start('))
        code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "gfx_vk_fg_pressure.h"
#include "lsfg/lsfg_graph_budget.h"
#define VLOG(...) do{if(0)printf(__VA_ARGS__);}while(0)
enum {LSFG_GRAPH_EMPTY,LSFG_GRAPH_BUILDING,FG_COLD_DRAIN=4,FG_COLD_BUILD,FG_COLD_RETRY};
typedef struct {uint64_t bytes;} lsfg_chain;
typedef struct {unsigned phase;} graph;
typedef struct {unsigned running,scale,cancelled;uint64_t epoch;} measure;
static lsfg_chain old={76*FG_MIB};
static struct {
    lsfg_chain *chain;int flow,upward_seed,seed_policy,mods,ext,out_view[3],cold_job,coordination;
    graph graph;measure upward_measure;
    struct {unsigned pending;} shadow;
    struct {unsigned phase,scale,upward,restoring;uint64_t limit,epoch;
        lsfg_graph_budget reservation,prior_reservation;} cold;
    uint64_t timing_epoch,coordination_ns,active_published_ns;
    double upward_deadline,graph_deadline;
} F;
static dm_fg_heap_snapshot heaps;
static int live=1,stable=1,ready,holds,refused,warm_starts,cold_starts,restore_starts;
static uint64_t lsfg_chain_heap_bytes(const lsfg_chain *c,unsigned i){return c&&i==0?c->bytes:0;}
static uint64_t lsfg_chain_bytes(const lsfg_chain *c){return c?c->bytes:0;}
static int fg_live_enabled(void){return live;}
static int dm_fg_coordination_recover(const int *c,uint64_t t){(void)c;(void)t;return stable;}
static void lsfg_graph_measure_cancel(measure *j){j->cancelled=1;}
static int lsfg_graph_measure_take(measure *j,uint64_t epoch,lsfg_graph_budget *r){
    *r=(lsfg_graph_budget){.limit=UINT64_MAX};if(!ready)return 0;
    if(!j->cancelled&&j->epoch==epoch)assert(lsfg_graph_charge_heap(r,0,80*FG_MIB));
    j->running=0;return 1;
}
static void fg_cancel_request(void){refused++;F.shadow.pending=0;}
static dm_fg_heap_snapshot vk_fg_heap_snapshot(void){return heaps;}
static void vk_quality_recovery_hold(unsigned ms){assert(ms==12000);holds++;}
static uint64_t vk_fg_transition_cap(int up){assert(!up);return 1;}
static void fg_build_upward(void){}
static void fg_build_restore(void){}
static int lsfg_graph_owner_start_reserved(graph *g,const int *mods,int ext,unsigned scale,
        const int *outputs,uint64_t epoch,const lsfg_graph_budget *r,void(*build)(void)){
    (void)mods;(void)ext;(void)outputs;
    assert(scale==55&&epoch==7&&r->used==80*FG_MIB&&r->heap_used[0]==r->used&&build==fg_build_upward);
    assert(F.chain==&old);g->phase=LSFG_GRAPH_BUILDING;warm_starts++;return 1;
}
static int lsfg_graph_job_start_reserved_replace(int *job,const int *mods,int ext,float scale,
        const int *outputs,uint64_t epoch,lsfg_chain *retire,const lsfg_graph_budget *r,void(*build)(void)){
    (void)job;(void)mods;(void)ext;(void)outputs;assert(epoch==7);
    if(build==fg_build_upward){assert(scale==.55f&&retire==&old&&r->used==80*FG_MIB);cold_starts++;}
    else {assert(build==fg_build_restore&&scale==.50f&&!retire&&r->used==76*FG_MIB);restore_starts++;}
    assert(r->used==r->heap_used[0]);return 1;
}
static int lsfg_graph_job_start_replace(int *j,const int *m,int e,float s,const int *v,uint64_t l,uint64_t ep,lsfg_chain *r){
    (void)j;(void)m;(void)e;(void)s;(void)v;(void)l;(void)ep;(void)r;assert(0);return 0;
}
static int lsfg_graph_job_start(int *j,const int *m,int e,float s,const int *v,uint64_t l,uint64_t ep){
    return lsfg_graph_job_start_replace(j,m,e,s,v,l,ep,NULL);
}
''' + body + r'''
static void setup(void){
    memset(&F,0,sizeof F);F.chain=&old;F.flow=50;F.timing_epoch=7;F.shadow.pending=55;
    F.upward_measure=(measure){.running=1,.scale=55,.epoch=7};F.upward_seed=1;F.upward_deadline=20;
    F.active_published_ns=9000000000ull;live=stable=1;ready=0;
    heaps=(dm_fg_heap_snapshot){.sampled_ns=10000000000ull,.os_available=2048*FG_MIB,.thermal=0,
        .count=2,.eligible=3,.budget={1024*FG_MIB,64*FG_MIB},.usage={964*FG_MIB,63*FG_MIB}};
}
int main(void){
    setup();fg_upward_poll(10.);assert(F.upward_measure.running&&F.chain==&old&&!F.cold.phase&&!warm_starts);
    ready=1;fg_upward_poll(10.);
    assert(F.cold.phase==FG_COLD_DRAIN&&F.cold.upward&&F.chain==&old&&!warm_starts&&holds==1);
    assert(F.cold.reservation.used==80*FG_MIB&&F.cold.limit==80*FG_MIB);
    assert(fg_cold_pressure_ok(10.));heaps.thermal=2;assert(!fg_cold_pressure_ok(10.));heaps.thermal=0;
    assert(!fg_cold_pressure_ok(16.));assert(fg_cold_start_replace()&&cold_starts==1);
    assert(F.cold.prior_reservation.used==76*FG_MIB);
    F.chain=NULL;F.cold.restoring=1;F.cold.phase=FG_COLD_RETRY;F.cold.scale=50;
    assert(fg_cold_retry_start()&&restore_starts==1);
    setup();heaps.budget[0]=2048*FG_MIB;ready=1;fg_upward_poll(10.);
    assert(F.graph.phase==LSFG_GRAPH_BUILDING&&!F.cold.phase&&warm_starts==1&&F.chain==&old);
    setup();ready=1;F.active_published_ns=11000000000ull;fg_upward_poll(10.);
    assert(!F.cold.phase&&F.chain==&old&&refused==1);
    setup();ready=1;stable=0;fg_upward_poll(10.);assert(!F.cold.phase&&refused==2&&F.chain==&old);
    setup();ready=1;F.upward_measure.epoch=6;fg_upward_poll(10.);assert(!F.cold.phase&&refused==3);
}
'''
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'upward.c';p.write_text(code);exe=p.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(android),str(p),'-o',str(exe)],check=True)
            r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=5)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
