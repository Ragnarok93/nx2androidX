// dm_framegen.h's implementation — Banana Native's vka_framegen.c over this repo's structures. The differences
// from the original are structural only: the window texture arrives as the replay's own vkimg_t (no registry
// acquire — the one queue's submission order and the 60-present eviction window keep it alive through the
// chain's read, and a destroy would be deferred past the GPU anyway); the submissions carry no timeline wait
// (same queue, same order); the generated frames are plain images the present thread blits itself. GPL headers
// and the status vocabulary stay Banana's.
#include "dm_framegen.h"
#include "gfx_vk_fg_flow.h"
#include "gfx_vk_fg_cadence.h"
#include "gfx_vk_present_admission.h"
#include "gfx_vk_fg_timing.h"
#include "gfx_vk_fg_governor.h"
#include "gfx_vk_fg_warmup.h"
#include "gfx_vk_fg_coordination.h"
#include "gfx_vk_source_continuity.h"
#include "gfx_vk_capture_scale.h"
#include "vka.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef NX_LSFG
#include "lsfg/lsfg.h"
#include "lsfg/lsfg_graph_owner.h"
#include "lsfg/lsfg_graph_measure_job.h"
#endif

static _Atomic int g_on = -1, g_mult = 2, g_target, g_flow = 75, g_state, g_reprobe;   // g_state: 0 off, 1 generating, 2 on and not generating, -1 unavailable
static char g_reason[256] = "off";
static _Thread_local dm_fg_decision fg_decision;
dm_fg_decision dm_fg_last_decision(void) { return fg_decision; }
static int fg_skip(const char *why) { fg_decision.reason=why; return 0; }
static pthread_mutex_t g_rmu = PTHREAD_MUTEX_INITIALIZER;
// The host's frame rate limit (the adaptive rate, perf_android.c): the rate's own strong definition wins this weak one; 0 = no limit here
__attribute__((weak)) int dm_perf_rate_fps(void) { return 0; }
static void reason(const char *fmt, const char *a) { pthread_mutex_lock(&g_rmu); snprintf(g_reason, sizeof g_reason, fmt, a); pthread_mutex_unlock(&g_rmu); }

static void settings_init(void) {
    int exp = -1;
    if (atomic_load(&g_on) != -1) return;
    const char *e;
    if ((e = getenv("DM_LSFG_MULT"))) atomic_store(&g_mult, atoi(e));
    if ((e = getenv("DM_LSFG_TARGET"))) atomic_store(&g_target, atoi(e));
    if ((e = getenv("DM_LSFG_FLOW"))) atomic_store(&g_flow, dm_fg_flow_normalize(atoi(e)));
    e = getenv("DM_LSFG");
    atomic_compare_exchange_strong(&g_on, &exp, e && atoi(e) ? 1 : 0);
}

// A freshly imported user-supplied DLL can retry a previously unavailable
// frame generation setup. The present thread owns the actual reset.
void dm_fg_retry(void) { atomic_store(&g_reprobe, 1); }

void dm_fg_set(int on, int multiplier, int target_rate, int flow_pct) {
    atomic_store(&g_mult, multiplier < 2 ? 2 : multiplier > 4 ? 4 : multiplier);
    atomic_store(&g_target, target_rate < 0 ? 0 : target_rate);
    atomic_store(&g_flow,dm_fg_flow_normalize(flow_pct));
    atomic_store(&g_on, on ? 1 : 0);
    VLOG("frame generation: %s, x%d, target %d fps, flow %d%%", on ? "on" : "off", atomic_load(&g_mult), atomic_load(&g_target), atomic_load(&g_flow));
}

int dm_fg_wants_input(void) {
#ifdef NX_LSFG
    settings_init();
    return atomic_load(&g_on) > 0 &&
           (atomic_load(&g_state) >= 0 || atomic_load(&g_reprobe));
#else
    return 0;
#endif
}

// Read-only cross-thread opt-in check; no settings initialization or graph
// access from replay. Fixed/manual/disabled operation preserves old policy.
int dm_fg_adaptive_auto_selected(void) {
#ifdef NX_LSFG
    const char *e=getenv("DM_LSFG_ADAPTIVE_AUTO");
    return atomic_load(&g_on)>0 && dm_fg_flow_is_adaptive(atomic_load(&g_flow),e&&atoi(e));
#else
    return 0;
#endif
}
int dm_fg_adaptive_auto_active(void) {
    return dm_fg_adaptive_auto_selected() && atomic_load(&g_state)>0;
}

int dm_fg_status(char *buf, int n) {
    settings_init();
    int st = atomic_load(&g_state);
    if (buf && n > 0) { pthread_mutex_lock(&g_rmu); snprintf(buf, (size_t)n, "%s", g_reason); pthread_mutex_unlock(&g_rmu); }
    return st;
}

#ifndef NX_LSFG
void dm_fg_prepare_workload(uint64_t key,uint64_t ns,int blocked){(void)key;(void)ns;(void)blocked;}
void dm_fg_shadow_present(int real,int generated,uint64_t ticket,uint64_t workload) {
    (void)real;(void)generated;(void)ticket;
    fg_decision.governor=(dm_fg_governor_report){.status="not compiled",.workload=workload};
}
int dm_fg_frame(vkimg_t *im, const int *crop, int flip_y, unsigned encoding, unsigned source_reset,
                double period, VkImage gen[3], double *hold, double *out_period, VkFence *consumer) {
    (void)im; (void)crop; (void)period; (void)gen; (void)flip_y; (void)encoding; (void)source_reset;
    *consumer = VK_NULL_HANDLE;
    settings_init(); *hold = 0; *out_period = 0;
    fg_decision=(dm_fg_decision){.requested_flow=atomic_load(&g_flow)};
    if (atomic_load(&g_on) > 0) { atomic_store(&g_state, -1); reason("%s", "not in this build (built without NX_LSFG)"); }
    return fg_skip(atomic_load(&g_on)>0?"not compiled":"disabled");
}
#else
enum { NFG = 3, FG_QUERY_STRIDE = 10 };
enum { FG_COLD_DRAIN=4, FG_COLD_BUILD, FG_COLD_RETRY };
typedef struct { VkCommandBuffer cb; VkFence fence; int pending; } fg_slot;
static struct {
    int state;                         // 0 not tried, 1 ready (modules loaded), -1 unavailable
    lsfg_modules mods;
    lsfg_chain *chain; VkExtent2D ext; int flow; unsigned encoding;
    lsfg_graph_owner graph;
    lsfg_graph_job cold_job;
    lsfg_graph_measure_job upward_measure;
    int upward_seed;
    double upward_deadline;
    uint64_t active_published_ns;
    struct {
        unsigned phase,scale,failures,prior_scale,restoring,upward;
        uint64_t limit,epoch,prior_limit;
        double retry_at;
        lsfg_graph_budget reservation,prior_reservation;
    } cold;
    int cold_init;
    uint64_t graph_ticket;
    int auto_applied,requested_flow;
    int seed_policy;
    double graph_deadline;
    VkExtent2D failed_extent; int failed_flow; double rebuild_retry_at;
    VkImage out[LSFG_MAX_GENERATIONS]; VkImageView out_view[LSFG_MAX_GENERATIONS]; VkDeviceMemory out_mem[LSFG_MAX_GENERATIONS];
    VkCommandPool pool;
    fg_slot fr[NFG];
    VkQueryPool timing_queries;
    unsigned timing_tried,timing_bits;
    double timestamp_ns,timing_period;
    int timing_mult,timing_target;
    uint64_t timing_epoch,timing_serial;
    dm_fg_timing timing_pending[NFG],timing_latest;
    dm_fg_governor shadow;
    uint64_t shadow_workload;
    dm_fg_coordination coordination;
    uint64_t coordination_ns, source_ns;
    int fixed_base;
    struct {uint64_t serial,workload,ticket; double elapsed;int accepted;} shadow_rows[8];
    double command_retry_at;
    unsigned fi;
    lsfg_pacer pacer; uint64_t count; uint32_t warm_streak;
    double out_period, longer_since;   // the output period the window's frame rate is set for, and since when a longer one is wanted
} F;

