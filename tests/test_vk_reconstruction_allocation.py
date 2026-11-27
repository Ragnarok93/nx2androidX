"""Run the production spatial allocation/fallback block with a fault-injected allocator.

This is a host control-flow test, not Vulkan image or shader validation.
"""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

class ReconstructionAllocation(unittest.TestCase):
    def test_mandatory_images_precede_optional_sharpening(self):
        header=(ROOT/'nxnative/dm/android/gfx_vk_spatial.h').read_text()
        match=header[header.index('static int spatial_image_matches('):header.index('static int spatial_image_ready(')]
        start=header.index('    int wants_post = (mode == SP_BILINEAR || flow)')
        end=header.index('    if (direct) {',start)
        block=header[start:end]
        source=r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "gfx_vk_reconstruction_limits.h"
#include "spatial_upscaler.h"
typedef unsigned VkFormat, VkImageUsageFlags;
typedef struct { unsigned width, height; } VkExtent2D;
enum { SP_SOURCE, SP_INTERMEDIATE, SP_OUTPUT, SP_RCAS, SP_IMAGE_COUNT=3 };
enum { VK_IMAGE_USAGE_TRANSFER_SRC_BIT=1, VK_IMAGE_USAGE_TRANSFER_DST_BIT=2,
       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT=4, VK_IMAGE_USAGE_SAMPLED_BIT=8,
       VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT=16 };
#define SP_TEMPORAL 1
typedef struct { struct { int img; unsigned w,h,fmt; } texture;
    VkFormat view_fmt; VkImageUsageFlags usage; uint64_t last_use; unsigned bytes; } spatial_image;
static VkFormat g_sfmt=37;
static struct { spatial_image image[SP_IMAGE_COUNT]; dm_recon_shared_retry shared_output_retry; VkExtent2D report_allocated;
    uint64_t report_alloc_wait_ns, rcas_retry_after, retry_after; unsigned report_alloc_waits; } g_spatial;
static unsigned cap, failure_shared, shared_attempts, evictions, deferred, last_mode, sharpened;
static unsigned first_mandatory, first_optional, attempts;
'''+match+r'''
static void spatial_image_drop(spatial_image *s) { if(s==&g_spatial.image[SP_INTERMEDIATE] && s->texture.img) ++evictions; *s=(spatial_image){0}; }
static int spatial_wait(uint64_t fence,uint64_t *elapsed) { assert(fence); ++*elapsed; return 0; }
static int spatial_pipeline_ready(int pass) { (void)pass; return 1; }
static void spatial_flow_defer(uint64_t ticket) { assert(ticket); ++deferred; }
static void spatial_report(int requested,int active,const char *reason,uint64_t ticket) { (void)requested;(void)reason;(void)ticket;last_mode=active; }
static int spatial_image_ready(int index,unsigned w,unsigned h,VkImageUsageFlags usage,VkFormat fmt) {
    spatial_image *s=&g_spatial.image[index];
    if(spatial_image_matches(s,w,h,usage,fmt))return 1;
    ++attempts;
    if(index==SP_INTERMEDIATE){if(!first_optional)first_optional=attempts;}
    else if(!first_mandatory)first_mandatory=attempts;
    if(index==SP_OUTPUT && (usage&7)==7){++shared_attempts;if(failure_shared){--failure_shared;return 0;}}
    spatial_image_drop(s);
    unsigned used=0;for(unsigned i=0;i<SP_IMAGE_COUNT;++i)used+=g_spatial.image[i].bytes;
    unsigned bytes=index==SP_SOURCE?20:40;
    if(used+bytes>cap)return 0;
    *s=(spatial_image){.texture={1,w,h,g_sfmt},.view_fmt=fmt,.usage=usage,.bytes=bytes,.last_use=1};
    return 1;
}
static spatial_image *run(int requested,int flow,float sharpness,unsigned w,uint64_t ticket) {
    int mode=flow?SP_SGSR2:requested;
    unsigned h=1080,iw=1280,ih=720;
    unsigned raster_features=16|32,sampling=32;
    VkFormat sample_fmt=g_sfmt;
    spatial_image *output=&g_spatial.image[SP_OUTPUT],*intermediate=&g_spatial.image[SP_INTERMEDIATE];
    const char *reason=NULL;
    int disable_post=0,reclaimed_optional=0;
select_spatial_bilinear:;
select_backend:;
    int raster=!flow&&mode!=SP_BILINEAR;
    int rcas=mode==SP_FSR1&&sharpness>0.001f&&ticket>=g_spatial.rcas_retry_after;
    int stage_needed=!flow;
    VkImageUsageFlags stage_usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|(raster?VK_IMAGE_USAGE_SAMPLED_BIT:VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
'''+block+r'''
    last_mode=mode;sharpened=post||rcas;return output;
}
static void reset(unsigned budget) { memset(&g_spatial,0,sizeof g_spatial);cap=budget;failure_shared=shared_attempts=evictions=deferred=last_mode=sharpened=first_mandatory=first_optional=attempts=0; }
int main(void) {
    reset(85);
    assert(run(SP_BILINEAR,0,1,1920,100));
    assert(first_mandatory && first_optional>first_mandatory);
    assert(last_mode==SP_BILINEAR&&!sharpened);
    reset(65);
    g_spatial.image[SP_INTERMEDIATE]=(spatial_image){.texture={1,1920,1080,37},.usage=8,.view_fmt=37,.bytes=40,.last_use=1};
    assert(run(SP_BILINEAR,0,0,1920,100));
    assert(evictions==1 && g_spatial.report_alloc_waits==1 && last_mode==SP_BILINEAR);
    reset(100);failure_shared=1;
    assert(run(SP_BILINEAR,0,0,1920,100));
    assert(shared_attempts==1 && g_spatial.shared_output_retry.after==220);
    assert(run(SP_BILINEAR,0,0,1920,221));
    assert(shared_attempts==1); // do not reallocate a working narrow output
    assert(run(SP_BILINEAR,0,0,2560,222));
    assert(shared_attempts==2 && !g_spatial.shared_output_retry.after);
    reset(60);
    assert(run(SP_SGSR2,1,1,1920,100));
    assert(last_mode==SP_SGSR2&&!sharpened&&!deferred);
    reset(85);
    assert(run(SP_FSR1,0,1,1920,100));
    assert(last_mode==SP_FSR1&&!sharpened);
    return 0;
}'''
        with tempfile.TemporaryDirectory() as d:
            d=Path(d);(d/'case.c').write_text(source)
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(ROOT/'nxnative/dm/android'),str(d/'case.c'),'-o',str(d/'case')],check=True)
            subprocess.run([str(d/'case')],check=True)

if __name__=='__main__': unittest.main()
