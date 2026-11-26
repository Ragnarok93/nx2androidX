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
    uint64_t timing_ticket;
    double gpu_ms;
    const char *reason; // immutable reconstruction status string
} vk_capture_scale_present_sample;

int vk_pass_dump_user_active(void);
void vk_rs_capture_replay(uint64_t ticket, uint64_t cpu_replay_ns, double recent_gpu_ms);
void vk_capture_scale_present(uint64_t ticket, vk_capture_scale_present_sample sample);
