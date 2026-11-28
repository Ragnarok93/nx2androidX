"""Exercise reservation, wait, and speculative failure in the graphics pipeline map."""
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class PipelineClaim(unittest.TestCase):
    def test_one_compiler_and_failed_speculation_retry(self):
        code=r'''
#include <assert.h>
#include <stdatomic.h>
#include "gfx_vk_pipeline_claim.h"
static nvn_hmap map={.klen=sizeof(uint64_t)};
static pthread_mutex_t mu=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv=PTHREAD_COND_INITIALIZER;
static uint64_t key=1;
static atomic_int entered,completed;
static void *demand(void *arg){
    pthread_mutex_lock(&mu);
    assert(!vk_pipe_reserve_locked(&map,&key,VK_PIPE_BUILDING));
    atomic_store(&entered,1);
    void *p=vk_pipe_wait_locked(&map,&key,&mu,&cv,0);
    if(arg){assert(!p);assert(vk_pipe_reserve_locked(&map,&key,VK_PIPE_BUILDING));}
    else assert(p==(void *)(uintptr_t)16);
    atomic_store(&completed,1);
    pthread_mutex_unlock(&mu);return NULL;
}
int main(void){
    for(int fail=0;fail<2;fail++){
        key++;atomic_store(&entered,0);atomic_store(&completed,0);
        pthread_mutex_lock(&mu);assert(vk_pipe_reserve_locked(&map,&key,VK_PIPE_BUILDING));pthread_mutex_unlock(&mu);
        pthread_t t;assert(!pthread_create(&t,NULL,demand,fail?(void*)1:NULL));
        while(!atomic_load(&entered)){}
        pthread_mutex_lock(&mu);assert(!atomic_load(&completed));
        assert(vk_pipe_finish_locked(&map,&key,&cv,VK_PIPE_BUILDING,fail?VK_PIPE_FAILED:(void*)16,1));
        pthread_mutex_unlock(&mu);pthread_join(t,NULL);assert(atomic_load(&completed));
        if(fail){pthread_mutex_lock(&mu);assert(vk_pipe_finish_locked(&map,&key,&cv,VK_PIPE_BUILDING,(void*)17,0));pthread_mutex_unlock(&mu);}
    }
    key++;pthread_mutex_lock(&mu);
    assert(vk_pipe_reserve_locked(&map,&key,VK_PIPE_PENDING));
    assert(vk_pipe_wait_locked(&map,&key,&mu,&cv,1)==VK_PIPE_PENDING);
    assert(!vk_pipe_finish_locked(&map,&key,&cv,VK_PIPE_BUILDING,(void*)20,0));
    assert(vk_pipe_finish_locked(&map,&key,&cv,VK_PIPE_PENDING,NULL,0));
    assert(vk_pipe_wait_locked(&map,&key,&mu,&cv,0)==VK_PIPE_FAILED);
    pthread_mutex_unlock(&mu);free(map.keys);free(map.vals);return 0;
}'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'case.c';p.write_text(code);exe=p.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-pthread','-I',str(ROOT/'nxnative/dm/android'),'-I',str(ROOT/'nxnative/dm/gfx/nvn'),str(p),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True,timeout=10)
