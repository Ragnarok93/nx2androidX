#include "gfx_vk_runtime_identity.h"
#include "gfx_vk_capture_scale.h"
#include "spatial_upscaler.h"

#ifndef DM_RUNTIME_SOURCE_COMMIT
#define DM_RUNTIME_SOURCE_COMMIT "unavailable"
#endif
#ifndef VK_CAPTURE_DRIVER_ID
#define VK_CAPTURE_DRIVER_ID (-1)
#define VK_CAPTURE_DRIVER_VERSION 0u
#endif

// The replay thread owns the Vulkan readbacks. The archive worker freezes
// publication while it reads the files. A stopped capture stays available
// for export retry without continuing to collect samples.
enum { CAPTURE_SLOTS = 3, CAPTURE_INTERVAL = 30 };
static pthread_mutex_t g_capture_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_capture_exporting;
static int g_capture_delay;
static unsigned g_capture_next_slot;
static uint64_t g_capture_next_present;
static int g_capture_rolling_dump;
static char g_capture_slot[896];
static FILE *g_capture_timeline;

// One slot per source-frame ticket. Replay writes the row before enqueueing
// presentation, which later joins its result to the same ticket.
enum { SCALE_CAPTURE_FRAMES = 4096 };
typedef struct {
    uint64_t ticket;
    vk_capture_scale_replay_sample replay;
    vk_capture_scale_present_sample present;
    int seen_present;
} scale_capture_row;
static scale_capture_row *g_scale_capture;
static uint64_t g_scale_capture_ticket, g_scale_capture_overwritten;

static void capture_scale_replay(uint64_t ticket, vk_capture_scale_replay_sample sample) {
    pthread_mutex_lock(&g_capture_lock);
    if (atomic_load(&g_pd_user_pending) && g_scale_capture && ticket) {
        scale_capture_row *row = &g_scale_capture[ticket % SCALE_CAPTURE_FRAMES];
        if (row->ticket && row->ticket != ticket) g_scale_capture_overwritten++;
        *row = (scale_capture_row){.ticket = ticket, .replay = sample};
        g_scale_capture_ticket = ticket;
    }
    pthread_mutex_unlock(&g_capture_lock);
}

void vk_capture_scale_present(uint64_t ticket, vk_capture_scale_present_sample sample) {
    pthread_mutex_lock(&g_capture_lock);
    if (atomic_load(&g_pd_user_pending) && g_scale_capture && ticket) {
        scale_capture_row *row = &g_scale_capture[ticket % SCALE_CAPTURE_FRAMES];
        if (row->ticket == ticket) {
            row->present = sample;
            row->seen_present = 1;
        }
    }
    pthread_mutex_unlock(&g_capture_lock);
}

