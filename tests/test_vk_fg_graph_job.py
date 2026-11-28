"""Test real background job ownership using a blocked fake Vulkan constructor."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class FGGraphJob(unittest.TestCase):
    def test_nonblocking_poll_cancel_and_epoch(self):
        code=r'''
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <vulkan/vulkan_core.h>
#include "lsfg/lsfg_graph_job.h"
struct lsfg_chain {int id;};
static pthread_mutex_t mu=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv=PTHREAD_COND_INITIALIZER;
static int entered,release,fail,builds,frees;
static lsfg_modules mods;
static VkImageView views[LSFG_MAX_GENERATIONS]={(VkImageView)(uintptr_t)1,(VkImageView)(uintptr_t)2,(VkImageView)(uintptr_t)3};
lsfg_chain *lsfg_chain_build(const lsfg_modules *m,VkExtent2D e,float f,const VkImageView v[LSFG_MAX_GENERATIONS],uint64_t cap){
    assert(m==&mods&&e.width==1920&&e.height==1080&&f==.5f&&cap==4096);
    for(unsigned i=0;i<LSFG_MAX_GENERATIONS;i++)assert(v[i]==views[i]);
    pthread_mutex_lock(&mu);
    entered=1;pthread_cond_broadcast(&cv);
    while(!release)pthread_cond_wait(&cv,&mu);
    ++builds;
    pthread_mutex_unlock(&mu);
    if(fail)return NULL;
    lsfg_chain *c=malloc(sizeof *c);assert(c);c->id=builds;return c;
}
void lsfg_chain_free(lsfg_chain *c){if(c){++frees;free(c);}}
static void wait_enter(void){pthread_mutex_lock(&mu);while(!entered)pthread_cond_wait(&cv,&mu);pthread_mutex_unlock(&mu);}
static void unblock(void){pthread_mutex_lock(&mu);release=1;pthread_cond_broadcast(&cv);pthread_mutex_unlock(&mu);}
static void start(lsfg_graph_job *j,uint64_t epoch){
    entered=release=0;
    assert(lsfg_graph_job_start(j,&mods,(VkExtent2D){1920,1080},.5f,views,4096,epoch));
    wait_enter();
}
static lsfg_chain *take(lsfg_graph_job *j,uint64_t epoch){
    while(!atomic_load_explicit(&j->done,memory_order_acquire)){}
    lsfg_chain *out=NULL;assert(lsfg_graph_job_take(j,epoch,&out));return out;
}
int main(void){
    lsfg_graph_job j={0};lsfg_chain *out=NULL;
    start(&j,1);
    assert(!lsfg_graph_job_take(&j,1,&out)&&!out);
    assert(!lsfg_graph_job_start(&j,&mods,(VkExtent2D){1920,1080},.5f,views,4096,1));
    lsfg_graph_job_cancel(&j);
    assert(!lsfg_graph_job_take(&j,1,&out)&&j.running);
    unblock();assert(!take(&j,1)&&frees==1&&!j.running);
    start(&j,2);unblock();assert(!take(&j,3)&&frees==2);
    start(&j,3);unblock();out=take(&j,3);assert(out&&out->id==3&&frees==2);
    lsfg_chain_free(out);assert(frees==3);
    fail=1;start(&j,4);unblock();assert(!take(&j,4)&&frees==3&&!j.running);
    fail=0;start(&j,5);unblock();lsfg_graph_job_drain(&j);assert(frees==4&&!j.running);
    lsfg_graph_job_drain(&j);assert(frees==4&&builds==5);
    assert(!lsfg_graph_job_start(&j,&mods,(VkExtent2D){1920,1080},.5f,views,0,5));
    return 0;
}'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'case.c';p.write_text(code);exe=p.with_suffix('')
            headers=ROOT/'nxnative/dm/third_party/vulkan-headers/include'
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-pthread',
                            '-I',str(headers),'-I',str(ROOT/'nxnative/dm/android'),str(p),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True,timeout=10)