static double mono_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static void unavailable(const char *why, const char *a) { F.state = -1; reason(why, a); atomic_store(&g_state, -1); VRED("frame generation unavailable: %s", g_reason); }

// The device must have what the shaders use (Float16, StorageImageExtendedFormats, StorageImageWriteWithoutFormat), null descriptors, and storage images of the chain's formats
static int device_ok(void) {
    VkPhysicalDeviceVulkan12Features f12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceFeatures2 f2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &f12};
    vk.vkGetPhysicalDeviceFeatures2(V.pd, &f2);
    const char *miss = !f12.shaderFloat16 ? "shaderFloat16" : !f2.features.shaderStorageImageExtendedFormats ? "shaderStorageImageExtendedFormats"
                     : !f2.features.shaderStorageImageWriteWithoutFormat ? "shaderStorageImageWriteWithoutFormat" : !V.null_desc ? "null descriptors (robustness2)" : NULL;
    VkFormat fmts[3] = {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8_UNORM, VK_FORMAT_R16G16B16A16_SFLOAT};
    for (int i = 0; i < 3 && !miss; i++) {
        VkFormatProperties fp; vk.vkGetPhysicalDeviceFormatProperties(V.pd, fmts[i], &fp);
        if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) || !(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) miss = "storage images of R8, RGBA8 or RGBA16F";
    }
    if (miss) { unavailable("the GPU lacks %s", miss); return 0; }
    return 1;
}

static int load(void) {
    if (F.state) return F.state > 0;
    if (!device_ok()) return 0;
    const char *path = getenv("DM_LSFG_DLL");
    if (!path || !*path) { unavailable("%s", "no Lossless.dll (the app looks in Documents/Emulation/bios, Switch and Download, or pick one on the settings page)"); return 0; }
    double t0 = mono_s();
    int r = lsfg_dll_load(path, &F.mods);
    if (r) { char b[200]; snprintf(b, sizeof b, "%s: %s", path, lsfg_status_name(r)); unavailable("%s", b); return 0; }
    VLOG("frame generation: %d shaders from %s (%.1f ms)", F.mods.n, path, (mono_s() - t0) * 1e3);
    F.state = 1;
    return 1;
}

// Timing state is present-thread owned, with process-lifetime query pool like
// the existing FG command pool. Old epoch queries are retired but ignored.
static int fg_live_enabled(void) { return dm_fg_adaptive_auto_selected(); }
// A refresh-capped or invalid source may never reuse a command slot.
// Collect only signalled private graph users, without waiting or resetting
// any fence. The normal slot path still owns query retirement/reset.
static void fg_collect_retired(void) {
    if(F.graph.phase==LSFG_GRAPH_BUILDING &&
        atomic_load_explicit(&F.graph.job.cancelled,memory_order_acquire))
        (void)lsfg_graph_owner_poll(&F.graph,F.timing_epoch);
    if(F.graph.phase!=LSFG_GRAPH_RETIRING)return;
    PFN_vkGetFenceStatus status=(PFN_vkGetFenceStatus)vk.vkGetDeviceProcAddr(V.dev,"vkGetFenceStatus");
    if(!status)return;
    for(unsigned i=0;i<NFG;i++){
        if(!(F.graph.extra_slots&(1u<<i)))continue;
        fg_slot *slot=&F.fr[i];
        if(slot->pending){
            if(status(V.dev,slot->fence)!=VK_SUCCESS)continue;
            vk_fg_fence_retired(slot->fence);
            slot->pending=0;
        }
        lsfg_graph_owner_retire_slot(&F.graph,i);
    }
}
static void fg_cancel_request(void) {
    lsfg_graph_measure_cancel(&F.upward_measure);
    lsfg_graph_owner_cancel(&F.graph);
    if(F.shadow.pending) dm_fg_governor_ack(&F.shadow,0,F.timing_epoch);
    F.seed_policy=FG_SEED_DEFER;
    fg_collect_retired();
}
static void fg_timing_invalidate(void) {
    ++F.timing_epoch;
    F.timing_latest.valid=0;
    fg_cancel_request();
}

void dm_fg_prepare_workload(uint64_t key,uint64_t ns,int blocked) {
    // This source clock is required for fixed and manual modes too. Never
    // sample the time the worker happens to reach the generation stage.
    F.source_ns=ns;
    if(!fg_live_enabled())return;
    F.coordination_ns=ns;
    if(dm_fg_coordination_update(&F.coordination,key,ns,blocked))fg_timing_invalidate();
    // An upward graph must not commit when competing work is recovering
    // or the source is already stressed. Downward relief remains eligible.
    if(F.upward_measure.running && !dm_fg_coordination_recover(&F.coordination,ns))
        fg_cancel_request();
    if(F.graph.phase==LSFG_GRAPH_BUILDING || F.graph.phase==LSFG_GRAPH_WARMING)
        if(F.graph.scale>(unsigned)F.flow &&
            !dm_fg_coordination_recover(&F.coordination,ns))fg_cancel_request();
}

static void fg_timing_init(void) {
    if (F.timing_tried) return;
    F.timing_tried=1;
    if (!vkCreateQueryPool || !vkCmdResetQueryPool || !vkCmdWriteTimestamp ||
        !vkGetQueryPoolResults || !vkGetPhysicalDeviceQueueFamilyProperties ||
        !vkGetPhysicalDeviceProperties) return;
    uint32_t n=0;
    vkGetPhysicalDeviceQueueFamilyProperties(V.pd,&n,NULL);
    if (V.qf>=n) return;
    VkQueueFamilyProperties *q=calloc(n,sizeof *q);
    if (!q) return;
    vkGetPhysicalDeviceQueueFamilyProperties(V.pd,&n,q);
    if (V.qf<n) F.timing_bits=q[V.qf].timestampValidBits;
    free(q);
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(V.pd,&props);
    F.timestamp_ns=props.limits.timestampPeriod;
    if (!F.timing_bits || F.timing_bits>64 || !(F.timestamp_ns>0) || !isfinite(F.timestamp_ns)) return;
    VkQueryPoolCreateInfo ci={VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,NULL,0,
        VK_QUERY_TYPE_TIMESTAMP,NFG*FG_QUERY_STRIDE};
    if (vkCreateQueryPool(V.dev,&ci,NULL,&F.timing_queries)!=VK_SUCCESS)
        F.timing_queries=VK_NULL_HANDLE;
}
static void fg_timing_read(unsigned slot) {
    dm_fg_timing s=F.timing_pending[slot];
    F.timing_pending[slot].serial=0;
    F.timing_latest.valid=0; // unavailable is not a fresh sample
    if (!F.timing_queries || !s.serial || s.epoch!=F.timing_epoch) return;
    uint64_t stamp[FG_QUERY_STRIDE];
    unsigned n=4+2*s.generated;
    if (vkGetQueryPoolResults(V.dev,F.timing_queries,slot*FG_QUERY_STRIDE,n,
            n*sizeof stamp[0],stamp,sizeof stamp[0],VK_QUERY_RESULT_64_BIT)!=VK_SUCCESS) return;
    if (!dm_fg_timing_decode(&s,stamp,n,F.timing_bits,F.timestamp_ns)) return;
    F.timing_latest=s;
    const char *trace=getenv("DM_LSFG_TIMING_TRACE");
    if ((trace && atoi(trace)) || s.serial%120==1) {
        VLOG("LSFG_TIMING serial=%llu epoch=%llu scale=%u input=%ux%u generated=%u copy_ms=%.3f mip_ms=%.3f shared_ms=%.3f refine_ms=%.3f output_ms=%.3f total_ms=%.3f",
            (unsigned long long)s.serial,(unsigned long long)s.epoch,s.scale,s.width,s.height,s.generated,
            s.copy_ms,s.mip_ms,s.shared_ms,s.refine_ms,s.output_ms,s.total_ms);
    }
}
static void fg_timing_stamp(VkCommandBuffer cb,uint32_t index) {
    if (F.timing_queries) vkCmdWriteTimestamp(cb,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,F.timing_queries,index);
}