static int capture_scale_write(const char *root) {
    if (!g_scale_capture) return 1;
    char path[1024], tmp[1024];
    int n = snprintf(path, sizeof path, "%s/scaling_frames.tsv", root);
    int m = snprintf(tmp, sizeof tmp, "%s/scaling_frames.tsv.tmp", root);
    if (n < 0 || (size_t)n >= sizeof path || m < 0 || (size_t)m >= sizeof tmp) return 0;
    FILE *out = fopen(tmp, "w");
    if (!out) return 0;
    char runtime_id[DM_RUNTIME_ID_CAP];
    dm_runtime_build_id(runtime_id);
    fprintf(out, "# runtime_git_commit=%s\n"
                 "# runtime_build_configuration=%s\n"
                 "# runtime_lsfg_compiled=%d\n"
                 "# vulkan_driver_id=%d\n"
                 "# vulkan_driver_version=%#x\n",
            DM_RUNTIME_SOURCE_COMMIT,
#ifdef DM_RELEASE
            "release",
#else
            "development",
#endif
#ifdef NX_LSFG
            1,
#else
            0,
#endif
            VK_CAPTURE_DRIVER_ID, VK_CAPTURE_DRIVER_VERSION);
    fprintf(out, "# overwritten_rows=%llu\n"
                 "# runtime_elf_build_id=%s\n"
                 "ticket\tmode\tmin_pct\tmax_pct\tlatched_pct\tscaled_passes\tpasses_total\t"
                 "largest_scaled_logical_w\tlargest_scaled_logical_h\tlargest_scaled_physical_w\t"
                 "largest_scaled_physical_h\trenderer_available\tgpu_snapshot_in_flight\t"
                 "replay_cpu_us\tsource_monotonic_us\trecent_replay_gpu_ms\t"
                 "presentation_seen\trequested_upscaler\tactive_upscaler\tphysical_candidate\t"
                 "source_w\tsource_h\toutput_w\toutput_h\tgenerated_frames\t"
                 "reconstruction_timing_ticket\treconstruction_gpu_ms\treason\tfirst_scope_rejection\trejected_w\trejected_h\tbacking_bytes\tpeak_published_backing_bytes\ttiming_backend\ttiming_iw\ttiming_ih\ttiming_ow\ttiming_oh\ttiming_budget_ms\ttiming_cadence_us\ttiming_generation\ttiming_history_epoch\ttiming_history_reset\tsharpness_requested\tsharpness_applied\tstage_input_ms\tstage_motion_ms\tstage_accumulate_ms\tstage_output_ms\tsource_enqueue_ns\tsource_reset_flags\tdocked_at_enqueue\treconstruction_alloc_attempts\treconstruction_allocations\treconstruction_alloc_waits\treconstruction_alloc_wait_ns\treconstruction_slot_wait_ns\tdisplay_w\tdisplay_h\tactual_reconstruction_w\tactual_reconstruction_h\tconfigured_output_short_edge\tsupersampling\tguest_w\tguest_h\tguest_crop_x\tguest_crop_y\tguest_crop_w\tguest_crop_h\tfinal_source_w\tfinal_source_h\tdrawable_w\tdrawable_h\tswapchain_w\tswapchain_h\treconstruction_submitted\tpresent_accepted\tgenerated_accepted\n",
            (unsigned long long)g_scale_capture_overwritten, runtime_id);
    uint64_t end = g_scale_capture_ticket;
    uint64_t first = end >= SCALE_CAPTURE_FRAMES ? end - SCALE_CAPTURE_FRAMES + 1 : 1;
    static const char *const modes[] = {"Off", "Fixed", "Adaptive"};
    for (uint64_t ticket = first; ticket <= end; ticket++) {
        const scale_capture_row *row = &g_scale_capture[ticket % SCALE_CAPTURE_FRAMES];
        if (row->ticket != ticket) continue;
        const vk_capture_scale_replay_sample *r = &row->replay;
        const vk_capture_scale_present_sample *p = &row->present;
        fprintf(out, "%llu\t%s\t%u\t%u\t%u\t%u\t%llu\t%u\t%u\t%u\t%u\t%d\t%d\t%llu\t%llu\t%.3f\t%d\t"
                "%s\t%s\t%d\t%u\t%u\t%u\t%u\t%d\t%llu\t%.3f\t%s\t%s\t%u\t%u\t%llu\t%llu\t%s\t%u\t%u\t%u\t%u\t%.3f\t%u\t%llu\t%llu\t%d\t%d\t%d\t%.3f\t%.3f\t%.3f\t%.3f\t%llu\t%u\t%d\t%u\t%u\t%u\t%llu\t%llu\t%u\t%u\t%u\t%u\t%d\t%d\t%u\t%u\t%d\t%d\t%d\t%d\t%u\t%u\t%u\t%u\t%u\t%u\t%d\t%d\t%d\n",
                (unsigned long long)ticket, r->mode < 3 ? modes[r->mode] : "-", r->min, r->max,
                r->target, r->passes, (unsigned long long)r->passes_total, r->logical_w, r->logical_h,
                r->physical_w, r->physical_h, r->renderer_available, r->gpu_snapshot,
                (unsigned long long)(r->cpu_replay_ns / 1000), (unsigned long long)(r->source_time_ns / 1000),
                r->recent_replay_gpu_ms,
                row->seen_present, row->seen_present ? spatial_mode_name(p->requested) : "-",
                row->seen_present ? spatial_mode_name(p->active) : "-", p->physical, p->source_w, p->source_h,
                p->output_w, p->output_h, p->generated, (unsigned long long)p->timing_ticket, p->gpu_ms,
                row->seen_present ? (p->reason ? p->reason : "ready") : "no presentation sample",
                r->scope_reason ? r->scope_reason : "-", r->rejected_w, r->rejected_h,
                (unsigned long long)r->backing_bytes, (unsigned long long)r->peak_published_backing_bytes,
                p->timing_ticket ? spatial_mode_name(p->timing_mode) : "-",
                p->timing_iw, p->timing_ih, p->timing_ow, p->timing_oh,
                p->timing_budget_ms, p->timing_cadence_us, (unsigned long long)p->timing_generation,
                (unsigned long long)p->timing_history_epoch, p->timing_history_reset,
                p->sharpness_requested, p->sharpness_applied,
                p->stage_ms[0], p->stage_ms[1], p->stage_ms[2], p->stage_ms[3],
                (unsigned long long)p->source_ns, p->source_reset, p->docked,
                p->alloc_attempts, p->allocations, p->alloc_waits,
                (unsigned long long)p->alloc_wait_ns, (unsigned long long)p->slot_wait_ns,
                p->display_w, p->display_h, p->actual_w, p->actual_h,
                p->output_height, p->supersampling, p->guest_w, p->guest_h,
                p->crop[0], p->crop[1], p->crop[2], p->crop[3],
                p->final_source_w, p->final_source_h, p->drawable_w, p->drawable_h,
                p->swapchain_w, p->swapchain_h, p->reconstruction_submitted,
                p->present_accepted, p->generated_accepted);
    }
    int ok = !ferror(out);
    if (fclose(out) || !ok || rename(tmp, path)) {
        unlink(tmp);
        return 0;
    }
    return 1;
}

