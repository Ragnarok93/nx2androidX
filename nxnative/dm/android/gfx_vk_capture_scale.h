#pragma once
#include <stdint.h>

enum { CAPTURE_OFF, CAPTURE_SCALING, CAPTURE_SNAPSHOTS };

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
    uint64_t timing_ticket;
    double gpu_ms;
    int timing_mode;
    unsigned timing_iw, timing_ih, timing_ow, timing_oh, timing_cadence_us;
    float timing_budget_ms;
    uint64_t timing_generation, timing_history_epoch;
    int timing_history_reset, sharpness_requested, sharpness_applied;
    double stage_ms[4];
    uint64_t source_ns, alloc_wait_ns, slot_wait_ns;
    unsigned source_reset, alloc_attempts, allocations, alloc_waits;
    int docked;
    const char *reason; // immutable reconstruction status string
} vk_capture_scale_present_sample;

int vk_pass_dump_user_active(void);
void vk_rs_capture_replay(uint64_t ticket, uint64_t cpu_replay_ns, double recent_gpu_ms);
void vk_capture_scale_present(uint64_t ticket, vk_capture_scale_present_sample sample);