static lsfg_graph_budget fg_chain_reservation(const lsfg_chain *chain) {
    lsfg_graph_budget r={.limit=UINT64_MAX};
    for(unsigned i=0;i<LSFG_GRAPH_MAX_HEAPS;i++)
        if(!lsfg_graph_charge_heap(&r,i,lsfg_chain_heap_bytes(chain,i)))return (lsfg_graph_budget){0};
    return r.used==lsfg_chain_bytes(chain)?r:(lsfg_graph_budget){0};
}
static int fg_heap_recovery_gate(double now) {
    dm_fg_heap_snapshot s=vk_fg_heap_snapshot();
    return dm_fg_heap_gate(&s,(uint64_t)(now*1e9),1);
}
static lsfg_chain *fg_build_reserved_direction(const lsfg_modules *mods,VkExtent2D ext,float scale,
        const VkImageView *outputs,const lsfg_graph_budget *req,const atomic_int *cancel,int upward) {
    dm_fg_heap_snapshot heaps=vk_fg_heap_refresh();
    if(!dm_fg_heap_reserved_fits(&heaps,(uint64_t)(mono_s()*1e9),req->heap_used,upward)) {
        VLOG("LSFG_ALLOC_REJECT origin=reservation_pressure bytes=%llu upward=%d",(unsigned long long)req->used,upward);
        return NULL;
    }
    return lsfg_chain_build_reserved(mods,ext,scale,outputs,req,cancel);
}
static lsfg_chain *fg_build_upward(const lsfg_modules *mods,VkExtent2D ext,float scale,
        const VkImageView *outputs,const lsfg_graph_budget *req,const atomic_int *cancel) {
    return fg_build_reserved_direction(mods,ext,scale,outputs,req,cancel,1);
}
static lsfg_chain *fg_build_restore(const lsfg_modules *mods,VkExtent2D ext,float scale,
        const VkImageView *outputs,const lsfg_graph_budget *req,const atomic_int *cancel) {
    return fg_build_reserved_direction(mods,ext,scale,outputs,req,cancel,0);
}
// A completed preflight is only a candidate. Keep the active graph until
// exact warm or cold admission, with a fresh pressure observation.
static void fg_upward_poll(double now) {
    if(!F.upward_measure.running)return;
    unsigned scale=F.upward_measure.scale;
    int eligible=F.chain&&fg_live_enabled()&&!F.cold.phase&&F.graph.phase==LSFG_GRAPH_EMPTY&&
        F.shadow.pending==scale&&scale>(unsigned)F.flow&&
        dm_fg_coordination_recover(&F.coordination,F.coordination_ns)&&now<=F.upward_deadline;
    if(!eligible)lsfg_graph_measure_cancel(&F.upward_measure);
    lsfg_graph_budget req;
    if(!lsfg_graph_measure_take(&F.upward_measure,F.timing_epoch,&req))return;
    if(!eligible||!req.used) {fg_cancel_request();return;}
    dm_fg_heap_snapshot heaps=vk_fg_heap_snapshot();
    uint64_t ns=(uint64_t)(now*1e9);
    dm_fg_heap_admission warm=dm_fg_heap_admit(&heaps,ns,req.heap_used,NULL,0,1);
    if(F.upward_seed&&warm.reason==DM_FG_HEAP_OK&&
        lsfg_graph_owner_start_reserved(&F.graph,&F.mods,F.ext,scale,F.out_view,F.timing_epoch,&req,fg_build_upward)) {
        F.seed_policy=F.upward_seed;F.graph_deadline=now+10.0;
        vk_quality_recovery_hold(12000);
        VLOG("LSFG_FLOW_GRAPH upward warm admitted %u%% bytes=%llu",scale,(unsigned long long)req.used);
        return;
    }
    lsfg_graph_budget old=fg_chain_reservation(F.chain);
    dm_fg_heap_admission cold=dm_fg_heap_admit(&heaps,ns,req.heap_used,old.heap_used,F.active_published_ns,1);
    if(old.used&&cold.reason==DM_FG_HEAP_OK) {
        F.cold.phase=FG_COLD_DRAIN;F.cold.scale=scale;F.cold.upward=1;
        F.cold.limit=req.used;F.cold.epoch=F.timing_epoch;F.cold.reservation=req;
        vk_quality_recovery_hold(12000);
        VLOG("LSFG_FLOW_GRAPH upward cold admitted %u%% required=%llu growth=%llu",
            scale,(unsigned long long)req.used,(unsigned long long)cold.growth);
        return;
    }
    VLOG("LSFG_FLOW_GRAPH upward refused warm=%u cold=%u required=%llu",warm.reason,cold.reason,(unsigned long long)req.used);
    fg_cancel_request();
}

