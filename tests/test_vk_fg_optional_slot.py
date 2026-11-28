"""Exercise real LSFG command-slot reuse with a deterministic busy GPU."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class FGOptionalSlot(unittest.TestCase):
    def test_busy_fence_never_blocks_or_resets_optional_slot(self):
        android = ROOT / 'nxnative/dm/android'
        src = (android / 'dm_framegen.c').read_text()
        start = src.index('static VkResult begin_slot(')
        body = src[start:src.index('\n}', start)+2]
        code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#define VK_NULL_HANDLE 0
#define VK_TRUE 1
#define VK_SUCCESS 0
#define VK_TIMEOUT 2
#define VK_ERROR_DEVICE_LOST (-4)
#define VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO 1
#define VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT 2
#define VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO 3
#define VK_COMMAND_BUFFER_LEVEL_PRIMARY 4
#define VK_STRUCTURE_TYPE_FENCE_CREATE_INFO 5
#define VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO 6
#define VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT 7
#define VK_NOT_READY 1
typedef int VkResult;
typedef struct { int sType; void *pNext; int flags,queueFamilyIndex; } VkCommandPoolCreateInfo;
typedef struct { int sType; void *pNext; int commandPool,level,commandBufferCount; } VkCommandBufferAllocateInfo;
typedef struct { int sType; void *pNext; int flags; } VkFenceCreateInfo;
typedef struct { int sType; void *pNext; int flags; } VkCommandBufferBeginInfo;
typedef struct {int cb,fence,pending;} fg_slot;
static struct {int dev,qf;} V;
static struct {int pool;} F={1};
static int wait_result=VK_TIMEOUT, resets,begins,retired;
static uint64_t timeout_seen;
static int make_pool(int d,const VkCommandPoolCreateInfo *i,void *a,int *p){(void)d;(void)i;(void)a;*p=1;return 0;}
static int make_cb(int d,const VkCommandBufferAllocateInfo *i,int *p){(void)d;(void)i;*p=2;return 0;}
static int make_fence(int d,const VkFenceCreateInfo *i,void *a,int *p){(void)d;(void)i;(void)a;*p=3;return 0;}
static int wait(int d,int n,int *f,int all,uint64_t timeout){
    (void)d;(void)n;(void)f;(void)all;timeout_seen=timeout;
    // Model eventual completion of a blocking wait, but a busy nonblocking poll.
    return timeout && wait_result==VK_TIMEOUT ? VK_SUCCESS : wait_result;
}
static void vk_fg_fence_retired(int f){assert(f==3);retired++;}
static int reset(int c,int flags){(void)c;(void)flags;resets++;return 0;}
static int begin(int c,const VkCommandBufferBeginInfo *i){(void)c;(void)i;begins++;return 0;}
static struct {
    int (*vkCreateCommandPool)(int,const VkCommandPoolCreateInfo*,void*,int*);
    int (*vkAllocateCommandBuffers)(int,const VkCommandBufferAllocateInfo*,int*);
    int (*vkCreateFence)(int,const VkFenceCreateInfo*,void*,int*);
    int (*vkWaitForFences)(int,int,int*,int,uint64_t);
    int (*vkResetCommandBuffer)(int,int);
    int (*vkBeginCommandBuffer)(int,const VkCommandBufferBeginInfo*);
} vk={make_pool,make_cb,make_fence,wait,reset,begin};
''' + body + r'''
int main(void){
    fg_slot s={2,3,1};
    int result=begin_slot(&s);
    if(timeout_seen!=0 || result!=VK_TIMEOUT || !s.pending || resets || begins || retired){
        fprintf(stderr,"busy optional slot blocked or reset: timeout=%llu result=%d pending=%d reset=%d\n",
            (unsigned long long)timeout_seen,result,s.pending,resets);return 1;
    }
    wait_result=VK_ERROR_DEVICE_LOST;
    assert(begin_slot(&s)==VK_ERROR_DEVICE_LOST&&s.pending&&!resets&&!begins&&!retired);
    wait_result=VK_SUCCESS;
    assert(begin_slot(&s)==VK_SUCCESS&&!s.pending&&resets==1&&begins==1&&retired==1);
    fg_slot fresh={0};
    assert(begin_slot(&fresh)==VK_SUCCESS&&fresh.cb&&fresh.fence&&!fresh.pending);
    assert(resets==2&&begins==2&&retired==1);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as d:
            c = Path(d) / 'slot.c'
            c.write_text(code)
            exe = c.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers',str(c),'-o',str(exe)],check=True)
            run = subprocess.run([str(exe)],capture_output=True,text=True,timeout=5)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

    def test_expired_batch_and_busy_slot_do_not_enter_recording(self):
        android = ROOT / 'nxnative/dm/android'
        src = (android / 'dm_framegen.c').read_text()
        start = src.index('    unsigned slot_index=F.fi++%NFG;')
        # The deadline guard, when present, immediately precedes slot reuse.
        marker = '    // Optional work cannot recover an already-expired WSI deadline.'
        if marker in src:
            start = src.index(marker)
        body = src[start:src.index('    VkCommandBuffer cb = slot->cb;', start)]
        code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "gfx_vk_fg_cadence.h"
#include "gfx_vk_present_admission.h"
#define NFG 3
#define VK_TIMEOUT 2
#define VK_ERROR_DEVICE_LOST (-4)
typedef int VkResult;
typedef struct {int cb;} fg_slot;
static struct {
    unsigned fi,count,warm_streak;
    uint64_t source_ns;
    struct {double smoothed_interval;unsigned issued;} pacer;
    fg_slot fr[NFG];
} F;
static double now;
static double mono_s(void){return now;}
static int result_value, begins;
static VkResult begin_slot(fg_slot *s){(void)s;begins++;return result_value;}
static int try_batch(unsigned ng, double period, int fixed_base, uint64_t rns){
    const char *admission_reason=NULL;
    (void)period;(void)fixed_base;(void)rns;
    (void)mono_s;
''' + body + r'''
    return 1;
report:
    assert(admission_reason && ng==0 && F.count==0 && F.warm_streak==0 && F.pacer.issued==0);
    return 0;
command_error:
    return -1;
}
static void reset_history(void){F.count=4;F.warm_streak=3;F.pacer.issued=1;}
int main(void){
    F.source_ns=1000000000;F.pacer.smoothed_interval=1./60.;now=1.020;
    reset_history();
    if(try_batch(1,1./60.,60,8333333)!=0 || begins){
        fprintf(stderr,"expired optional batch reached recording\n");return 1;
    }
    now=1.001;reset_history();result_value=VK_TIMEOUT;
    assert(try_batch(1,1./60.,60,8333333)==0&&begins==1);
    reset_history();result_value=0;
    assert(try_batch(1,1./60.,60,8333333)==1&&begins==2);
    result_value=VK_ERROR_DEVICE_LOST;
    assert(try_batch(1,1./60.,60,8333333)==-1&&begins==3);
    // No optional output deadline exists for a history-only opportunity.
    result_value=0;now=1.020;
    assert(try_batch(0,1./60.,60,8333333)==1&&begins==4);
}
'''
        with tempfile.TemporaryDirectory() as d:
            c = Path(d) / 'admit.c'
            c.write_text(code)
            exe = c.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-label',
                            '-I',str(android),str(c),'-lm','-o',str(exe)],check=True)
            run = subprocess.run([str(exe)],capture_output=True,text=True,timeout=5)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
