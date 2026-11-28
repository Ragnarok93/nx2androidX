"""Exercise the actual queue producer and retirement wait with mocked clock/worker."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class PresentRetirement(unittest.TestCase):
    def test_timeout_and_surface_detach_do_not_orphan_ticket(self):
        android = ROOT / 'nxnative/dm/android'
        src = (android / 'gfx_vk_present.c').read_text()
        def function(marker):
            begin = src.index(marker)
            return src[begin:src.index('\n}', begin)+2]
        code = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "gfx_vk_present_queue_trace.h"
#include "gfx_vk_source_continuity.h"
#define PQ_N 4
typedef int vkimg_t;
typedef struct {void *backing;} vk_reconstruction_input;
static struct {
    vkimg_t *im;
    int crop[4],has_crop,docked,flip_y;
    unsigned replay_flags;
    uint64_t n,source_ns,lifecycle_epoch;
    double period;
    dm_present_queue_trace trace;
    vk_reconstruction_input physical;
} g_pq[PQ_N];
static uint64_t g_pq_head,g_pq_tail,g_pq_done;
static pthread_mutex_t g_pq_mu=PTHREAD_MUTEX_INITIALIZER,g_present_mu=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_pq_cv=PTHREAD_COND_INITIALIZER;
static _Atomic int g_present_thread_started;
static unsigned waits,clock_calls;
static int last_clock;
static uint64_t target;
static int wait_mode;
static int test_clock(clockid_t id,struct timespec *ts){last_clock=id;ts->tv_sec=100+clock_calls++;ts->tv_nsec=0;return 0;}
static int test_wait(pthread_cond_t *cv,pthread_mutex_t *mu,const struct timespec *dl){
    (void)cv;(void)mu;(void)dl;++waits;
    if(wait_mode==1 && waits==1)return 0; // spurious
    if(waits==1)return ETIMEDOUT;
    g_pq_done=target;return 0;
}
#define clock_gettime test_clock
#define pthread_cond_timedwait test_wait
#define VKLOG(...) ((void)0)
#define die(msg) do {fprintf(stderr,"%s\n",msg);abort();}while(0)
static void present_queue_cv_ready(void){} // isolated wait predicate, init tested by native CI
static uint64_t mono_ns(void){return 1000000;}
static uint64_t dm_front_lifecycle_epoch(void){return 2;}
static int vk_pass_dump_user_active(void){return 1;}
int dm_opmode(void){return 0;}
static int vk_window_bottom_up(void){return 0;}
static void *dm_front_window(void){return NULL;}
static void present_thread_start(void){atomic_store(&g_present_thread_started,1);}
static void present_window(vkimg_t *im,const int *crop,double period,uint64_t n,int redraw,uint64_t ns,int docked,int flip,unsigned flags,uint64_t lifecycle_epoch,const vk_reconstruction_input *physical){
    (void)im;(void)crop;(void)period;(void)n;(void)redraw;(void)ns;(void)docked;(void)flip;(void)flags;(void)lifecycle_epoch;(void)physical;
    abort(); // detached source must never call WSI
}
static void vk_rs_variant_destroy(void *b){(void)b;}
static void vk_capture_scale_queue(uint64_t n,dm_present_queue_trace s){(void)n;(void)s;}
''' + function('static uint64_t present_enqueue(') + '\n' + function('void mtl_present_wait(') + r'''
int main(void){
    unsetenv("DM_VK_SYNC_PRESENT");
    target=9;g_pq_done=8;mtl_present_wait(target);
    if(g_pq_done!=target){fprintf(stderr,"timeout granted an unretired texture\n");return 1;}
    assert(waits==2&&last_clock==CLOCK_MONOTONIC&&clock_calls==2);
    waits=clock_calls=0;target=10;wait_mode=1;mtl_present_wait(target);
    assert(g_pq_done==10&&waits==2&&clock_calls==1);
    waits=0;mtl_present_wait(10);assert(!waits);
    vkimg_t image=1;
    // No window and no worker: ordered synchronous retirement.
    assert(present_enqueue(&image,NULL,11,1./60.,0,NULL)==11&&g_pq_done==11);
    // An existing worker already owns ticket 12 when the surface detaches.
    atomic_store(&g_present_thread_started,1);g_pq_head=1;g_pq_tail=0;
    g_pq[0].n=12;g_pq[0].im=&image;g_pq[0].source_ns=100;
    assert(present_enqueue(&image,NULL,13,1./60.,0,NULL)==13);
    assert(g_pq_head==2&&g_pq_done==11&&g_pq[1].n==13&&!g_pq[1].im);
    assert(g_pq[1].trace.entry_occupied==2&&g_pq[1].trace.source_ns);
    puts("timeout predicate, spurious wake, detached ordered retirement: PASS");
}
'''
        with tempfile.TemporaryDirectory() as d:
            c = Path(d) / 'queue.c'
            c.write_text(code)
            exe = c.with_suffix('')
            subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Wno-unused-function','-pthread',
                            '-I',str(android),str(c),'-o',str(exe)],check=True)
            run = subprocess.run([str(exe)],capture_output=True,text=True,timeout=5)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
