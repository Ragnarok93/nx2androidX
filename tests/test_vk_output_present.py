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
#include "gfx_vk_present_admission.h"
#include "spatial_upscaler.h"
#include "gfx_vk_menu_recon.h"
static dm_recon_menu_status g_menu_recon;
static dm_fg_menu_status g_menu_fg;
static struct {int panel;} g_ov;
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
    unsigned replay_flags;
    int flow_pressure, explicit_target, live_sources, encoding, report_physical, measured_mode, report_sharpness_requested, report_sharpness_applied, measured_history_reset;
    VkExtent2D report_input, report_allocated, measured_input, measured_output, resolve_extent;
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
static int configured=2160,mode=SP_SGSR2,force_fail,fail_resolve,fg_on=1;
static spatial_image resolve;
static uint64_t resolved_ticket;
static unsigned rec_calls,fg_calls,present_calls,capture_calls,reset_flags;
static VkExtent2D rec_target,fg_input,display={1920,1080};
static VkImage fg_image;
static int reject_generated, gen_count=1;
static unsigned fg_encoding;
static VkExtent2D g_sext={1920,1080},g_next={1920,1080};
static struct {VkImage image;int w,h,meta,flip;VkExtent2D display;uint64_t ticket,deadline;double period;} observed[4];
static vk_capture_scale_present_sample cap;
int dm_recon_output_height(void){return configured;}
int dm_spatial_mode(void){return mode;}
int dm_fsr_sharpness(void){return 50;}
int dm_perf_rate_fps(void){return 30;}
static int dm_fg_wants_input(void){return fg_on;}
static unsigned dm_menu_rs_recorded(void){return 0;}
static void dm_fg_prepare_workload(uint64_t key,uint64_t ns,int blocked){(void)key;(void)ns;(void)blocked;}
double vk_gpu_ms_recent(void){return 8.0;}
static void dm_fg_shadow_present(int real,int generated,double replay,double recon,uint64_t workload){(void)real;(void)generated;(void)replay;(void)recon;(void)workload;}
typedef struct {const char *reason;double period_ms,measured_ms;uint64_t refresh_ns;unsigned planned,warm_streak;
    int requested_flow,active_flow;uint64_t submission_serial;dm_fg_timing timing;dm_fg_governor_report governor;
    unsigned graph_phase,pending_flow,seed_policy,seed_count;
    uint64_t active_bytes,extra_bytes,build_limit;} dm_fg_decision;
