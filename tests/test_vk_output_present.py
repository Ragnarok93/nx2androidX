"""Execute the production present_window against recording external boundaries."""
import pathlib, subprocess, tempfile, unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
class OutputPresent(unittest.TestCase):
    def test_real_generated_redraw_and_fallback(self):
        code=(ROOT/'nxnative/dm/android/gfx_vk_present.c').read_text()
        start=code.index('static void present_window(')
        end=code.index('\n}',start)+2
        src=r'''
#include <stdint.h>
#include <assert.h>
#include <stdatomic.h>
#include <stddef.h>
#include <string.h>
#include "gfx_vk_reconstruction_limits.h"
#include "gfx_vk_output_target.h"
#include "gfx_vk_source_continuity.h"
#include "gfx_vk_flow_budget.h"
#include "gfx_vk_capture_scale.h"
#include "spatial_upscaler.h"
typedef struct {uint32_t width,height;} VkExtent2D;
typedef int VkFormat,VkFence,VkImage;
#define VK_NULL_HANDLE 0
typedef struct {uint32_t w,h;VkFormat fmt;VkImage img;} vkimg_t;
typedef struct {vkimg_t texture;VkFence last_use;} spatial_image;
typedef struct {int backing;} vk_reconstruction_input;
static dm_recon_continuity g_recon_source;
static struct {uint64_t ticket,last_real_ticket;dm_flow_budget policy;} g_flow;
#define SP_OUTPUT 0
static struct {
    int flow_pressure, live_sources, encoding, report_physical, measured_mode, report_sharpness_requested, report_sharpness_applied, measured_history_reset;
    VkExtent2D report_input, report_allocated, measured_input, measured_output;
    VkImage report_image;
    int report_crop[4], report_recorded;
    uint64_t report_completed_ticket;
    uint64_t retry_after, rcas_retry_after, ticket, measured_ticket, measured_epoch, measured_history_epoch, report_alloc_wait_ns, report_slot_wait_ns;
    unsigned flow_cadence_us, measured_cadence, report_alloc_attempts, report_allocations, report_alloc_waits;
    double flow_budget_ms, gpu_ms, measured_budget, measured_stage_ms[4];
    spatial_image image[1];
} g_spatial;
static int g_present_failed,g_want=37,spatial_requested,spatial_active;
static const char *spatial_reason;
static _Atomic uint64_t g_fg_out_ns;
static _Atomic int g_fg_images;
static int configured=2160,mode=SP_SGSR2,force_fail,fg_on=1;
static unsigned rec_calls,fg_calls,present_calls,capture_calls,reset_flags;
static VkExtent2D rec_target,fg_input,display={1920,1080};
static VkImage fg_image;
static VkExtent2D g_sext={1920,1080},g_next={1920,1080};
static struct {VkImage image;int w,h,meta,flip;VkExtent2D display;uint64_t ticket;} observed[4];
static vk_capture_scale_present_sample cap;
int dm_recon_output_height(void){return configured;}
int dm_spatial_mode(void){return mode;}
int dm_perf_rate_fps(void){return 30;}
static int reconstruction_pressure(dm_flow_cadence cadence){assert(cadence.period_us);return 0;}
void vk_guest_flow_feedback(uint64_t ticket,uint32_t cadence,int stressed){assert(ticket&&cadence&&!stressed);}
static void spatial_collect_sources(void){}
static VkFormat present_window_format(VkFormat f){return f;}
static void swap_destroy(void){dm_recon_source_invalidate(&g_recon_source);g_spatial.ticket=0;}
static int swap_make(void){return 1;}
static VkExtent2D picture_extent(int w,int h){VkExtent2D p;dm_recon_picture_extent(w,h,display.width,display.height,&p.width,&p.height);return p;}
static spatial_image *spatial_reconstruct(vkimg_t *im,const int *raw,int flip,uint32_t w,uint32_t h,uint64_t ticket,const vk_reconstruction_input *physical){
    (void)flip;(void)physical;rec_calls++;rec_target=(VkExtent2D){w,h};
    spatial_requested=mode;g_spatial.report_input=(VkExtent2D){(unsigned)raw[2],(unsigned)raw[3]};
    g_spatial.ticket=0;spatial_active=SP_NATIVE;
    if(mode==SP_NATIVE||force_fail||!w||!h||(w<=(unsigned)raw[2]&&h<=(unsigned)raw[3]))return NULL;
    spatial_active=mode;g_spatial.ticket=ticket;g_spatial.image[0].texture=(vkimg_t){w,h,im->fmt,22};return &g_spatial.image[0];
}
static int dm_fg_frame(vkimg_t *im,const int *rect,int flip,unsigned encoding,unsigned reset,double period,VkImage gen[3],double *hold,double *out_period,VkFence *consumer){
    (void)flip;(void)encoding;(void)period;
    fg_image=im->img;
    fg_calls++;fg_input=(VkExtent2D){(unsigned)rect[2],(unsigned)rect[3]};reset_flags=reset;
    if(!fg_on)return 0;
    gen[0]=33;*hold=.01;*out_period=.01;*consumer=44;return 1;
}
int vk_pass_dump_user_active(void){return 1;}
void vk_capture_scale_present(uint64_t ticket,vk_capture_scale_present_sample sample){assert(ticket);cap=sample;capture_calls++;}
static int present_one(vkimg_t *im,VkImage src,const int *crop,VkExtent2D picture,int flip,double period,uint64_t ticket,int meta){
    (void)im;(void)period;assert(present_calls<4);unsigned i=present_calls++;
    observed[i].image=src;observed[i].w=crop[2];observed[i].h=crop[3];observed[i].meta=meta;observed[i].display=picture;observed[i].flip=flip;observed[i].ticket=ticket;
    return src != 11;
}
'''+code[start:end]+r'''
static vkimg_t src={1920,1080,37,11};
static void real(uint64_t ticket,int docked){present_calls=0;present_window(&src,NULL,1.0/30,ticket,0,ticket*33333333,docked,1,NULL);}
int main(void){
    real(1,1);
    assert(rec_target.width==3840&&rec_target.height==2160); // must not inherit drawable limit
    assert(rec_calls==1&&fg_calls==1&&fg_input.width==3840&&fg_input.height==2160);
    assert(present_calls==2&&observed[0].image==33&&observed[1].image==22);
    for(unsigned i=0;i<2;i++)assert(observed[i].w==3840&&observed[i].h==2160&&observed[i].display.width==1920&&observed[i].display.height==1080&&!observed[i].flip);
    assert(observed[0].ticket==0&&observed[1].ticket==1);
    assert(fg_image==22&&g_spatial.image[0].last_use==44);
    assert(cap.reconstruction_submitted&&cap.actual_w==3840&&cap.actual_h==2160);
    assert(cap.present_accepted==1&&cap.generated_accepted==1);
    // A paused setting change must not generate or reconstruct the old ticket.
    configured=1440;present_calls=0;
    present_window(&src,NULL,-1,1,1,33333333,1,1,NULL);
    assert(rec_calls==1&&fg_calls==1&&capture_calls==1&&present_calls==1&&observed[0].w==3840);
    g_spatial.retry_after=121;g_spatial.rcas_retry_after=121;
    real(2,1);assert(!g_spatial.retry_after&&!g_spatial.rcas_retry_after);assert(rec_target.width==2560&&rec_target.height==1440&&reset_flags==DM_RECON_SOURCE_GEOMETRY);
    g_spatial.retry_after=122;g_spatial.rcas_retry_after=122;
    real(3,1);assert(!reset_flags&&g_spatial.retry_after==122&&g_spatial.rcas_retry_after==122);
    // An allocation failure keeps native presentation alive, with native FG input.
    force_fail=1;real(4,1);assert(fg_image==11&&fg_input.width==1920&&fg_input.height==1080&&observed[1].image==11&&observed[1].flip);
    assert(!cap.reconstruction_submitted&&!cap.actual_w&&!cap.actual_h&&cap.final_source_w==1920);
    assert(!cap.present_accepted&&cap.generated_accepted==1);
    force_fail=0;configured=1080;real(5,1);assert(rec_target.width==1920&&observed[1].image==11);
    // Handheld 720p input still reconstructs to explicit 1440p; LSFG disabled.
    src.w=1280;src.h=720;configured=1440;fg_on=0;real(6,0);
    assert(rec_target.width==2560&&rec_target.height==1440&&present_calls==1&&observed[0].w==2560&&observed[0].display.width==1920);
    assert(reset_flags&(DM_RECON_SOURCE_MODE|DM_RECON_SOURCE_GEOMETRY));
    // Off ignores the selected target, and Auto follows the actual surface.
    mode=SP_NATIVE;configured=2160;real(7,0);assert(rec_target.width==1920&&observed[0].image==11);
    mode=SP_FSR1;configured=0;display=(VkExtent2D){2560,1440};real(8,0);assert(rec_target.width==2560&&observed[0].w==2560&&observed[0].display.width==2560);
    // Invalid or overflowing explicit targets never allocate, never resize the swapchain.
    configured=INT_MAX;real(9,0);assert(!rec_target.width&&!rec_target.height&&observed[0].image==11&&observed[0].display.width==2560);
    // Docked enlargement must route each selected backend's output into LSFG.
    src.w=1920;src.h=1080;display=(VkExtent2D){1920,1080};fg_on=1;
    const int modes[]={SP_BILINEAR,SP_FSR1,SP_SGSR1,SP_SGSR2};
    for(unsigned i=0;i<sizeof modes/sizeof modes[0];++i){
        mode=modes[i];configured=2160;real(10+2*i,1);
        assert(cap.active==mode&&cap.reconstruction_submitted&&fg_image==22);
        assert(cap.present_accepted==1&&cap.generated_accepted==1);
        configured=1080;real(11+2*i,1);
        assert(cap.active==SP_NATIVE&&!cap.reconstruction_submitted&&fg_image==11);
    }
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as d:
            p=pathlib.Path(d)/'case.c';p.write_text(src);exe=p.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(ROOT/'nxnative/dm/android'),str(p),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True,cwd=d)
if __name__=='__main__':unittest.main()
