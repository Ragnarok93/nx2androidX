"""Exercise production performance interval accounting across hold/resume."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class PerfLifecycle(unittest.TestCase):
    def test_explicit_hold_is_not_work_and_active_stall_is_reported(self):
        android = ROOT / 'nxnative/dm/android'
        src = (android / 'perf_android.c').read_text()
        block = src[src.index('// hle_nvn.c, the first acquire'):src.index('// The window\'s frame rate:')]
        modern = 'g_frame_work_mu' in block
        code = r'''
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include "gfx_vk_source_continuity.h"
#ifndef EXPLICIT_WORK_EPOCH
static int64_t g_work_start;
#endif
static int64_t clock_ns=1000000000,reported;
static uint64_t lifecycle_epoch=2,g_game_period;
static unsigned reports;
static int g_hint=1;
static int64_t g_hint_target=16666667;
static struct {void (*update)(int,int64_t);} H;
static int64_t perf_now(void){return clock_ns;}
uint64_t dm_front_lifecycle_epoch(void){return lifecycle_epoch;}
static void game_mode_sync(void){}
static void hint_init(int64_t p){(void)p;}
static void gpu_turbo(void){}
static int64_t dm_perf_pq_pace(int64_t pq){(void)pq;return 0;}
static void adapt_frame(int64_t w,int64_t t){(void)w;(void)t;}
static void hint_report(int64_t w,int64_t pq,int64_t pace,int64_t t,int64_t target){(void)pq;(void)pace;(void)t;(void)target;reports++;reported=w;}
static void hint_threads_scan(int32_t tid){(void)tid;}
static int gettid(void){return 1;}
''' + block + r'''
int main(void){
    dm_perf_frame_start(0,0);clock_ns+=10000000;dm_perf_frame(1./60.);
    assert(reports==1&&reported==10000000);
    // Acquire before a short menu hold, present after resume.
    dm_perf_frame_start(0,0);lifecycle_epoch=3;clock_ns+=20000000;
    lifecycle_epoch=4;clock_ns+=5000000;dm_perf_frame(1./60.);
    if(reports!=1){fprintf(stderr,"short hold reported as work: %lld\n",(long long)reported);return 1;}
    // First fresh acquire, then a genuine active one-second stall.
    dm_perf_frame_start(0,0);clock_ns+=1000000000;dm_perf_frame(1./60.);
    assert(reports==2&&reported==1000000000);
    // Held tickets must not update the work hint.
    lifecycle_epoch=5;dm_perf_frame_start(0,0);clock_ns+=10000000;dm_perf_frame(1./60.);
    assert(reports==2);
    lifecycle_epoch=6;clock_ns+=3000000000;dm_perf_frame_start(0,0);
    clock_ns+=9000000;dm_perf_frame(1./60.);
    assert(reports==3&&reported==9000000);
    // Legacy frontend still treats a long interval as ambiguous.
    lifecycle_epoch=0;dm_perf_frame_start(0,0);clock_ns+=1000000000;dm_perf_frame(1./60.);
    assert(reports==3);
}
'''
        with tempfile.TemporaryDirectory() as d:
            c=Path(d)/'perf.c';c.write_text(code);exe=c.with_suffix('')
            cmd=['cc','-std=c11','-Wall','-Wextra','-Werror','-pthread','-I',str(android),str(c),'-o',str(exe)]
            if modern:cmd.append('-DEXPLICIT_WORK_EPOCH')
            subprocess.run(cmd,check=True)
            run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=5)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