static int test_flow=50;
static dm_fg_decision dm_fg_last_decision(void){return (dm_fg_decision){.reason=fg_on?"test fg":"disabled",.period_ms=33.333,.planned=fg_on?1:0,.active_flow=fg_on?test_flow:0};}
static spatial_image *spatial_display_resolved(uint64_t ticket){return resolved_ticket==ticket?&resolve:NULL;}
static int reconstruction_pressure(dm_flow_cadence cadence,uint64_t ticket){assert(cadence.period_us && ticket);return 0;}
void vk_guest_flow_feedback(uint64_t ticket,uint32_t cadence,int stressed){assert(ticket&&cadence&&!stressed);}
static void spatial_collect_sources(void){}
static VkFormat present_window_format(VkFormat f){return f;}
static void swap_destroy(void){dm_recon_source_invalidate(&g_recon_source);g_spatial.ticket=0;}
static int swap_make(void){return 1;}
static VkExtent2D picture_extent(int w,int h){VkExtent2D p;dm_recon_picture_extent(w,h,display.width,display.height,&p.width,&p.height);return p;}
static spatial_image *spatial_reconstruct(vkimg_t *im,const int *raw,int flip,uint32_t w,uint32_t h,uint64_t ticket,const vk_reconstruction_input *physical){
    (void)flip;(void)physical;rec_calls++;rec_target=(VkExtent2D){w,h};
    spatial_requested=mode;g_spatial.report_input=(VkExtent2D){(unsigned)raw[2],(unsigned)raw[3]};
    g_spatial.ticket=0;resolved_ticket=0;spatial_active=SP_NATIVE;
    if(mode==SP_NATIVE||force_fail||!w||!h||(w==(unsigned)raw[2]&&h==(unsigned)raw[3]))return NULL;
    int downscale=w<=(unsigned)raw[2]&&h<=(unsigned)raw[3];
    if(downscale&&!g_spatial.explicit_target)return NULL;
    spatial_active=downscale?SP_BILINEAR:mode;g_spatial.ticket=ticket;g_spatial.image[0].texture=(vkimg_t){w,h,im->fmt,22};
    if(g_spatial.resolve_extent.width&&!fail_resolve){
        resolved_ticket=ticket;resolve.texture=(vkimg_t){g_spatial.resolve_extent.width,g_spatial.resolve_extent.height,im->fmt,55};}
    return &g_spatial.image[0];
}
static int dm_fg_frame(vkimg_t *im,const int *rect,int flip,unsigned encoding,unsigned reset,double period,VkImage gen[3],double *hold,double *out_period,VkFence *consumer){
    (void)flip;(void)period;
    fg_encoding=encoding;
    fg_image=im?im->img:0;
    fg_calls++;fg_input=(VkExtent2D){(unsigned)rect[2],(unsigned)rect[3]};reset_flags=reset;
    if(!fg_on||!im)return 0;
    for(int i=0;i<gen_count;i++)gen[i]=33+i;
    *hold=.01;*out_period=.01;*consumer=44;return gen_count;
}
int vk_pass_dump_user_active(void){return 1;}
void vk_capture_scale_present(uint64_t ticket,vk_capture_scale_present_sample sample){assert(ticket);cap=sample;capture_calls++;}
static int present_one(vkimg_t *im,VkImage src,const int *crop,VkExtent2D picture,int flip,double period,uint64_t ticket,int meta,
                       uint64_t deadline,const char **reason,VkFence source_dependency){
    (void)im;(void)source_dependency;assert(present_calls<4);unsigned i=present_calls++;
    observed[i].image=src;observed[i].w=crop[2];observed[i].h=crop[3];observed[i].meta=meta;observed[i].display=picture;observed[i].flip=flip;observed[i].ticket=ticket;
    observed[i].period=period;observed[i].deadline=deadline;
    if(reason && reject_generated){*reason="generated acquire deadline";return 0;}
    return src != 11;
}
'''+code[start:end]+r'''
static vkimg_t src={1920,1080,37,11};
static void real(uint64_t ticket,int docked){present_calls=0;present_window(&src,NULL,1.0/30,ticket,0,ticket*33333333,docked,1,0,NULL);}
int main(void){
    real(1,1);
    assert(rec_target.width==3840&&rec_target.height==2160); // must not inherit drawable limit
    assert(rec_calls==1&&fg_calls==1&&fg_input.width==1920&&fg_input.height==1080);
    assert(present_calls==2&&observed[0].image==33&&observed[1].image==55);
    for(unsigned i=0;i<2;i++)assert(observed[i].w==1920&&observed[i].h==1080&&observed[i].display.width==1920&&observed[i].display.height==1080&&!observed[i].flip);
    assert(observed[0].ticket==0&&observed[1].ticket==1);
    assert(fg_image==55&&resolve.last_use==44&&!g_spatial.image[0].last_use);
    assert(cap.reconstruction_submitted&&cap.actual_w==3840&&cap.actual_h==2160);
    assert(cap.final_source_w==1920&&cap.final_source_h==1080);
    assert(cap.present_accepted==1&&cap.generated_accepted==1);
    assert(g_menu_recon.valid&&g_menu_recon.output_pass==SP_SGSR2&&g_menu_recon.w==3840&&g_menu_recon.h==2160&&g_ov.panel==-1);
    char status[80];dm_recon_menu_label(&g_menu_recon,1,status,sizeof status);
    assert(!strcmp(status,"Last: 3840x2160"));
    dm_fg_menu_label(&g_menu_fg,status,sizeof status);assert(!strcmp(status,"Last graph: 50%"));
    assert(cap.fg_reason && !strcmp(cap.fg_reason,"test fg") && cap.fg_planned==1);
    // A paused setting change must not generate or reconstruct the old ticket.
    configured=1440;present_calls=0;
    present_window(&src,NULL,-1,1,1,33333333,1,1,0,NULL);
    assert(rec_calls==1&&fg_calls==1&&capture_calls==1&&present_calls==1&&observed[0].w==1920&&observed[0].image==55);
    assert(g_menu_recon.w==3840&&g_menu_recon.h==2160); // paused change is not a real frame
    g_spatial.retry_after=121;g_spatial.rcas_retry_after=121;
    real(2,1);assert(!g_spatial.retry_after&&!g_spatial.rcas_retry_after);assert(rec_target.width==2560&&rec_target.height==1440&&reset_flags==DM_RECON_SOURCE_GEOMETRY);
    g_spatial.retry_after=122;g_spatial.rcas_retry_after=122;
    real(3,1);assert(!reset_flags&&g_spatial.retry_after==122&&g_spatial.rcas_retry_after==122);
    // An allocation failure keeps native presentation alive, with native FG input.
    test_flow=40;force_fail=1;real(4,1);assert(fg_image==11&&fg_input.width==1920&&fg_input.height==1080&&observed[1].image==11&&observed[1].flip);
    assert(!cap.reconstruction_submitted&&!cap.actual_w&&!cap.actual_h&&cap.final_source_w==1920);
    assert(!cap.present_accepted&&cap.generated_accepted==1);
    assert(g_menu_recon.w==2560&&g_menu_recon.h==1440); // failed real present cannot publish
    assert(g_menu_fg.scale==50);
    force_fail=0;configured=1080;real(5,1);assert(rec_target.width==1920&&observed[1].image==11);
    // Handheld 720p input still reconstructs to explicit 1440p; LSFG disabled.
    src.w=1280;src.h=720;configured=1440;fg_on=0;real(6,0);
    assert(rec_target.width==2560&&rec_target.height==1440&&present_calls==1&&observed[0].w==2560&&observed[0].display.width==1920);
    assert(reset_flags&(DM_RECON_SOURCE_MODE|DM_RECON_SOURCE_GEOMETRY));
    assert(!g_menu_fg.scale);dm_fg_menu_label(&g_menu_fg,status,sizeof status);assert(!strcmp(status,"Last: waiting"));
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
        assert(cap.active==mode&&cap.reconstruction_submitted&&fg_image==55);
        assert(cap.present_accepted==1&&cap.generated_accepted==1);
        configured=1080;real(11+2*i,1);
        assert(cap.active==SP_NATIVE&&!cap.reconstruction_submitted&&fg_image==11);
    }
    // The unresolvable frame remains renderable at its explicit output
    // target. LSFG sees a skipped source and can restart on recovery.
    configured=2160;fail_resolve=1;real(18,1);
    assert(present_calls==1&&fg_image==0&&observed[0].image==22);
    assert(cap.actual_w==3840&&cap.final_source_w==3840&&!cap.generated_accepted);
    fail_resolve=0;real(19,1);
    assert(present_calls==2&&fg_image==55&&cap.generated_accepted==1);
    configured=720;real(20,1);
    assert(cap.actual_w==1280&&cap.actual_h==720&&cap.active==SP_BILINEAR);
    assert(fg_input.width==1280&&fg_input.height==720&&observed[1].image==fg_image);
    unsigned prior_encoding=fg_encoding;
    present_calls=0;
    present_window(&src,NULL,1.0/30,21,0,21*33333333,1,1,
        SP_REPLAY_TEMPORAL|SP_REPLAY_SHARPENED,NULL);
    assert(fg_encoding!=prior_encoding);
    dm_recon_menu_label(&g_menu_recon,0,status,sizeof status);
    assert(!strcmp(status,"Last: GSR 2 + Bilinear"));
    // A rejected first optional output must skip the remaining two and
    // leave the original real output next, with the whole batch interval.
    gen_count=3;reject_generated=1;configured=2160;real(22,1);
    assert(present_calls==2&&observed[0].image==33&&observed[0].deadline);
    assert(observed[1].meta&&observed[1].ticket==22&&observed[1].period>.0399&&observed[1].period<.0401);
    assert(!cap.generated_accepted&&cap.present_accepted&&!strcmp(cap.fg_present_reason,"generated acquire deadline"));
    dm_recon_menu_status empty={0};
    dm_recon_menu_label(&empty,0,status,sizeof status);assert(!strcmp(status,"Last: waiting"));
    assert(dm_recon_menu_publish(&empty,1,SP_NATIVE,0,0,1920,1080));
    dm_recon_menu_label(&empty,1,status,sizeof status);assert(!strcmp(status,"Last: 1920x1080 native"));
    assert(!dm_recon_menu_publish(&empty,1,SP_NATIVE,0,0,1920,1080));
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as d:
            p=pathlib.Path(d)/'case.c';p.write_text(src);exe=p.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(ROOT/'nxnative/dm/android'),str(p),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True,cwd=d)
if __name__=='__main__':unittest.main()
