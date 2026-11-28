#pragma once
#include <stdint.h>
#include <math.h>
// One retired LSFG submission. Times are disjoint GPU intervals, not CPU waits.
typedef struct {
    uint64_t serial,epoch;
    unsigned scale,width,height,generated,valid;
    double copy_ms,mip_ms,shared_ms,refine_ms,output_ms,total_ms;
} dm_fg_timing;
static inline int dm_fg_timing_decode(dm_fg_timing *s,const uint64_t *t,unsigned n,unsigned bits,double period_ns) {
    s->valid=0;
    if (!t || s->generated>3 || n!=4+2*s->generated || !bits || bits>64 ||
        !(period_ns>0) || !isfinite(period_ns)) return 0;
    const uint64_t mask=bits==64?UINT64_MAX:(UINT64_C(1)<<bits)-1;
    double d[9]={0};
    for(unsigned i=1;i<n;i++) d[i-1]=(double)((t[i]-t[i-1])&mask)*period_ns/1e6;
    s->copy_ms=d[0];s->mip_ms=d[1];s->shared_ms=d[2];
    s->refine_ms=s->output_ms=0;
    for(unsigned i=0;i<s->generated;i++) {s->refine_ms+=d[3+2*i];s->output_ms+=d[4+2*i];}
    s->total_ms=s->copy_ms+s->mip_ms+s->shared_ms+s->refine_ms+s->output_ms;
    s->valid=isfinite(s->total_ms);
    return s->valid;
}
static inline int dm_fg_timing_fresh(const dm_fg_timing *s,uint64_t epoch,uint64_t last) {
    return s->valid && s->generated && s->epoch==epoch && s->serial>last;
}
