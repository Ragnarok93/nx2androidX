#ifndef DM_VK_FLOW_BUDGET_H
#define DM_VK_FLOW_BUDGET_H
#include <stdint.h>

/* The host rate limiter may impose a slower cadence than the guest interval.
 * This is an estimate of source period, not a GPU headroom measurement. */
typedef struct { uint32_t period_us; float budget_ms; } dm_flow_cadence;
static inline dm_flow_cadence dm_flow_cadence_estimate(double guest_period, int cap_fps) {
    if (!(guest_period >= 0.0) || guest_period > 1.0) return (dm_flow_cadence){0};
    double period = guest_period;
    if (cap_fps > 0 && period < 1.0 / cap_fps) period = 1.0 / cap_fps;
    if (period == 0.0) return (dm_flow_cadence){0, 2.0f};
    double budget = period * 100.0;
    return (dm_flow_cadence){(uint32_t)(period * 1000000.0 + 0.5),
        (float)(budget > 2.5 ? 2.5 : budget)};
}

/* Present-thread policy. Only completed GSR2 samples from the current
 * workload generation may enter this controller. Spatial samples never do. */
typedef struct {
    uint32_t iw, ih, ow, oh, cadence_us, source_format, output_format;
    int crop_x, crop_y, flip_y, physical, sharpen;
} dm_flow_workload;
typedef struct {
    dm_flow_workload key;
    uint64_t generation, retry_after, last_sample;
    unsigned slow, affordable, backoff, overload_events;
    int valid;
} dm_flow_budget;

static inline void dm_flow_budget_invalidate(dm_flow_budget *p) {
    if (!p->valid) return;
    uint64_t generation = p->generation + 1;
    *p = (dm_flow_budget){.generation=generation};
}

static inline void dm_flow_budget_set_workload(dm_flow_budget *p, const dm_flow_workload *k) {
    const dm_flow_workload *a = &p->key;
    if (p->valid && a->iw == k->iw && a->ih == k->ih && a->ow == k->ow &&
        a->oh == k->oh && a->cadence_us == k->cadence_us &&
        a->source_format == k->source_format && a->output_format == k->output_format &&
        a->crop_x == k->crop_x && a->crop_y == k->crop_y &&
        a->flip_y == k->flip_y && a->physical == k->physical && a->sharpen == k->sharpen) return;
    uint64_t generation = p->generation + 1;
    *p = (dm_flow_budget){.key=*k, .generation=generation, .backoff=120, .valid=1};
}

static inline int dm_flow_budget_allow(dm_flow_budget *p, uint64_t ticket) {
    if (!p->valid || p->retry_after == UINT64_MAX || ticket < p->retry_after) return 0;
    if (p->retry_after) {
        p->retry_after = 0;
        p->generation++;
        p->last_sample = 0;
    }
    return 1;
}

static inline void dm_flow_budget_observe(dm_flow_budget *p, uint64_t generation,
        uint64_t sample_ticket, uint64_t decision_ticket, float budget_ms, double cost_ms) {
    if (!p->valid || p->retry_after || generation != p->generation ||
        !sample_ticket || sample_ticket <= p->last_sample || sample_ticket >= decision_ticket ||
        !(budget_ms > 0) || !(cost_ms >= 0)) return;
    p->last_sample = sample_ticket;
    if (cost_ms <= budget_ms) {
        p->slow = 0;
        if (p->affordable < 60 && ++p->affordable == 60) {
            p->backoff = 120;
            p->overload_events = 0;
        }
        return;
    }
    p->affordable = 0;
    if (++p->slow < 5) return;
    // After three failed recovery probes, wait for a workload change.
    p->retry_after = ++p->overload_events >= 4 ? UINT64_MAX : decision_ticket + p->backoff;
    p->backoff = p->backoff < 1920 ? p->backoff * 2 : 1920;
    p->slow = 0;
    p->generation++;
    p->last_sample = 0;
}
// Resource/recording failures have a separate retry clock from measured overload.
static inline uint64_t dm_flow_resource_defer(unsigned *failures, uint64_t ticket) {
    unsigned shift = *failures < 3 ? (*failures)++ : 3;
    uint64_t delay = UINT64_C(120) << shift;
    return ticket > UINT64_MAX - delay ? UINT64_MAX : ticket + delay;
}
static inline void dm_flow_budget_retire_samples(dm_flow_budget *p) {
    p->generation++;
    p->last_sample = 0;
    p->slow = p->affordable = 0;
}
#endif