// Shadow or explicit experimental Auto. Decisions never rebuild the active chain.
// The available budget is an estimate from recent completed replay and
// reconstruction measurements, not a measured free GPU interval.
void dm_fg_shadow_present(int real,int generated,uint64_t ticket,uint64_t workload) {
    const char *enabled=getenv("DM_LSFG_FLOW_SHADOW");
    int live=fg_live_enabled();
    dm_fg_governor_report *report=&fg_decision.governor;
    *report=(dm_fg_governor_report){.status="fixed Auto",.workload=workload,.live=live};
    // Capture automatically runs the non-mutating shadow policy, not live
    // transitions. No second debug switch or verbose log mode is needed.
    if (!live && (!enabled || !atoi(enabled)) && !vk_pass_dump_user_active()) return;
    report->pressure=vk_fg_pressure_snapshot();
    if(F.upward_measure.running) {
        report->status="upward graph preflight";report->requested_pct=F.upward_measure.scale;
        return;
    }
    if (F.cold.phase) {
        report->status=F.cold.phase==FG_COLD_DRAIN?"cold graph drain":F.cold.phase==FG_COLD_BUILD?"cold graph build":"cold graph retry";
        report->requested_pct=F.cold.scale;
        report->allocation_cap=F.cold.limit;
        return;
    }
    dm_fg_decision d=fg_decision;
    report->status=d.requested_flow>0?"manual flow":!d.active_flow?"no graph":!d.submission_serial?"no submission":!real?"real present rejected":"awaiting timing";
    if (d.requested_flow>0 || d.requested_flow!=atomic_load(&g_flow) || !d.active_flow || !d.submission_serial || !real) {
        if(live)fg_cancel_request();
        dm_fg_governor_gap(&F.shadow);return;
    }
    if (F.shadow.epoch!=F.timing_epoch || F.shadow.applied!=(unsigned)d.active_flow ||
        F.shadow_workload!=workload) {
        if(live)fg_cancel_request();
        dm_fg_governor_reset(&F.shadow,(unsigned)d.active_flow,F.timing_epoch);
        dm_fg_governor_preset(&F.shadow,d.requested_flow);
        F.shadow_workload=workload;
        memset(F.shadow_rows,0,sizeof F.shadow_rows);
    }
    // A submission serial is unique and at most NFG slots old when read.
    // Match the actual acceptance of that batch, not the current present.
    unsigned i=d.submission_serial%8;
    F.shadow_rows[i].serial=d.submission_serial;
    F.shadow_rows[i].workload=workload;
    F.shadow_rows[i].ticket=ticket;
    F.shadow_rows[i].elapsed=d.measured_ms/1000.0;
    F.shadow_rows[i].accepted=real?generated:0;
    dm_fg_timing s=d.timing;
    if (!s.valid) {dm_fg_governor_gap(&F.shadow);return;}
    unsigned j=s.serial%8;
    int matched=F.shadow_rows[j].serial==s.serial && F.shadow_rows[j].workload==workload &&
        F.shadow_rows[j].ticket && ticket>=F.shadow_rows[j].ticket && ticket-F.shadow_rows[j].ticket<=8;
    if (!matched) {report->status="unmatched timing";dm_fg_governor_gap(&F.shadow);return;}
    report->matched=1;report->matched_serial=s.serial;
    double matched_budget=vk_source_budget_ms(F.shadow_rows[j].ticket,workload);
    report->budget_ms=isfinite(matched_budget)?matched_budget:0;
    if(!isfinite(matched_budget)) {
        report->status="awaiting same-source GPU timing";
        dm_fg_governor_gap(&F.shadow);return;
    }
    // Pending and retiring graphs cannot launch a third graph. Do not let
    // transition-only workload contaminate profitability or dwell evidence.
    if(live && F.graph.phase!=LSFG_GRAPH_EMPTY){report->status="graph transition in flight";return;}
    int recover=live && dm_fg_coordination_recover(&F.coordination,F.coordination_ns)&&fg_heap_recovery_gate(mono_s());
    uint64_t recovery_cap=recover?report->pressure.up_cap:0;
    report->recovery_allowed=recover;
    int accepted=F.shadow_rows[j].accepted==(int)s.generated && s.generated;
    report->status=!accepted?"generated batch not accepted":!isfinite(matched_budget)?"budget unavailable":matched_budget<=0?"GPU budget exhausted":"hold";
    dm_fg_flow_request req=dm_fg_governor_observe(&F.shadow,&s,matched_budget,
        F.shadow_rows[j].elapsed,accepted,recover);
    report->requested_pct=req.pct;report->reason=req.reason;
    if (req.reason!=FG_FLOW_HOLD) {
        report->status=live?"request evaluated":"shadow recommendation";
        VLOG("LSFG_FLOW_SHADOW serial=%llu epoch=%llu active=%d suggested=%u reason=%u gpu_ms=%.3f estimated_budget_ms=%.3f",
            (unsigned long long)s.serial,(unsigned long long)s.epoch,d.active_flow,req.pct,req.reason,
            s.total_ms,matched_budget);
        if(live) {
            int up=req.pct>(unsigned)F.flow;
            uint64_t cap=up?recovery_cap:report->pressure.down_cap;
            int seed=dm_fg_seed_policy(&s,req.pct,matched_budget,!up);
            report->allocation_cap=cap;
            // A lower flow can replace the old graph without keeping two
            // copies resident or charging a second seed to an overloaded
            // frame. Only live downward requests with fresh safe pressure
            // may use this path. Its allocation cap is the retired graph's
            // actual private bytes, not an increase to the extra-memory cap.
            uint64_t old_bytes=lsfg_chain_bytes(F.chain);
            dm_fg_heap_snapshot heaps=vk_fg_heap_snapshot();
            for(unsigned i=0;i<heaps.count && i<DM_FG_MAX_HEAPS;i++) {
                if(!(heaps.eligible&(1u<<i)))continue;
                VLOG("LSFG_HEAP_PRESSURE sampled_ns=%llu heap=%u budget=%llu usage=%llu active_private=%llu",
                    (unsigned long long)heaps.sampled_ns,i,(unsigned long long)heaps.budget[i],
                    (unsigned long long)heaps.usage[i],(unsigned long long)lsfg_chain_heap_bytes(F.chain,i));
            }
            if(up) {
                // Measure off the render thread, even when the scalar
                // overlapping-graph cap is too small to hold a full graph.
                if(recover&&old_bytes&&vk_quality_recovery_try(12000)&&
                    lsfg_graph_measure_start(&F.upward_measure,F.ext,req.pct,F.timing_epoch,lsfg_chain_measure)) {
                    F.upward_seed=seed;F.upward_deadline=mono_s()+10.0;
                    report->status="upward graph preflight requested";
                    return;
                }
                report->status="upward preflight veto";
                dm_fg_governor_ack(&F.shadow,0,F.timing_epoch);
                return;
            }
            if (!up && req.reason==FG_FLOW_DOWN && cap && old_bytes &&
                F.graph.phase==LSFG_GRAPH_EMPTY && (!seed || cap<old_bytes)) {
                F.cold.phase=FG_COLD_DRAIN;F.cold.scale=req.pct;
                F.cold.limit=old_bytes;F.cold.epoch=F.timing_epoch;
                report->status="cold graph requested";
                report->allocation_cap=old_bytes;
                vk_quality_recovery_hold(12000);
                VLOG("LSFG_FLOW_GRAPH cold request %u%%, retiring %llu bytes, extra cap %llu",
                    req.pct,(unsigned long long)old_bytes,(unsigned long long)cap);
                return;
            }
            int admitted=cap && seed && (!up || vk_quality_recovery_try(12000));
            if(admitted && lsfg_graph_owner_start(&F.graph,&F.mods,F.ext,req.pct,F.out_view,cap,F.timing_epoch)) {
                if(!up)vk_quality_recovery_hold(12000);
                report->status="build requested";
                F.seed_policy=seed;F.graph_deadline=mono_s()+10.0;
                VLOG("LSFG_FLOW_GRAPH request %u%%, cap %llu, warmup %d",req.pct,(unsigned long long)cap,seed);
                return; // wait for publication or cancellation to acknowledge
            }
            report->status=!cap?"memory or recovery veto":!seed?"warmup budget veto":!admitted?"competing quality recovery":"build refused";
            VLOG("LSFG_FLOW_GRAPH request refused (cap %llu, warmup %d)",(unsigned long long)cap,seed);
        }
        dm_fg_governor_ack(&F.shadow,0,F.timing_epoch);
    }
}

static void teardown(void) {
    fg_timing_invalidate();
    lsfg_graph_measure_drain(&F.upward_measure);
    lsfg_graph_job_drain(&F.cold_job);
    F.active_published_ns=0;
    memset(&F.cold,0,sizeof F.cold);
    F.cold_init=0;
    if (!F.chain && !F.out[0]) return;
    pthread_mutex_lock(V.qmu); vk.vkQueueWaitIdle(V.q); pthread_mutex_unlock(V.qmu);
    lsfg_graph_owner_clear_idle(&F.graph); // private GPU users and shared outputs retired
    lsfg_chain_free(F.chain); F.chain = NULL;
    F.auto_applied=0;
    for (int i = 0; i < LSFG_MAX_GENERATIONS; i++) {
        if (F.out_view[i]) vk.vkDestroyImageView(V.dev, F.out_view[i], NULL);
        F.out_view[i] = VK_NULL_HANDLE;
        if (F.out[i]) vk.vkDestroyImage(V.dev, F.out[i], NULL);
        F.out[i] = VK_NULL_HANDLE;
        if (F.out_mem[i]) vk.vkFreeMemory(V.dev, F.out_mem[i], NULL);
        F.out_mem[i] = VK_NULL_HANDLE;
    }
    lsfg_pacer_reset(&F.pacer); F.count = 0; F.warm_streak = 0; F.out_period = 0; F.longer_since = 0;
}

// A generated frame's own image (the present blits it into the swapchain as it blits the game's)
static int out_new(VkExtent2D e, int i, VkImageView *view) {
    VkImageCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {e.width, e.height, 1}, .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
    VkResult r = vk.vkCreateImage(V.dev, &ci, NULL, &F.out[i]);
    if (r) F.out[i] = VK_NULL_HANDLE;
    VkMemoryRequirements mr = {0}; if (!r) vk.vkGetImageMemoryRequirements(V.dev, F.out[i], &mr);
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size, .memoryTypeIndex = vka_mem_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0)};
    if (!r && ai.memoryTypeIndex == UINT32_MAX) r = VK_ERROR_OUT_OF_DEVICE_MEMORY;
    if (!r) {
        r = vk.vkAllocateMemory(V.dev, &ai, NULL, &F.out_mem[i]);
        if (r) F.out_mem[i] = VK_NULL_HANDLE;
    }
    if (!r) r = vk.vkBindImageMemory(V.dev, F.out[i], F.out_mem[i], 0);
    VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = F.out[i], .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    if (!r) r = vk.vkCreateImageView(V.dev, &vi, NULL, view);
    // Rebuild's teardown must still see every successfully created image
    // and allocation, even when a later bind or view creation fails.
    if (r) { *view = VK_NULL_HANDLE; VRED("frame generation: output image %ux%u: %d", e.width, e.height, r); return 0; }
    return 1;
}

