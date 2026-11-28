#pragma once
#include "gfx_vk_fg_timing.h"
enum { FG_SEED_DEFER, FG_SEED_PARALLEL, FG_SEED_INSTEAD_OF_GENERATION };
// A conservative proxy: charge all shared alpha/beta work as seed cost,
// even though the seed excludes beta. Never force double generation.
// Only replace optional generation on a downward probe, and only when the
// active copy/shared plus seed fits inside the original estimated budget.
static inline int dm_fg_seed_policy(const dm_fg_timing *s,unsigned pct,double budget,int downward) {
    if(!s->valid || !s->generated || !s->scale || !pct || !(budget>0) ||
        !isfinite(budget) || !(s->total_ms>0) || !isfinite(s->total_ms))return FG_SEED_DEFER;
    double ratio=(double)pct/s->scale;
    double seed=s->copy_ms+(s->mip_ms+s->shared_ms)*ratio*ratio;
    if(!(seed>0) || !isfinite(seed))return FG_SEED_DEFER;
    if(s->total_ms+seed<=.90*budget)return FG_SEED_PARALLEL;
    if(downward && s->copy_ms+s->mip_ms+s->shared_ms+seed<=.90*budget)
        return FG_SEED_INSTEAD_OF_GENERATION;
    return FG_SEED_DEFER;
}
