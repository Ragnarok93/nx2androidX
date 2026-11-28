// dm_framegen.h's implementation — Banana Native's vka_framegen.c over this repo's structures. The differences
// from the original are structural only: the window texture arrives as the replay's own vkimg_t (no registry
// acquire — the one queue's submission order and the 60-present eviction window keep it alive through the
// chain's read, and a destroy would be deferred past the GPU anyway); the submissions carry no timeline wait
// (same queue, same order); the generated frames are plain images the present thread blits itself. GPL headers
// and the status vocabulary stay Banana's.
#include "dm_framegen.h"
#include "gfx_vk_fg_flow.h"
#include "gfx_vk_fg_timing.h"
#include "gfx_vk_fg_governor.h"
#include "vka.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef NX_LSFG
#include "lsfg/lsfg.h"
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
    if ((e = getenv("DM_LSFG_FLOW"))) atomic_store(&g_flow, atoi(e));
    e = getenv("DM_LSFG");
    atomic_compare_exchange_strong(&g_on, &exp, e && atoi(e) ? 1 : 0);
}

// A freshly imported user-supplied DLL can retry a previously unavailable
// frame generation setup. The present thread owns the actual reset.
void dm_fg_retry(void) { atomic_store(&g_reprobe, 1); }

void dm_fg_set(int on, int multiplier, int target_rate, int flow_pct) {
    atomic_store(&g_mult, multiplier < 2 ? 2 : multiplier > 4 ? 4 : multiplier);
    atomic_store(&g_target, target_rate < 0 ? 0 : target_rate);
    atomic_store(&g_flow, flow_pct <= 0 ? 0 : flow_pct < 25 ? 25 : flow_pct > 100 ? 100 : flow_pct);
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

int dm_fg_status(char *buf, int n) {
    settings_init();
    int st = atomic_load(&g_state);
    if (buf && n > 0) { pthread_mutex_lock(&g_rmu); snprintf(buf, (size_t)n, "%s", g_reason); pthread_mutex_unlock(&g_rmu); }
    return st;
}

#ifndef NX_LSFG
void dm_fg_shadow_present(int real,int generated,double replay_ms,double recon_ms,uint64_t workload) {
    (void)real;(void)generated;(void)replay_ms;(void)recon_ms;(void)workload;
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
typedef struct { VkCommandBuffer cb; VkFence fence; int pending; } fg_slot;
static struct {
    int state;                         // 0 not tried, 1 ready (modules loaded), -1 unavailable
    lsfg_modules mods;
    lsfg_chain *chain; VkExtent2D ext; int flow; unsigned encoding;
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
    struct {uint64_t serial,workload; double budget,elapsed;int accepted;} shadow_rows[8];
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
static void fg_timing_invalidate(void) {
    ++F.timing_epoch;
    F.timing_latest.valid=0;
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

// Shadow only: this cannot call rebuild or modify F.flow, pacing or history.
// The available budget is an estimate from recent completed replay and
// reconstruction measurements, not a measured free GPU interval.
void dm_fg_shadow_present(int real,int generated,double replay_ms,double recon_ms,uint64_t workload) {
    const char *enabled=getenv("DM_LSFG_FLOW_SHADOW");
    if (!enabled || !atoi(enabled)) return;
    dm_fg_decision d=fg_decision;
    if (d.requested_flow || !d.active_flow || !d.submission_serial) {
        dm_fg_governor_gap(&F.shadow);return;
    }
    if (F.shadow.epoch!=F.timing_epoch || F.shadow.applied!=(unsigned)d.active_flow ||
        F.shadow_workload!=workload) {
        dm_fg_governor_reset(&F.shadow,(unsigned)d.active_flow,F.timing_epoch);
        F.shadow_workload=workload;
        memset(F.shadow_rows,0,sizeof F.shadow_rows);
    }
    // A submission serial is unique and at most NFG slots old when read.
    // Match the actual acceptance of that batch, not the current present.
    unsigned i=d.submission_serial%8;
    F.shadow_rows[i].serial=d.submission_serial;
    F.shadow_rows[i].workload=workload;
    F.shadow_rows[i].budget=replay_ms>0 && recon_ms>=0 ?
        d.period_ms-replay_ms-recon_ms-1.0 : 0;
    F.shadow_rows[i].elapsed=d.measured_ms/1000.0;
    F.shadow_rows[i].accepted=real && generated>0;
    dm_fg_timing s=d.timing;
    if (!s.valid) {dm_fg_governor_gap(&F.shadow);return;}
    unsigned j=s.serial%8;
    int matched=F.shadow_rows[j].serial==s.serial && F.shadow_rows[j].workload==workload;
    if (!matched) {dm_fg_governor_gap(&F.shadow);return;}
    // Recovery is deliberately vetoed until fresh memory/thermal evidence
    // is available; pure controller tests cover the recovery path.
    dm_fg_flow_request req=dm_fg_governor_observe(&F.shadow,&s,F.shadow_rows[j].budget,
        F.shadow_rows[j].elapsed,F.shadow_rows[j].accepted && real && generated>0,0);
    if (req.reason!=FG_FLOW_HOLD) {
        VLOG("LSFG_FLOW_SHADOW serial=%llu epoch=%llu active=%d suggested=%u reason=%u gpu_ms=%.3f estimated_budget_ms=%.3f",
            (unsigned long long)s.serial,(unsigned long long)s.epoch,d.active_flow,req.pct,req.reason,
            s.total_ms,F.shadow_rows[j].budget);
        dm_fg_governor_ack(&F.shadow,0,F.timing_epoch); // no actual transition; bound repeat reports
    }
}

static void teardown(void) {
    fg_timing_invalidate();
    if (!F.chain && !F.out[0]) return;
    pthread_mutex_lock(V.qmu); vk.vkQueueWaitIdle(V.q); pthread_mutex_unlock(V.qmu);
    lsfg_chain_free(F.chain); F.chain = NULL;
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
    F.ext = e; F.flow = flow;
    VLOG("frame generation: chain for %ux%u (effective flow %d%%) made in %.1f ms", e.width, e.height, flow, (mono_s() - t0) * 1e3);
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
        result = vk.vkWaitForFences(V.dev, 1, &slot->fence, VK_TRUE, UINT64_MAX);
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
    if (!on) {
        F.rebuild_retry_at = F.command_retry_at = 0;
        if (F.chain) { teardown(); VLOG("frame generation off"); }
        if (F.state >= 0) { atomic_store(&g_state, 0); reason("%s", "off"); }
        return fg_skip("disabled");
    }
    // Unlocked source has no fixed cadence; do not invent an LSFG hold or
    // let generated images impose a hidden rate limit. Resume on fixed mode.
    if (period == 0 && dm_perf_rate_fps() == 0) {
        if (F.chain) teardown();
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
    { int lim = dm_perf_rate_fps(); if (lim > 0 && period < 1.0 / lim) period = 1.0 / lim; }
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
        F.warm_streak = 0; atomic_store(&g_state, 2); return fg_skip("source format or crop unsupported");
    }
    VkExtent2D e = {(uint32_t)cw, (uint32_t)ch};
    flow=dm_fg_effective_flow(flow,e.width,e.height);
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
        fg_timing_invalidate();
        F.encoding = encoding;
        F.count = F.warm_streak = 0;
        if (source_reset) VLOG("frame generation: real-source discontinuity %#x", source_reset);
    }
    if (F.timing_period!=period || F.timing_mult!=mult || F.timing_target!=target) {
        fg_timing_invalidate();
        F.timing_period=period; F.timing_mult=mult; F.timing_target=target;
    }
    fg_timing_init();
    // The floor for the game's own rate (DM_LSFG_MIN_FPS, default 30): the generation's work must not push the game under it
    F.pacer.min_base = 30.0f;
    { const char *e = getenv("DM_LSFG_MIN_FPS"); if (e && atoi(e) > 0) F.pacer.min_base = (float)atoi(e); }
    // Decoupled (the pacer's other measure of the game's own rate): a game whose speed follows its measured rate keeps its speed at any rate, so a
    // slower base does not slow it down. Every game here is held by a limiter or by its own engine, so a slower base slows it too: not decoupled.
    F.pacer.decoupled = 0;
    lsfg_plan plan = lsfg_pacer_plan(&F.pacer, LSFG_MAX_GENERATIONS, mult, target, mono_s());
    uint64_t count = F.count++;
    int warm = plan.warm && count + 1 >= 2;
    F.warm_streak = warm ? F.warm_streak + 1 : 0;
    size_t ng = warm && F.warm_streak >= 2 ? plan.generations : 0;
    if (ng > LSFG_MAX_GENERATIONS) ng = LSFG_MAX_GENERATIONS;
    // Generated frames only as many as the refreshes in the game's own period leave room for (the panel shows one image a refresh; more would hold
    // the FIFO present, and the game with it, to a fraction of its rate — Banana's Honor phone note). DM_VK_FG_REFRESH_CAP=0: no cap
    uint64_t rns = getenv("DM_VK_FG_REFRESH_CAP") && !atoi(getenv("DM_VK_FG_REFRESH_CAP")) ? 0 : vk_refresh_cycle_ns(); int capped = 0;
    fg_decision.measured_ms=F.pacer.smoothed_interval*1000.0;
    fg_decision.planned=(unsigned)plan.generations;
    fg_decision.warm_streak=F.warm_streak;
    fg_decision.refresh_ns=rns;
    if (rns && period > 0) {
        // The game's frame period: the requested one (the limit's, above) or the measured one when longer (a throttled game's frames are further apart)
        double gp = F.pacer.smoothed_interval > period ? F.pacer.smoothed_interval : period;
        double n = gp * 1e9 / (double)rns;   // the refreshes in a game frame
        size_t fit = n < 1.9 ? 0 : (size_t)(n + 0.1) - 1;
        if (!fit) {
            // Keep the display vote and pacing observations, but do not copy
            // a source frame the panel cannot use for generation.
            F.count = F.warm_streak = F.pacer.issued = 0;
            ng = 0;
            capped = 1;
            goto report;
        }
        if (ng > fit) { ng = fit; capped = 1; F.pacer.issued = ng; }
    }

    unsigned slot_index=F.fi++%NFG;
    fg_slot *slot = &F.fr[slot_index];
    VkResult result = begin_slot(slot);
    if (result) goto command_error;
    VkCommandBuffer cb = slot->cb;
    fg_timing_read(slot_index); // existing begin_slot fence already retired
    if (F.timing_latest.valid) fg_decision.timing=F.timing_latest;
    uint32_t qb=slot_index*FG_QUERY_STRIDE;
    if (F.timing_queries) vkCmdResetQueryPool(cb,F.timing_queries,qb,FG_QUERY_STRIDE);
    full_barrier(cb);
    fg_timing_stamp(cb,qb);
    VkImageBlit b = {{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {{cx, cy, 0}, {cx + cw, cy + ch, 1}}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {{0, 0, 0}, {cw, ch, 1}}};
    if (flip_y) { b.srcOffsets[0].y = cy + ch; b.srcOffsets[1].y = cy; }   // lower-left origin: the window image is stored bottom-up, the chain sees it upright
    vk.vkCmdBlitImage(cb, im->img, VK_IMAGE_LAYOUT_GENERAL, lsfg_chain_input(F.chain, count), VK_IMAGE_LAYOUT_GENERAL, 1, &b, VK_FILTER_NEAREST);
    fg_timing_stamp(cb,qb+1);
    if (warm) lsfg_chain_shared_timed(F.chain,cb,count,F.timing_queries,qb+2);
    else fg_timing_stamp(cb,qb+2);
    fg_timing_stamp(cb,qb+3);
    for (size_t g = 0; g < ng; g++) {
        lsfg_chain_generate_timed(F.chain,cb,count,ng,g,F.timing_queries,qb+4+2*g);
        fg_timing_stamp(cb,qb+5+2*g);
    }
    full_barrier(cb);   // the outputs for the presents after this
    result = submit_slot(slot);
    if (result) goto command_error;
    if (F.timing_queries) F.timing_pending[slot_index]=(dm_fg_timing){
        .serial=++F.timing_serial,.epoch=F.timing_epoch,.scale=(unsigned)flow,
        .width=e.width,.height=e.height,.generated=(unsigned)ng};
    if (F.timing_queries) fg_decision.submission_serial=F.timing_serial;
    *consumer = slot->fence;
    for (size_t g = 0; g < ng; g++) gen[g] = F.out[g];
    double interval = F.pacer.smoothed_interval > 0 ? F.pacer.smoothed_interval : period;
    if (interval < period) interval = period;
    if (ng) *hold = interval / (double)(ng + 1);
    // A hold within 0.15 refresh of a whole number of refreshes is that number: the measured interval runs a little long (34 ms at a 30 fps frame step), and a hold
    // of 17 ms on a 16.7 ms panel let each image's desired time (the previous one's plus the hold) drift across a refresh every few dozen frames, two images in one
    // refresh
    if (ng && rns) { double r = *hold * 1e9 / (double)rns, w = floor(r + 0.5); if (w >= 1 && fabs(r - w) <= 0.15) *hold = w * (double)rns / 1e9; }
report:;
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
    fg_decision.reason=ng?"generated":capped?"refresh cap":met?"target met":
        !plan.warm?"pacer not warm or floor cooldown":!warm||F.warm_streak<2?"history warmup":"pacer issued zero";
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