static int rebuild(VkExtent2D e, int flow) {
    teardown();
    VkImage imgs[LSFG_MAX_GENERATIONS];
    for (int i = 0; i < LSFG_MAX_GENERATIONS; i++) { if (!out_new(e, i, &F.out_view[i])) { teardown(); return 0; } imgs[i] = F.out[i]; }
    double t0 = mono_s();
    F.chain = lsfg_chain_new(&F.mods, e, flow / 100.0f, imgs, F.out_view);
    if (!F.chain) { teardown(); return 0; }
    F.ext = e; F.flow = flow; F.active_published_ns=(uint64_t)(mono_s()*1e9);
    VLOG("frame generation: chain for %ux%u (effective flow %d%%) made in %.1f ms", e.width, e.height, flow, (mono_s() - t0) * 1e3);
    return 1;
}

// Revalidate before transferring old ownership, then recheck current
// physical capacity in the worker after the old private allocations vanish.
static int fg_cold_pressure_ok(double now) {
    if(!F.cold.upward)return vk_fg_transition_cap(0)!=0;
    dm_fg_heap_snapshot heaps=vk_fg_heap_snapshot();
    uint64_t ns=(uint64_t)(now*1e9);
    if(F.cold.restoring)return dm_fg_heap_gate(&heaps,ns,0);
    if(!dm_fg_heap_gate(&heaps,ns,1)||
        !dm_fg_coordination_recover(&F.coordination,F.coordination_ns))return 0;
    if(F.cold.phase==FG_COLD_DRAIN) {
        lsfg_graph_budget old=fg_chain_reservation(F.chain);
        return old.used&&dm_fg_heap_admit(&heaps,ns,F.cold.reservation.heap_used,
            old.heap_used,F.active_published_ns,1).reason==DM_FG_HEAP_OK;
    }
    return 1;
}
static int fg_cold_start_replace(void) {
    if(F.cold.upward) {
        F.cold.prior_reservation=fg_chain_reservation(F.chain);
        if(!F.cold.prior_reservation.used)return 0;
        return lsfg_graph_job_start_reserved_replace(&F.cold_job,&F.mods,F.ext,F.cold.scale/100.0f,
            F.out_view,F.cold.epoch,F.chain,&F.cold.reservation,fg_build_upward);
    }
    return lsfg_graph_job_start_replace(&F.cold_job,&F.mods,F.ext,F.cold.scale/100.0f,
        F.out_view,F.cold.limit,F.cold.epoch,F.chain);
}
static int fg_cold_retry_start(void) {
    if(F.cold.upward&&F.cold.restoring)
        return lsfg_graph_job_start_reserved_replace(&F.cold_job,&F.mods,F.ext,F.cold.scale/100.0f,
            F.out_view,F.cold.epoch,NULL,&F.cold.prior_reservation,fg_build_restore);
    return lsfg_graph_job_start(&F.cold_job,&F.mods,F.ext,F.cold.scale/100.0f,
        F.out_view,F.cold.limit,F.cold.epoch);
}

// Same-extent cold replacement: the present thread stops submitting
// LSFG work, polls only private graph users, then hands old ownership to a
// worker. Shared generated output images remain alive for presentation.
// No queue-wide idle, overlapping private graphs, or unbounded allocation.
static int fg_cold_advance(double now) {
    if (!F.cold.phase) return 1;
    int pressure_ok=fg_cold_pressure_ok(now);
    if (F.cold.phase==FG_COLD_DRAIN) {
        // A discontinuity or newly unsafe pressure before ownership transfer
        // leaves the original graph available; no resources were discarded.
        if (F.cold.epoch!=F.timing_epoch || !pressure_ok) {
            memset(&F.cold,0,sizeof F.cold);
            fg_cancel_request();
            F.count=F.warm_streak=0;
            return 1;
        }
        for (unsigned i=0;i<NFG;i++) {
            if (!(F.graph.active_slots&(1u<<i))) continue;
            fg_slot *slot=&F.fr[i];
            if (slot->pending) {
                if (vk.vkWaitForFences(V.dev,1,&slot->fence,VK_TRUE,0)!=VK_SUCCESS) continue;
                vk_fg_fence_retired(slot->fence);
                slot->pending=0;
            }
            lsfg_graph_owner_retire_slot(&F.graph,i);
        }
        if (F.graph.active_slots) return 0;
        F.cold.prior_scale=(unsigned)F.flow;
        F.cold.prior_limit=lsfg_chain_bytes(F.chain);
        if (!fg_cold_start_replace()) {
            memset(&F.cold,0,sizeof F.cold);
            fg_cancel_request();
            F.count=F.warm_streak=0;
            return 1;
        }
        // Worker owns the fully retired chain, and frees it before build.
        F.chain=NULL;
        F.cold.phase=FG_COLD_BUILD;
        return 0;
    }
    if (F.cold.phase==FG_COLD_RETRY) {
        if (!pressure_ok || now<F.cold.retry_at) return 0;
        F.cold.epoch=F.timing_epoch;
        if (!fg_cold_retry_start()) {
            F.cold.retry_at=now+2.0;
            return 0;
        }
        F.cold.phase=FG_COLD_BUILD;
        return 0;
    }
    if (!pressure_ok || F.cold.epoch!=F.timing_epoch) lsfg_graph_job_cancel(&F.cold_job);
    lsfg_chain *replacement=NULL;
    if (!lsfg_graph_job_take(&F.cold_job,F.cold.epoch,&replacement)) return 0;
    if (!replacement) {
        // Do not repeatedly rebuild an unaffordable new graph after the old
        // one has been destroyed. Restore its last validated private budget.
        if (!F.cold.restoring && F.cold.prior_scale && F.cold.prior_limit) {
            VLOG("LSFG_FLOW_GRAPH cold replacement failed at %u%%; restoring %u%%",
                F.cold.scale,F.cold.prior_scale);
            F.cold.scale=F.cold.prior_scale;
            F.cold.limit=F.cold.prior_limit;
            F.cold.restoring=1;
        }
        unsigned shift=F.cold.failures<4?F.cold.failures++:4;
        F.cold.retry_at=now+(double)(2u<<shift);
        F.cold.phase=FG_COLD_RETRY;
        VLOG("LSFG_FLOW_GRAPH cold build deferred; real frames continue");
        return 0;
    }
    F.chain=replacement;
    F.active_published_ns=(uint64_t)(now*1e9);
    F.flow=(int)F.cold.scale;
    F.auto_applied=1;
    F.cold_init=1;
    F.count=F.warm_streak=F.pacer.issued=0;
    ++F.timing_epoch;
    F.timing_latest.valid=0;
    memset(F.shadow_rows,0,sizeof F.shadow_rows);
    // No warm history or comparable overlapped probe survives this gap.
    dm_fg_governor_reset(&F.shadow,(unsigned)F.flow,F.timing_epoch);
    dm_fg_governor_preset(&F.shadow,F.requested_flow);
    F.shadow.backoff=2.0;
    if (F.cold.restoring) {
        F.shadow.failed_requests=F.cold.failures?F.cold.failures:1;
        unsigned shift=F.shadow.failed_requests<4?F.shadow.failed_requests:4;
        F.shadow.backoff=(double)(2u<<shift);
        VLOG("LSFG_FLOW_GRAPH restored last valid scale %d%%",F.flow);
    }
    memset(&F.cold,0,sizeof F.cold);
    VLOG("LSFG_FLOW_GRAPH cold activated %d%%, private bytes %llu",F.flow,
        (unsigned long long)lsfg_chain_bytes(F.chain));
    return 1;
}

static void full_barrier(VkCommandBuffer cb) {   // the game's writes to the window texture, and the presents still reading the outputs (earlier submissions)
    VkMemoryBarrier mb = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, NULL, VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT, VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT};
    vk.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
}

