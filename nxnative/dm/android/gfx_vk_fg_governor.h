#pragma once
#include "gfx_vk_fg_timing.h"

// Pure policy. The backend owns resources and must acknowledge every request.
// All elapsed intervals are seconds of eligible real-frame cadence, not pauses.
enum { FG_FLOW_HOLD, FG_FLOW_DOWN, FG_FLOW_UP, FG_FLOW_ROLLBACK };
typedef struct { unsigned pct,reason; } dm_fg_flow_request;
typedef struct {
    uint64_t epoch,last;
    unsigned applied,pending,prior,pending_reason,probe_count;
    double down,up,backoff,baseline,base_budget,probe_time,probe_sum;
    int probe;
} dm_fg_governor;
static inline void dm_fg_governor_reset(dm_fg_governor *g,unsigned initial,uint64_t epoch) {
    *g=(dm_fg_governor){.epoch=epoch,.applied=initial<25?25:initial>100?100:initial};
}
static inline void dm_fg_governor_gap(dm_fg_governor *g) {
    g->down=g->up=0;
    g->probe_time=g->probe_sum=0;
}
static inline dm_fg_flow_request dm_fg_governor_request(dm_fg_governor *g,unsigned pct,unsigned why) {
    g->pending=pct;g->pending_reason=why;
    g->down=g->up=0;
    return (dm_fg_flow_request){pct,why};
}
static inline void dm_fg_governor_ack(dm_fg_governor *g,int success,uint64_t epoch) {
    if (!g->pending) return;
    if (success) {
        g->prior=g->applied;g->applied=g->pending;
        g->probe=g->pending_reason==FG_FLOW_DOWN;
        g->epoch=epoch;g->last=0;
    } else g->probe=0;
    g->pending=g->pending_reason=0;
    g->backoff=2.0;
    dm_fg_governor_gap(g);
}
static inline double dm_fg_predicted_ms(const dm_fg_timing *s,unsigned pct) {
    double ratio=(double)pct/s->scale;
    return s->copy_ms+s->output_ms+(s->mip_ms+s->shared_ms+s->refine_ms)*ratio*ratio;
}
static inline dm_fg_flow_request dm_fg_governor_observe(dm_fg_governor *g,const dm_fg_timing *s,
        double budget,double elapsed,int accepted,int recover) {
    dm_fg_flow_request hold={g->applied,FG_FLOW_HOLD};
    if (s->epoch!=g->epoch || s->serial<=g->last) return hold;
    if (g->last && s->serial!=g->last+1) dm_fg_governor_gap(g);
    g->last=s->serial;
    if (!dm_fg_timing_fresh(s,g->epoch,g->last-1) || s->scale!=g->applied ||
        !accepted || !(budget>0) || !isfinite(budget) || !(elapsed>0) ||
        !isfinite(elapsed) || !(s->total_ms>0) || !isfinite(s->total_ms)) {
        dm_fg_governor_gap(g);return hold;
    }
    if (elapsed>.25) {dm_fg_governor_gap(g);elapsed=.25;}
    if (g->pending) return hold;
    if (g->backoff>0) {g->backoff-=elapsed;if(g->backoff>1e-9)return hold;g->backoff=0;}
    if (g->probe) {
        // Compare only the same generation workload and approximately the
        // same available budget. Incomparable probes never justify rollback.
        if (s->generated!=g->probe_count || fabs(budget-g->base_budget)>.15*g->base_budget) {
            g->probe=0;dm_fg_governor_gap(g);return hold;
        }
        g->probe_time+=elapsed;g->probe_sum+=s->total_ms*elapsed;
        if (g->probe_time+.000001<.5) return hold;
        double gain=g->baseline-g->probe_sum/g->probe_time;
        g->probe=0;
        if (gain<.25 || gain<.05*g->baseline)
            return dm_fg_governor_request(g,g->prior,FG_FLOW_ROLLBACK);
        dm_fg_governor_gap(g);return hold;
    }
    unsigned lower=g->applied>35?g->applied-10:25;
    unsigned upper=g->applied<90?g->applied+10:100;
    if (lower<g->applied && s->total_ms>=.90*budget &&
        s->total_ms-dm_fg_predicted_ms(s,lower)>=.25) {
        g->up=0;g->down+=elapsed;
        if (g->down+.000001>=.4) {
            g->baseline=s->total_ms;g->base_budget=budget;g->probe_count=s->generated;
            return dm_fg_governor_request(g,lower,FG_FLOW_DOWN);
        }
    } else if (recover && upper>g->applied && dm_fg_predicted_ms(s,upper)<=.82*budget) {
        g->down=0;g->up+=elapsed;
        if (g->up+.000001>=4.0) return dm_fg_governor_request(g,upper,FG_FLOW_UP);
    } else dm_fg_governor_gap(g);
    return hold;
}
