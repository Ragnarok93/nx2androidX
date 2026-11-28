"""The actual asynchronous measurement worker must not stall or publish stale requirements."""
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class FGPreflightWorker(unittest.TestCase):
    def test_worker_cancellation_and_ownership(self):
        android=ROOT/'nxnative/dm/android'
        modern=(android/'lsfg/lsfg_graph_measure_job.h').exists()
        code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h>
#include <pthread.h>
#include <sched.h>
#include <vulkan/vulkan_core.h>
#ifdef HAS_MEASURE_JOB
#include "lsfg/lsfg_graph_measure_job.h"
static atomic_int entered,release,fail;
static int measure(VkExtent2D ext,float flow,lsfg_graph_budget *req,const atomic_int *cancel){
    assert(ext.width==1920&&ext.height==1080&&flow==.55f);
    atomic_store(&entered,1);
    while(!atomic_load(&release))sched_yield();
    if(atomic_load(cancel)||atomic_load(&fail))return 0;
    *req=(lsfg_graph_budget){.limit=UINT64_MAX};
    assert(lsfg_graph_charge_heap(req,0,4096));return 1;
}
static void start(lsfg_graph_measure_job *j,uint64_t epoch){
    atomic_store(&entered,0);atomic_store(&release,0);
    assert(lsfg_graph_measure_start(j,(VkExtent2D){1920,1080},55,epoch,measure));
    while(!atomic_load(&entered))sched_yield();
}
static void finish(lsfg_graph_measure_job *j){
    atomic_store(&release,1);while(!atomic_load(&j->done))sched_yield();
}
#endif
int main(void){
#ifndef HAS_MEASURE_JOB
    fprintf(stderr,"no asynchronous allocation-free preflight worker\n");return 1;
#else
    lsfg_graph_measure_job j={0};lsfg_graph_budget r;
    start(&j,1);
    assert(!lsfg_graph_measure_take(&j,1,&r)&&!r.used);
    assert(!lsfg_graph_measure_start(&j,(VkExtent2D){1920,1080},55,1,measure));
    lsfg_graph_measure_cancel(&j);finish(&j);
    assert(lsfg_graph_measure_take(&j,1,&r)&&!r.used&&!j.running);
    start(&j,2);finish(&j);assert(lsfg_graph_measure_take(&j,3,&r)&&!r.used);
    start(&j,3);finish(&j);assert(lsfg_graph_measure_take(&j,3,&r)&&r.used==4096&&r.heap_used[0]==4096);
    atomic_store(&fail,1);start(&j,4);finish(&j);assert(lsfg_graph_measure_take(&j,4,&r)&&!r.used);
    atomic_store(&fail,0);start(&j,5);atomic_store(&release,1);lsfg_graph_measure_drain(&j);
    assert(!j.running&&!lsfg_graph_measure_take(&j,5,&r));
#endif
}
'''
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'worker.c';p.write_text(code);exe=p.with_suffix('')
            headers=ROOT/'nxnative/dm/third_party/vulkan-headers/include'
            cmd=['cc','-std=c11','-Wall','-Wextra','-Werror','-pthread','-I',str(android),'-I',str(headers),str(p),'-o',str(exe)]
            if modern:cmd.append('-DHAS_MEASURE_JOB')
            subprocess.run(cmd,check=True)
            r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=5)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