static VkResult begin_slot(fg_slot *slot) {
    VkResult result;
    if (!F.pool) {
        VkCommandPoolCreateInfo pi = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = V.qf};
        result = vk.vkCreateCommandPool(V.dev, &pi, NULL, &F.pool);
        if (result) { F.pool = VK_NULL_HANDLE; return result; }
    }
    if (!slot->cb) {
        VkCommandBufferAllocateInfo ai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = F.pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
        result = vk.vkAllocateCommandBuffers(V.dev, &ai, &slot->cb);
        if (result) { slot->cb = VK_NULL_HANDLE; return result; }
    }
    if (!slot->fence) {
        VkFenceCreateInfo fi = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        result = vk.vkCreateFence(V.dev, &fi, NULL, &slot->fence);
        if (result) { slot->fence = VK_NULL_HANDLE; return result; }
    }
    if (slot->pending) {
        // Generation is optional: do not wait for a previous FG submission.
        result = vk.vkWaitForFences(V.dev, 1, &slot->fence, VK_TRUE, 0);
        if (result) return result;
        vk_fg_fence_retired(slot->fence);
        slot->pending = 0;
    }
    result = vk.vkResetCommandBuffer(slot->cb, 0);
    if (result) return result;
    VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    return vk.vkBeginCommandBuffer(slot->cb, &bi);
}

static VkResult submit_slot(fg_slot *slot) {
    VkCommandBuffer cb = slot->cb;
    VkResult result = vk.vkEndCommandBuffer(cb);
    if (result) return result;
    result = vk.vkResetFences(V.dev, 1, &slot->fence);
    if (result) return result;
    VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 0, NULL, NULL, 1, &cb};
    pthread_mutex_lock(V.qmu);
    result = vk.vkQueueSubmit(V.q, 1, &si, slot->fence);
    pthread_mutex_unlock(V.qmu);
    if (result) return result;
    slot->pending = 1;
    return VK_SUCCESS;
}

