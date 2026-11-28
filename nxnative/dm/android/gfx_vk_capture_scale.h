#pragma once
#include <stdint.h>
#include "gfx_vk_fg_timing.h"
#include "gfx_vk_present_queue_trace.h"

// Preserve existing mode values for internal/legacy callers.
enum { CAPTURE_OFF, CAPTURE_SCALING, CAPTURE_SNAPSHOTS, CAPTURE_DETAILED };
static inline int capture_level_valid(int level) {
    return level==CAPTURE_SCALING || level==CAPTURE_DETAILED || level==CAPTURE_SNAPSHOTS;
}
static inline int capture_level_timeline(int level) {
    return level==CAPTURE_DETAILED || level==CAPTURE_SNAPSHOTS;
}
static inline const char *capture_level_name(int level) {
    return level==CAPTURE_SNAPSHOTS?"GPU snapshots":level==CAPTURE_DETAILED?"Detailed":"Performance";
}
static inline int capture_level_step(int level,int direction) {
    const int levels[]={CAPTURE_SCALING,CAPTURE_DETAILED,CAPTURE_SNAPSHOTS};
    unsigned i=0;while(i<2 && levels[i]!=level)i++;
    return levels[(i+(direction<0?2:1))%3];
}
int vk_pass_dump_user_begin_level(int delay,int level);

typedef struct {
    unsigned mode, min, max, target, passes;
    unsigned logical_w, logical_h, physical_w, physical_h, rejected_w, rejected_h;
    int renderer_available, gpu_snapshot;
    uint64_t cpu_replay_ns, passes_total, source_time_ns;
    uint64_t backing_bytes, peak_published_backing_bytes;
    const char *scope_reason;
    double recent_replay_gpu_ms;
} vk_capture_scale_replay_sample;

typedef struct {
    int requested, active, physical, generated;
    unsigned source_w, source_h, output_w, output_h;
    unsigned display_w, display_h, actual_w, actual_h;
    unsigned guest_w, guest_h, final_source_w, final_source_h;
    unsigned drawable_w, drawable_h, swapchain_w, swapchain_h;
    int crop[4];
    int output_height, supersampling, reconstruction_submitted;
    int present_accepted, generated_accepted;
    uint64_t scene_image, guest_image, completed_ticket;
    unsigned allocated_w, allocated_h;
    int scene_crop[4], reconstruction_recorded;
    uint64_t timing_ticket;
    double gpu_ms;
    int timing_mode;
    unsigned timing_iw, timing_ih, timing_ow, timing_oh, timing_cadence_us;
    float timing_budget_ms;
    uint64_t timing_generation, timing_history_epoch;
    int timing_history_reset, sharpness_requested, sharpness_applied;
    double stage_ms[4];
    uint64_t source_ns, alloc_wait_ns, slot_wait_ns, lifecycle_epoch;
    unsigned source_reset, alloc_attempts, allocations, alloc_waits;
    int docked;
    const char *fg_reason; // immutable frame-generation decision
    const char *fg_present_reason; // separate generated WSI admission result
    double fg_period_ms, fg_measured_ms, fg_hold_ms;
    uint64_t fg_refresh_ns;
    unsigned fg_planned, fg_warm_streak;
    int fg_requested_flow,fg_active_flow;
    uint64_t fg_submission_serial;
    dm_fg_timing fg_timing;
    dm_fg_governor_report fg_governor;
    unsigned fg_graph_phase,fg_pending_flow,fg_seed_policy,fg_seed_count;
    uint64_t fg_active_bytes,fg_extra_bytes,fg_build_limit;
    const char *reason; // immutable reconstruction status string
} vk_capture_scale_present_sample;

int vk_pass_dump_user_active(void);
void vk_rs_capture_replay(uint64_t ticket, uint64_t cpu_replay_ns, double recent_gpu_ms);
void vk_capture_scale_present(uint64_t ticket, vk_capture_scale_present_sample sample);
void vk_capture_scale_queue(uint64_t ticket, dm_present_queue_trace sample);