static const char *vk_pass_dump_root(void) {
    if (g_capture_rolling_dump) return g_capture_slot;
    const char *root = getenv("DM_ROOT");
    return root ? root : ".";
}

int vk_pass_dump_user_begin(int delay, int full) {
    pthread_mutex_lock(&g_capture_lock);
    if (g_capture_exporting || g_capture_rolling_dump || atomic_load(&g_pd_user_pending)) {
        pthread_mutex_unlock(&g_capture_lock);
        return 0;
    }
    scale_capture_row *rows = calloc(SCALE_CAPTURE_FRAMES, sizeof *rows);
    if (!rows) {
        pthread_mutex_unlock(&g_capture_lock);
        return 0;
    }
    free(g_scale_capture);
    g_scale_capture = rows;
    g_scale_capture_ticket = g_scale_capture_overwritten = 0;
    const char *root = vk_pass_dump_root();
    char path[1024];
    for (unsigned i = 0; i < CAPTURE_SLOTS; i++) {
        snprintf(path, sizeof path, "%s/diagnostic-capture-%u/complete", root, i);
        unlink(path);
    }
    snprintf(path, sizeof path, "%s/scaling_frames.tsv", root);
    unlink(path);
    snprintf(path, sizeof path, "%s/capture_timeline.tsv", root);
    unlink(path);
    g_capture_timeline = full ? fopen(path, "w") : NULL;
    if (g_capture_timeline)
        fputs("present\tsubmission\tdraws_total\tpasses_total\tdispatches_total\tgeometry\n", g_capture_timeline);
    g_capture_delay = delay + 1; // let the pause overlay leave the screen
    g_capture_next_slot = 0;
    g_capture_next_present = 0;
    g_pd_scene_waited = 0;
    atomic_store(&g_pd_user_pending, full ? CAPTURE_SNAPSHOTS : CAPTURE_SCALING);
    pthread_mutex_unlock(&g_capture_lock);
    return 1;
}

static void capture_stop_locked(void) {
    atomic_store(&g_pd_user_pending, CAPTURE_OFF);
    if (g_capture_timeline && fclose(g_capture_timeline) != 0)
        VKLOG("[diagnostics] could not finish capture_timeline.tsv");
    g_capture_timeline = NULL;
    const char *root = getenv("DM_ROOT");
    if (capture_scale_write(root ? root : ".")) {
        free(g_scale_capture);
        g_scale_capture = NULL;
    } else {
        VKLOG("[diagnostics] could not write scaling_frames.tsv; retained samples for retry");
    }
}

void vk_pass_dump_user_stop(void) {
    pthread_mutex_lock(&g_capture_lock);
    if (!g_capture_exporting) capture_stop_locked();
    pthread_mutex_unlock(&g_capture_lock);
}

int vk_pass_dump_export_begin(void) {
    pthread_mutex_lock(&g_capture_lock);
    int available = !g_capture_exporting;
    if (available) {
        g_capture_exporting = 1;
        capture_stop_locked();
    }
    pthread_mutex_unlock(&g_capture_lock);
    return available;
}

void vk_pass_dump_export_end(void) {
    pthread_mutex_lock(&g_capture_lock);
    g_capture_exporting = 0;
    pthread_mutex_unlock(&g_capture_lock);
}

int vk_pass_dump_user_active(void) { return atomic_load(&g_pd_user_pending); }

static int capture_start_slot(void) {
    int n = snprintf(g_capture_slot, sizeof g_capture_slot,
                     "%s/diagnostic-capture-%u", vk_pass_dump_root(), g_capture_next_slot);
    if (n < 0 || (size_t)n >= sizeof g_capture_slot) return 0;
    if (mkdir(g_capture_slot, 0700) != 0 && errno != EEXIST) return 0;
    struct stat st;
    if (lstat(g_capture_slot, &st) != 0 || !S_ISDIR(st.st_mode)) return 0;
    char path[1024];
    static const char *const reports[] = {
        "complete", "pass_dump_manifest.tsv", "gpu_draw_inputs.tsv", "surface_usage.tsv",
        "surface_samples.tsv", "surface_samples.raw",
    };
    for (unsigned i = 0; i < sizeof reports / sizeof reports[0]; i++) {
        snprintf(path, sizeof path, "%s/%s", g_capture_slot, reports[i]);
        unlink(path);
    }
    g_capture_next_slot = (g_capture_next_slot + 1) % CAPTURE_SLOTS;
    g_capture_rolling_dump = 1;
    return 1;
}

static void capture_finish_slot(void) {
    if (atomic_load(&g_pd_user_pending) == CAPTURE_SNAPSHOTS) {
        char path[1024];
        snprintf(path, sizeof path, "%s/complete", g_capture_slot);
        FILE *file = fopen(path, "w");
        if (file) fclose(file);
    }
    g_capture_rolling_dump = 0;
}