int dm_fg_frame(vkimg_t *im, const int *crop, int flip_y, unsigned encoding, unsigned source_reset,
                double period, VkImage gen[3], double *hold, double *out_period, VkFence *consumer) {
    settings_init();
    *hold = 0; *out_period = 0; *consumer = VK_NULL_HANDLE;
    fg_decision=(dm_fg_decision){.period_ms=period*1000.0};
    int on = atomic_load(&g_on) > 0, mult = atomic_load(&g_mult), target = atomic_load(&g_target), flow = atomic_load(&g_flow);
    fg_decision.requested_flow=flow;
    uint64_t graph_ticket=++F.graph_ticket;
    if (source_reset & DM_RECON_SOURCE_PAUSE) {
        // Do this even if a cold transition or allocation retry defers work.
        lsfg_pacer_resume(&F.pacer, (double)F.source_ns / 1e9);
        F.count = F.warm_streak = 0;
        F.longer_since = 0;
    }
    fg_collect_retired();
    if(F.requested_flow!=flow){
        if (F.cold.phase) teardown();
        fg_timing_invalidate();F.auto_applied=0;F.requested_flow=flow;
    }
    if (flow>0 || !fg_live_enabled()) { F.auto_applied=0; fg_cancel_request(); }
    if (!on) {
        F.rebuild_retry_at = F.command_retry_at = 0;
        if (F.chain || F.cold.phase || F.out[0]) { teardown(); VLOG("frame generation off"); }
        if (F.state >= 0) { atomic_store(&g_state, 0); reason("%s", "off"); }
        return fg_skip("disabled");
    }
    // Unlocked source has no fixed cadence; do not invent an LSFG hold or
    // let generated images impose a hidden rate limit. Resume on fixed mode.
    if (period == 0 && dm_perf_rate_fps() == 0) {
        if (F.chain || F.cold.phase || F.out[0]) teardown();
        atomic_store(&g_state, 2);
        reason("%s", "uncapped source rate; select a fixed FPS mode for frame generation");
        return fg_skip("uncapped source");
    }
    if (!im || !im->img || im->layout != VK_IMAGE_LAYOUT_GENERAL) {
        fg_timing_invalidate();
        F.count = F.warm_streak = 0;
        return fg_skip(!im?"no reconstructed resolve":!im->img?"no source image":"source layout not general");
    }
    if (atomic_exchange(&g_reprobe, 0) && F.state < 0) {
        F.state = 0;
        atomic_store(&g_state, 0);
        reason("%s", "retrying user-selected Lossless.dll");
    }
    if (!load()) return fg_skip("shader or device unavailable");
    // The game's frame period is never shorter than the host's own frame limit allows (the adaptive rate, perf_android.c): a game that asks for interval 1
    // (16.7 ms) but is held to 30 fps has two refreshes of a 60 Hz panel a frame, room for one generated frame, and its output rate is 60, not 120
    extern int dm_graphics_target_fps(void);
    int configured_base = dm_graphics_target_fps();
    // period belongs to this source ticket. A queued ticket from a previous
    // setting must not be retroactively assigned the new explicit target.
    int fixed_base = (configured_base==30 || configured_base==40 || configured_base==60) &&
        fabs(period*configured_base-1.0)<0.01 ? configured_base : 0;
    if (!fixed_base && configured_base==0) {
        int lim = dm_perf_rate_fps(); if (lim > 0 && period < 1.0 / lim) period = 1.0 / lim;
    }
    // The window's frame rate: a shorter output period at once, a longer one only after it held for 2 s (a game that switches its own present
    // interval would flip the panel's mode back and forth; the generated frames are paced by hold, not by the panel's rate)
    double want = target > 0 ? 1.0 / target : period / (mult < 2 ? 2 : mult > 4 ? 4 : mult), tnow = mono_s();
    fg_decision.period_ms=period*1000.0;
    if (tnow < F.command_retry_at) return fg_skip("command retry cooldown");
    if (F.out_period <= 0 || want < F.out_period - 1e-4) { F.out_period = want; F.longer_since = 0; }
    else if (want > F.out_period + 1e-4) { if (!F.longer_since) F.longer_since = tnow; else if (tnow - F.longer_since >= 2.0) { F.out_period = want; F.longer_since = 0; } }
    else F.longer_since = 0;
    *out_period = F.out_period;
    int cx = 0, cy = 0, cw = (int)im->w, ch = (int)im->h;
    if (crop && crop[2] > 0 && crop[3] > 0) { cx = crop[0]; cy = crop[1]; cw = crop[2]; ch = crop[3]; }
    VkFormatProperties fp = {0}; vk.vkGetPhysicalDeviceFormatProperties(V.pd, im->fmt, &fp);
    if (!im->img || !(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) || cw <= 0 || ch <= 0) {
        VKRED_ONCE(96, "frame generation: the window texture (format %d) cannot be copied into the chain: not generating", im->fmt);
        fg_cancel_request();
        F.warm_streak = 0; atomic_store(&g_state, 2); return fg_skip("source format or crop unsupported");
    }
    VkExtent2D e = {(uint32_t)cw, (uint32_t)ch};
    flow=dm_fg_effective_flow(flow,e.width,e.height);
    if (F.cold.phase && (!fg_live_enabled() || F.ext.width!=e.width || F.ext.height!=e.height)) {
        teardown();
        *out_period=want;
    }
    if(F.cold.phase&&(source_reset||encoding!=F.encoding||F.timing_period!=period||
            F.timing_mult!=mult||F.timing_target!=target)) {
        fg_timing_invalidate();
        F.encoding=encoding;F.timing_period=period;F.timing_mult=mult;F.timing_target=target;
    }
    if (F.cold.phase && !fg_cold_advance(tnow)) {
        F.count=F.warm_streak=F.pacer.issued=0;
        fg_decision.active_flow=F.chain?F.flow:0;
        fg_decision.graph_phase=F.cold.phase;
        fg_decision.pending_flow=F.cold.scale;
        fg_decision.active_bytes=lsfg_chain_bytes(F.chain);
        fg_decision.build_limit=F.cold.limit;
        atomic_store(&g_state,2);
        reason("%s","adaptive graph replacement; real frames only");
        return fg_skip("adaptive cold graph replacement");
    }
    if (fg_decision.requested_flow<=0 && F.auto_applied && F.chain &&
        F.ext.width==e.width && F.ext.height==e.height) flow=F.flow;
    if (!F.chain || F.ext.width != e.width || F.ext.height != e.height || F.flow != flow) {
        // A larger source can exhaust allocations without making the loaded
        // LSFG shaders unavailable. Retry a different geometry immediately;
        // bound same-geometry retries to avoid an allocation storm.
        if (tnow < F.rebuild_retry_at && e.width == F.failed_extent.width &&
            e.height == F.failed_extent.height && flow == F.failed_flow) return fg_skip("allocation retry cooldown");
        if (!rebuild(e, flow)) {
            F.failed_extent = e; F.failed_flow = flow;
            F.rebuild_retry_at = tnow + 2.0;
            atomic_store(&g_state, 2);
            reason("%s", "frame generation chain allocation unavailable; retrying");
            return fg_skip("chain allocation failed");
        }
        F.rebuild_retry_at = 0;
    }
    fg_decision.active_flow=F.flow;
    if (source_reset || encoding != F.encoding) {
        // A same-workload lifecycle break invalidates history and timing
        // serials, not known allocation failures or applied flow scale.
        unsigned lifecycle_only = DM_RECON_SOURCE_PAUSE | DM_RECON_SOURCE_GAP;
        int preserve = (source_reset & DM_RECON_SOURCE_PAUSE) &&
            !(source_reset & ~lifecycle_only) && encoding == F.encoding &&
            F.shadow.epoch == F.timing_epoch && F.shadow.applied == (unsigned)F.flow &&
            F.timing_period == period && F.timing_mult == mult && F.timing_target == target;
        if (preserve) {
            ++F.timing_epoch;
            F.timing_latest.valid=0;
            lsfg_graph_measure_cancel(&F.upward_measure);
            lsfg_graph_owner_cancel(&F.graph);
            F.seed_policy=FG_SEED_DEFER;
            F.shadow.pending=F.shadow.pending_reason=0;
            F.shadow.probe=0;
            F.shadow.epoch=F.timing_epoch;
            F.shadow.last=0;
            dm_fg_governor_gap(&F.shadow);
            memset(F.shadow_rows,0,sizeof F.shadow_rows);
            fg_collect_retired();
        } else fg_timing_invalidate();
        F.encoding = encoding;
        F.count = F.warm_streak = 0;
        if (source_reset) VLOG("frame generation: real-source discontinuity %#x", source_reset);
    }
    if (F.timing_period!=period || F.timing_mult!=mult || F.timing_target!=target) {
        fg_timing_invalidate();
        F.timing_period=period; F.timing_mult=mult; F.timing_target=target;
    }
    fg_timing_init();
    fg_upward_poll(tnow);
    if (F.graph.phase==LSFG_GRAPH_BUILDING || F.graph.phase==LSFG_GRAPH_WARMING) {
        if (!fg_live_enabled() || tnow>F.graph_deadline ||
            !(F.graph.scale>(unsigned)F.flow?fg_heap_recovery_gate(tnow):vk_fg_transition_cap(0)!=0))fg_cancel_request();
    }
    int graph_poll=lsfg_graph_owner_poll(&F.graph,F.timing_epoch);
    if (graph_poll<0) {
        fg_cancel_request();
        VLOG("LSFG_FLOW_GRAPH build cancelled or failed");
    } else if(graph_poll>0) F.graph_deadline=tnow+1.0;
    if (F.fixed_base != fixed_base) {
        // Rate changes invalidate cadence evidence, not graph ownership.
        F.fixed_base = fixed_base;
        lsfg_pacer_reset(&F.pacer);
        F.count = F.warm_streak = 0;
    }
    // Target-aware default floor; the existing explicit override remains.
    F.pacer.fixed_base = fixed_base;
    F.pacer.min_base = dm_fg_base_floor(fixed_base);
    { const char *e = getenv("DM_LSFG_MIN_FPS"); if (e && atoi(e) > 0) F.pacer.min_base = (float)atoi(e); }
    // Decoupled (the pacer's other measure of the game's own rate): a game whose speed follows its measured rate keeps its speed at any rate, so a
    // slower base does not slow it down. Every game here is held by a limiter or by its own engine, so a slower base slows it too: not decoupled.
    F.pacer.decoupled = 0;
    lsfg_plan plan = F.source_ns ?
        lsfg_pacer_plan(&F.pacer, LSFG_MAX_GENERATIONS, mult, target, (double)F.source_ns / 1e9) :
        (lsfg_plan){0, 0};
    uint64_t count = F.count++;
    int warm = plan.warm && count + 1 >= 2;
    // Beta consumes three chronological alpha entries, not just two input
    // images. Never read undefined/stale alpha after a pacing interruption.
    F.warm_streak = warm ? (F.warm_streak < LSFG_GRAPH_HISTORY_COUNT ? F.warm_streak + 1 : F.warm_streak) : 0;
    size_t ng = warm && F.warm_streak >= LSFG_GRAPH_HISTORY_COUNT ? plan.generations : 0;
    if (ng > LSFG_MAX_GENERATIONS) ng = LSFG_MAX_GENERATIONS;
    // Generated frames only as many as the refreshes in the game's own period leave room for (the panel shows one image a refresh; more would hold
    // the FIFO present, and the game with it, to a fraction of its rate — Banana's Honor phone note). DM_VK_FG_REFRESH_CAP=0: no cap
    int seed_only_generation=0;
    const char *admission_reason = NULL;
    uint64_t rns = getenv("DM_VK_FG_REFRESH_CAP") && !atoi(getenv("DM_VK_FG_REFRESH_CAP")) ? 0 : vk_refresh_cycle_ns(); int capped = 0;
    fg_decision.measured_ms=F.pacer.smoothed_interval*1000.0;
    fg_decision.planned=(unsigned)plan.generations;
    fg_decision.warm_streak=F.warm_streak;
    fg_decision.refresh_ns=rns;
    if (F.pacer.base_blocked) {
        // No optional copy/seed/submit while the explicit base is below
        // its recovery floor. Keep graph ownership, not stale history.
        F.count = F.warm_streak = F.pacer.issued = 0;
        ng = 0;
        goto report;
    }
    if (rns && period > 0) {
        // Explicit base deadlines cannot gain optional refresh slots just
        // because the presentation worker or source has fallen behind.
        double gp = dm_fg_schedule_period(period, F.pacer.smoothed_interval, fixed_base);
        double n = gp * 1e9 / (double)rns;   // the refreshes in a game frame
        size_t fit = n < 1.9 ? 0 : (size_t)(n + 0.1) - 1;
        if (!fit) {
            // Keep the display vote and pacing observations, but do not copy
            // a source frame the panel cannot use for generation.
            fg_cancel_request();
            F.count = F.warm_streak = F.pacer.issued = 0;
            ng = 0;
            capped = 1;
            goto report;
        }
        if (ng > fit) { ng = fit; capped = 1; F.pacer.issued = ng; }
    }

    // Optional work cannot recover an already-expired WSI deadline.
    if (ng) {
        double scheduled_hold = dm_fg_output_hold(period, F.pacer.smoothed_interval, fixed_base, (unsigned)ng, rns);
        uint64_t deadline = dm_present_optional_deadline(F.source_ns, scheduled_hold, (unsigned)ng);
        if (deadline && !dm_present_optional_wait((uint64_t)(mono_s()*1e9), deadline, scheduled_hold)) {
            admission_reason = "generated GPU deadline expired";
            F.count = F.warm_streak = F.pacer.issued = 0;
            ng = 0;
            goto report;
        }
    }
    unsigned slot_index=F.fi++%NFG;
    fg_slot *slot = &F.fr[slot_index];
    VkResult result = begin_slot(slot);
    if (result == VK_TIMEOUT) {
        admission_reason = "generated GPU slot busy";
        F.count = F.warm_streak = F.pacer.issued = 0;
        ng = 0;
        goto report;
    }
    if (result) goto command_error;
    VkCommandBuffer cb = slot->cb;
    lsfg_graph_owner_retire_slot(&F.graph,slot_index);
    // Only switch on a real generation opportunity with contiguous history.
    int switching=ng && fg_decision.requested_flow<=0 && fg_live_enabled() &&
        lsfg_graph_owner_ready(&F.graph,F.timing_epoch,graph_ticket);
    int seeded=!switching && warm && ng && fg_decision.requested_flow<=0 && fg_live_enabled() && F.seed_policy &&
        F.graph.phase==LSFG_GRAPH_WARMING &&
        lsfg_graph_history_can_seed(&F.graph.history,F.timing_epoch,graph_ticket);
    seed_only_generation=seeded && F.seed_policy==FG_SEED_INSTEAD_OF_GENERATION;
    if(seed_only_generation)ng=0; // reserve optional generation budget for pending history
    lsfg_chain *record_chain=switching?F.graph.extra:F.chain;
    uint64_t record_count=switching?F.graph.history.next_index:count;
    fg_timing_read(slot_index); // existing begin_slot fence already retired
    if (F.timing_latest.valid) fg_decision.timing=F.timing_latest;
    uint32_t qb=slot_index*FG_QUERY_STRIDE;
    if (F.timing_queries) vkCmdResetQueryPool(cb,F.timing_queries,qb,FG_QUERY_STRIDE);
    full_barrier(cb);
    if (F.cold_init) lsfg_chain_record_init(F.chain,cb);
    fg_timing_stamp(cb,qb);
    VkImageBlit b = {{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {{cx, cy, 0}, {cx + cw, cy + ch, 1}}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {{0, 0, 0}, {cw, ch, 1}}};
    if (flip_y) { b.srcOffsets[0].y = cy + ch; b.srcOffsets[1].y = cy; }   // lower-left origin: the window image is stored bottom-up, the chain sees it upright
    vk.vkCmdBlitImage(cb, im->img, VK_IMAGE_LAYOUT_GENERAL, lsfg_chain_input(record_chain, record_count), VK_IMAGE_LAYOUT_GENERAL, 1, &b, VK_FILTER_NEAREST);
    fg_timing_stamp(cb,qb+1);
    if (ng) lsfg_chain_shared_timed(record_chain,cb,record_count,F.timing_queries,qb+2);
    else {
        // Seed only when generation is suppressed; do not read beta's
        // three history slots until they are all populated.
        if(warm)lsfg_chain_seed(record_chain,cb,record_count);
        fg_timing_stamp(cb,qb+2);
    }
    fg_timing_stamp(cb,qb+3);
    for (size_t g = 0; g < ng; g++) {
        lsfg_chain_generate_timed(record_chain,cb,record_count,ng,g,F.timing_queries,qb+4+2*g);
        fg_timing_stamp(cb,qb+5+2*g);
    }
    if (seeded) {
        // The same submission owns both reads of the real source; the
        // existing consumer fence therefore also covers the pending copy.
        uint64_t index=lsfg_graph_history_index(&F.graph.history,graph_ticket);
        if (!F.graph.history.initialized) lsfg_chain_record_init(F.graph.extra,cb);
        full_barrier(cb);
        vk.vkCmdBlitImage(cb,im->img,VK_IMAGE_LAYOUT_GENERAL,
            lsfg_chain_input(F.graph.extra,index),VK_IMAGE_LAYOUT_GENERAL,1,&b,VK_FILTER_NEAREST);
        lsfg_chain_seed(F.graph.extra,cb,index);
    }
    full_barrier(cb);   // the outputs for the presents after this
    result = submit_slot(slot);
    if (result) goto command_error;
    F.cold_init=0;
    if (switching) {
        unsigned new_flow=F.graph.scale;
        // All conditions were checked before recording; only this thread
        // mutates ownership. Shared outputs are NOT retired with old graph.
        int published=lsfg_graph_owner_publish(&F.graph,&F.chain,slot_index,F.timing_epoch,graph_ticket,1);
        if (!published) abort(); // internal ownership invariant
        F.flow=(int)new_flow;flow=F.flow;F.auto_applied=1;
        F.active_published_ns=(uint64_t)(mono_s()*1e9);
        F.count=record_count+1;
        ++F.timing_epoch;
        F.timing_latest.valid=0;
        dm_fg_governor_ack(&F.shadow,1,F.timing_epoch);
        memset(F.shadow_rows,0,sizeof F.shadow_rows);
        F.seed_policy=FG_SEED_DEFER;
        fg_decision.timing=(dm_fg_timing){0};
        fg_decision.active_flow=F.flow;
        VLOG("LSFG_FLOW_GRAPH activated %d%%, private bytes %llu",F.flow,(unsigned long long)lsfg_chain_bytes(F.chain));
    } else lsfg_graph_owner_submit(&F.graph,slot_index,1,1,seeded,F.timing_epoch,graph_ticket);
    if (F.timing_queries) {
        uint64_t serial=++F.timing_serial;
        // Warmup contributes extra GPU work after the measured interval.
        // Do not allow its otherwise valid active timings into the governor.
        F.timing_pending[slot_index]=seeded?(dm_fg_timing){0}:(dm_fg_timing){
            .serial=serial,.epoch=F.timing_epoch,.scale=(unsigned)flow,
            .width=e.width,.height=e.height,.generated=(unsigned)ng};
        fg_decision.submission_serial=serial;
    }
    *consumer = slot->fence;
    for (size_t g = 0; g < ng; g++) gen[g] = F.out[g];
    if (ng) *hold = dm_fg_output_hold(period, F.pacer.smoothed_interval, fixed_base, ng, rns);
report:;
    fg_decision.graph_phase=F.graph.phase;
    fg_decision.pending_flow=(F.graph.phase==LSFG_GRAPH_BUILDING||F.graph.phase==LSFG_GRAPH_WARMING)?F.graph.scale:0;
    fg_decision.seed_policy=(unsigned)F.seed_policy;
    fg_decision.seed_count=F.graph.phase==LSFG_GRAPH_WARMING?F.graph.history.filled:0;
    fg_decision.active_bytes=lsfg_chain_bytes(F.chain);
    fg_decision.extra_bytes=lsfg_chain_bytes(F.graph.extra);
    fg_decision.build_limit=F.graph.phase==LSFG_GRAPH_BUILDING?F.graph.job.byte_limit:0;
    // The floor's unloaded baseline must reflect submitted GPU work, not
    // the pacer's theoretical count before warm-up and refresh capping.
    F.pacer.issued = ng;
    int st2 = ng ? 1 : 2;
    // The target rate is met: there is no frame to add (the game already runs at the target, or faster)
    int met = target > 0 && F.pacer.smoothed_interval > 0 && 1.0 / F.pacer.smoothed_interval >= target * 0.95;
    static int was_capped, was_met;
    if (atomic_exchange(&g_state, st2) != st2 || (st2 == 2 && capped != was_capped) || (st2 == 2 && met != was_met)) {
        char why[96]; snprintf(why, sizeof why, "on, the display runs at %.0f Hz: no refresh left for a generated frame", rns ? 1e9 / (double)rns : 0.0);
        reason("%s", st2 == 1 ? "generating" : capped ? why : met ? "on, the target rate is met: no frame to add" : "on, waiting for a steady frame rate");
        static int ncap; if (st2 == 2 && capped && ncap++ < 4) VLOG("frame generation: not generating, %s (game frame period %.1f ms)", why + 4, period * 1e3);
    }
    fg_decision.reason=admission_reason?admission_reason:ng?"generated":seed_only_generation?"adaptive flow warmup":F.pacer.base_blocked?"explicit base cadence protection":capped?"refresh cap":met?"target met":
        !plan.warm?"pacer not warm or floor cooldown":!warm||F.warm_streak<LSFG_GRAPH_HISTORY_COUNT?"history warmup":"pacer issued zero";
    was_capped = st2 == 2 && capped;
    was_met = st2 == 2 && met;
    return (int)ng;

command_error:
    fg_timing_invalidate();
    F.count = F.warm_streak = F.pacer.issued = 0;
    *out_period = 0;
    VRED("frame generation command failed: %d", result);
    if (result == VK_ERROR_DEVICE_LOST) unavailable("%s", "GPU device lost");
    else {
        F.command_retry_at = tnow + 1.0;
        atomic_store(&g_state, 2);
        reason("%s", "GPU command failed; retrying");
    }
    return fg_skip("GPU command failed");
}
#endif
