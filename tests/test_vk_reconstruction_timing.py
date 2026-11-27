"""Compile the actual timing reader with injected Vulkan query results."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

class ReconstructionTiming(unittest.TestCase):
    def test_query_ring_wrap_and_stale_backend(self):
        h = (ROOT/'nxnative/dm/android/gfx_vk_spatial.h').read_text()
        state = h[h.index('static struct {'):h.index('static void spatial_image_drop(')]
        reader = h[h.index('static void spatial_read_timing('):h.index('static int spatial_active,')]
        source = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include "spatial_upscaler.h"
#include "gfx_vk_reconstruction_limits.h"
#include "gfx_vk_flow_budget.h"
#define SP_TEMPORAL 1
#define VK_GSR2_SLOTS 8
#define VK_SUCCESS 0
#define VK_TRUE 1
#define VK_QUERY_RESULT_64_BIT 1
#define SP_SLOTS 8
#define SP_QUERY_STRIDE 10
#define SP_PASS_COUNT 4
#define SP_IMAGE_COUNT 6
typedef int VkDescriptorSetLayout,VkPipelineLayout,VkDescriptorPool,VkDescriptorSet,VkSampler,VkPipeline,VkFormat,VkCommandPool,VkCommandBuffer,VkFence,VkQueryPool;
typedef struct { uint32_t width,height; } VkExtent2D;
typedef int spatial_image, vk_gsr2, vk_rs_variant;
static struct {int dev; struct {struct {double timestampPeriod;} limits;} props;} V={.props.limits.timestampPeriod=1000000.0};
static unsigned query_calls, query_count, polls;
static unsigned ready_fences;
static int vkWaitForFences(int dev, unsigned count, const VkFence *fences, unsigned all, uint64_t timeout) {
    (void)dev; assert(count == 1 && all == VK_TRUE && timeout == 0);
    polls++;
    return (ready_fences & (1u << *fences)) ? VK_SUCCESS : 1;
}
static int fail_query;
static uint64_t injected[10];
static int vkGetQueryPoolResults(int dev,int pool,unsigned first,unsigned count,size_t size,void *dst,size_t stride,unsigned flags) {
    (void)dev; assert(pool==1 && first%10==0 && size==count*8 && stride==8 && flags==1);
    query_calls++;query_count=count;
    if(fail_query)return 1;
    memcpy(dst,injected,size);return 0;
}
'''+state+reader+r'''
static void put(unsigned slot,uint64_t ticket,int mode,uint64_t gen) {
    g_spatial.query_ticket[slot]=ticket;
    g_spatial.query_mode[slot]=mode;
    g_spatial.query_epoch[slot]=gen;
    g_spatial.query_input[slot]=(VkExtent2D){1280,720};
    g_spatial.query_output[slot]=(VkExtent2D){1920,1080};
    g_spatial.query_budget[slot]=2.5f;
    g_spatial.query_cadence[slot]=33333;
    g_spatial.query_history_epoch[slot]=7;
    g_spatial.query_history_reset[slot]=1;
    g_flow.query_generation[slot]=gen;
    g_flow.budget_ms[slot]=2.5f;
}
int main(void) {
    dm_flow_workload key={.iw=1280,.ih=720,.ow=1920,.oh=1080};
    dm_flow_budget_set_workload(&g_flow.policy,&key);
    uint64_t gen=g_flow.policy.generation;
    g_spatial.queries=1;g_spatial.timestamp_bits=8;
    // A timestamp wrap at 256: 250 -> 254 is 4 ms total; stage 255 -> 0 is 1 ms.
    uint64_t stamps[]={250,254,250,251,255,0,251,252,252,253};
    memcpy(injected,stamps,sizeof stamps);
    put(0,100,SP_SGSR2,gen); spatial_read_timing(0,108);
    assert(query_calls==1 && query_count==10 && !g_spatial.query_ticket[0]);
    assert(g_spatial.measured_ticket==100 && g_spatial.gpu_ms==4);
    assert(g_spatial.measured_stage_ms[1]==1 && g_spatial.measured_history_epoch==7);
    assert(g_flow.policy.slow==1);
    spatial_read_timing(0,109);assert(query_calls==1 && g_flow.policy.slow==1);
    put(1,101,SP_SGSR1,gen);spatial_read_timing(1,109);
    assert(query_count==2 && g_spatial.measured_mode==SP_SGSR1 && g_flow.policy.slow==1);
    assert(g_spatial.measured_stage_ms[1]==0 && !g_spatial.measured_history_epoch);
    put(2,102,SP_SGSR2,gen-1);spatial_read_timing(2,110);
    assert(g_spatial.measured_ticket==102 && g_flow.policy.slow==1);
    put(3,103,SP_SGSR2,gen);fail_query=1;spatial_read_timing(3,111);
    assert(!g_spatial.measured_ticket && !g_spatial.gpu_ms && !g_spatial.query_ticket[3]);
    fail_query=0;spatial_read_timing(3,112);assert(query_calls==4 && g_flow.policy.slow==1);
    // Four more matching temporal samples are enough for demotion; spatial and stale samples did not count.
    for(unsigned i=0;i<4;i++){put(i,104+i,SP_SGSR2,gen);spatial_read_timing(i,112+i);}
    assert(g_flow.policy.retry_after==235 && g_flow.policy.generation!=gen);
    // A fresh ring: do not wait eight frames to use the first completed query.
    memset(&g_spatial, 0, sizeof g_spatial);
    g_spatial.queries=1; g_spatial.timestamp_bits=8;
    for(unsigned i=0;i<SP_SLOTS;i++) g_spatial.fence[i]=i;
    put(6,200,SP_SGSR1,gen); put(7,201,SP_SGSR2,gen);
    ready_fences=1u<<7; unsigned before=query_calls;
    spatial_retire_timing(0,202);
    assert(polls==1 && query_calls==before && g_spatial.query_ticket[6]==200);
    ready_fences|=1u<<6;
    spatial_retire_timing(0,202);
    assert(polls==2 && query_calls==before+1 && g_spatial.measured_ticket==200);
    assert(!g_spatial.query_ticket[6] && g_spatial.query_ticket[7]==201);
    // Consuming the next oldest result leaves the fence and later tickets intact.
    spatial_retire_timing(0,203);
    assert(polls==3 && query_calls==before+2 && g_spatial.measured_ticket==201);
    assert(g_spatial.fence[7]==7 && !g_spatial.query_ticket[7]);
    spatial_retire_timing(0,204);
    assert(polls==3 && query_calls==before+2);
    // The reused slot has already been waited by the recorder: no extra poll.
    put(0,204,SP_SGSR1,gen); ready_fences=0;
    spatial_retire_timing(0,212);
    assert(polls==3 && query_calls==before+3 && g_spatial.measured_ticket==204);
    g_spatial.queries=0; put(1,213,SP_SGSR1,gen);
    spatial_retire_timing(2,214);
    assert(polls==3 && query_calls==before+3);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as d:
            p=Path(d); (p/'case.c').write_text(source)
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(ROOT/'nxnative/dm/android'),str(p/'case.c'),'-o',str(p/'case')],check=True)
            subprocess.run([str(p/'case')],check=True)

if __name__=='__main__': unittest.main()
