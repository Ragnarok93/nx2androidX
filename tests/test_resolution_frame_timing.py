"""Concurrent replay timing readers must not associate a reused slot with an old frame."""
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class FrameTiming(unittest.TestCase):
    def test_accumulation_and_concurrent_ring_reuse(self):
        source=r'''
#include <assert.h>
#include <stdatomic.h>
#include "gfx_vk_frame_timing.h"
static vk_frame_timing t=VK_FRAME_TIMING_INIT;
static atomic_uint latest;
static atomic_int done;
static void *write_frames(void *unused){
    (void)unused;
    for(unsigned f=1;f<=100000;f++){
        vk_frame_timing_add(&t,f,(uint64_t)f*1000000);
        atomic_store(&latest,f);
    }
    atomic_store(&done,1);
    return NULL;
}
static void *read_frames(void *unused){
    (void)unused;
    do {
        unsigned f=atomic_load(&latest);
        if(f){
            double v=vk_frame_timing_ms(&t,f);
            assert(v==-1||v==f);
            if(f>64){
                v=vk_frame_timing_ms(&t,f-64);
                assert(v==-1||v==f-64);
            }
        }
    }while(!atomic_load(&done));
    return NULL;
}
int main(void){
    assert(vk_frame_timing_ms(&t,0)==-1);
    vk_frame_timing_add(&t,0,1500000);
    vk_frame_timing_add(&t,0,500000);
    assert(vk_frame_timing_ms(&t,0)==2);
    vk_frame_timing_add(&t,64,1000000);
    assert(vk_frame_timing_ms(&t,0)==-1&&vk_frame_timing_ms(&t,64)==1);
    pthread_t a,b,c;
    // Separate instance from deterministic accumulation test.
    pthread_mutex_lock(&t.mu);
    for(unsigned i=0;i<VK_FRAME_TIMING_SLOTS;i++)t.slots[i].tag=t.slots[i].ns=0;
    pthread_mutex_unlock(&t.mu);
    assert(!pthread_create(&a,NULL,write_frames,NULL));
    assert(!pthread_create(&b,NULL,read_frames,NULL));
    assert(!pthread_create(&c,NULL,read_frames,NULL));
    pthread_join(a,NULL);pthread_join(b,NULL);pthread_join(c,NULL);
    assert(vk_frame_timing_ms(&t,100000)==100000);
    return 0;
}'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'case.c';p.write_text(source);exe=p.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-pthread','-I',str(ROOT/'nxnative/dm/android'),str(p),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True,timeout=10)
