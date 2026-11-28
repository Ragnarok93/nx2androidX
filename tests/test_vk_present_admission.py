"""Exercise the real pre-submit acquire path with deterministic WSI responses."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class PresentAdmission(unittest.TestCase):
    def test_generated_wait_is_bounded_and_timeout_never_records(self):
        android = ROOT / 'nxnative/dm/android'
        source = (android / 'gfx_vk_present.c').read_text()
        start = source.index('static int present_one(')
        end = source.index('    int turned =', start)
        body = source[start:end]
        modern = 'optional_deadline_ns' in body
        code = r'''
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#ifdef NEW_ADMISSION
#include "gfx_vk_present_admission.h"
#endif
#define VK_TRUE 1
#define VK_NULL_HANDLE 0
#define VK_SUCCESS 0
#define VK_NOT_READY 1
#define VK_TIMEOUT 2
#define VK_SUBOPTIMAL_KHR 1000001003
#define VK_ERROR_OUT_OF_DATE_KHR (-1000001004)
#define VK_ERROR_SURFACE_LOST_KHR (-1000000000)
#define VKLOG(...) ((void)0)
#define VKRED_ONCE(...) ((void)0)
typedef int VkImage, VkResult, vkimg_t, VkFence;
typedef struct {uint32_t width,height;} VkExtent2D;
static struct {int dev;} V;
static int g_present_failed,g_swap=1,g_pfence[8],g_acq[8];
static unsigned g_pi;
static VkExtent2D g_sext={1920,1080};
static uint64_t now=1000000000, fence_timeout, acquire_timeout;
static unsigned acquire_calls, fence_calls, recreates;
static int wait_result, acquire_result;
static uint64_t mono_ns(void){return now;}
static uint64_t vk_refresh_cycle_ns(void){return 8333333;}
static VkExtent2D picture_extent(int w,int h){return (VkExtent2D){(unsigned)w,(unsigned)h};}
static void swap_destroy(void){g_swap=0;}
static int swap_make(void){recreates++;g_swap=1;return 1;}
static int vkWaitForFences(int d,int n,int *f,int all,uint64_t timeout){
    (void)d;(void)n;(void)f;(void)all;fence_calls++;fence_timeout=timeout;return wait_result;
}
static void spatial_forget_fence(int f){(void)f;}
static int vkAcquireNextImageKHR(int d,int sw,uint64_t timeout,int s,int f,uint32_t *idx){
    (void)d;(void)sw;(void)s;(void)f;acquire_calls++;acquire_timeout=timeout;
    if(acquire_result==VK_SUCCESS||acquire_result==VK_SUBOPTIMAL_KHR)*idx=0;
    return acquire_result;
}
''' + body + r'''
    (void)im;(void)src;(void)flip_y;(void)period;(void)ticket;(void)with_meta;
    (void)cx;(void)cy;(void)dx;(void)dy;(void)stall_start;(void)stall_fence;(void)stall_acquire;
    return 1; // reached recording; no actual Vulkan calls beyond acquire
}
static int attempt(int generated,uint64_t deadline,const char **why){
    int crop[4]={0,0,1920,1080};
#ifdef NEW_ADMISSION
    return present_one(NULL,1,crop,g_sext,0,1./120.,generated?0:42,!generated,deadline,generated?why:NULL,VK_NULL_HANDLE);
#else
    (void)deadline;(void)why;
    return present_one(NULL,1,crop,g_sext,0,1./120.,generated?0:42,!generated);
#endif
}
int main(void){
    const char *why=NULL;
    uint64_t deadline=now+5000000;
    acquire_result=VK_TIMEOUT;
    if(attempt(1,deadline,&why)){fprintf(stderr,"acquire timeout reached recording\n");return 1;}
    assert(fence_timeout==0 && acquire_timeout<=5000000 && acquire_timeout>0);
    unsigned called=acquire_calls, fences=fence_calls;
    assert(!attempt(1,now,&why)&&acquire_calls==called&&fence_calls==fences);
    wait_result=VK_TIMEOUT;
    assert(!attempt(1,deadline,&why)&&acquire_calls==called);
    wait_result=VK_SUCCESS;acquire_result=VK_NOT_READY;
    assert(!attempt(1,deadline,&why));
    acquire_result=VK_ERROR_OUT_OF_DATE_KHR;
    assert(!attempt(1,deadline,&why)&&recreates==0);
    acquire_result=VK_SUCCESS;
    assert(attempt(1,deadline,&why));
    acquire_result=VK_SUBOPTIMAL_KHR;
    assert(attempt(1,deadline,&why));
    acquire_result=VK_TIMEOUT;
    assert(!attempt(0,0,NULL)&&acquire_timeout>=50000000&&acquire_timeout<=100000000);
    acquire_result=VK_SUCCESS;
    assert(attempt(0,0,NULL));
#ifdef NEW_ADMISSION
    assert(dm_present_optional_deadline(now,1./120.,1)>now);
    assert(!dm_present_optional_deadline(now,0,1));
    assert(fabs(dm_present_real_period(1./60.,1./120.,1,0)-1./60.)<1e-8);
    assert(fabs(dm_present_real_period(1./60.,1./120.,1,1)-1./120.)<1e-8);
    assert(fabs(dm_present_real_period(1./30.,1./120.,3,1)-.025)<1e-8);
#endif
    puts("finite WSI timeout, rejected acquire, chronological real interval: PASS");
}
'''
        with tempfile.TemporaryDirectory() as d:
            c = Path(d) / 'present.c'
            c.write_text(code)
            exe = c.with_suffix('')
            cmd = ['cc','-std=c11','-Wall','-Wextra','-Wno-unused-function','-Wno-unused-parameter',
                   '-I', str(android), str(c), '-lm', '-o', str(exe)]
            if modern:
                cmd.append('-DNEW_ADMISSION')
            subprocess.run(cmd, check=True)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
