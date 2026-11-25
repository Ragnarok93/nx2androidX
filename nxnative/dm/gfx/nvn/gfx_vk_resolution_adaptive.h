#ifndef NX_RS_ADAPTIVE_H
#define NX_RS_ADAPTIVE_H
#include <stdint.h>
// Backend target policy, separate from engine CVar dynamic resolution.
// Percent is a request, not measured physical scale.
typedef struct {
    unsigned min,max,scale,budget_us,bad,good;
    uint64_t last,after;
    int valid;
} nx_rs_adaptive;
static inline unsigned nx_rs_budget(unsigned fps) {
    return fps==30?33333:fps==40?25000:fps==60?16667:0;
}
static inline int nx_rs_scale_step(unsigned p) { return p>=50 && p<=100 && p%5==0; }
static inline int nx_rs_adaptive_reset(nx_rs_adaptive *s,unsigned min,unsigned max,unsigned fps,uint64_t frame) {
    if (!s || !nx_rs_scale_step(min) || !nx_rs_scale_step(max) || min>max) return 0;
    *s=(nx_rs_adaptive){.min=min,.max=max,.scale=max,.budget_us=nx_rs_budget(fps),.after=frame};
    return 1;
}
// CPU work is a conservative upper-bound estimate with pacing waits removed.
// Unknown/CPU-bound samples break the streak, rather than lower resolution.
// 12 distinct overloaded frames to decrease, 60 with headroom to increase.
static inline int nx_rs_adaptive_sample(nx_rs_adaptive *s,uint64_t frame,unsigned gpu,unsigned cpu) {
    if (!s || !s->budget_us || frame<=s->after || (s->valid && frame<=s->last)) return 0;
    if (s->valid && frame-s->last>1) s->bad=s->good=0;
    s->last=frame; s->valid=1;
    if (!gpu || !cpu || gpu>500000 || cpu>500000) { s->bad=s->good=0; return 0; }
    unsigned before=s->scale;
    if (cpu>=s->budget_us*9/10 || cpu>=gpu) { s->bad=s->good=0; return 0; }
    if (gpu>s->budget_us) {
        s->good=0;
        if (++s->bad>=12) { if (s->scale>s->min) s->scale-=5; s->bad=0; }
    } else if (gpu<s->budget_us*7/10) {
        s->bad=0;
        if (++s->good>=60) { if (s->scale<s->max) s->scale+=5; s->good=0; }
    } else s->bad=s->good=0;
    return before!=s->scale;
}
static inline void nx_rs_adaptive_requested(nx_rs_adaptive *s,uint64_t frame) {
    s->after=frame; s->bad=s->good=0;
}
#endif
