#ifndef NX_RS_DRAW_H
#define NX_RS_DRAW_H
#include <stdint.h>
typedef struct { uint32_t lw,lh,pw,ph; int live; } nx_rs_draw_extent;
typedef struct { unsigned binding; float scale; } nx_rs_draw_sample_entry;
typedef struct { nx_rs_draw_sample_entry entries[72]; unsigned count; } nx_rs_draw_samples;
static inline int nx_rs_draw_group(const nx_rs_draw_extent *a,unsigned n,nx_rs_draw_extent *out) {
    if (!a || !out || !n || n>9) return 0;
    nx_rs_draw_extent first={0};
    for (unsigned i=0;i<n;i++) if (a[i].live) {
        const nx_rs_draw_extent *v=&a[i];
        if (!v->lw || !v->lh || !v->pw || !v->ph ||
            (uint64_t)v->pw*v->lh!=(uint64_t)v->ph*v->lw ||
            (uint64_t)v->pw*2<v->lw || (uint64_t)v->pw>2ull*v->lw) return 0;
        if (!first.live) first=*v;
        else if (first.lw!=v->lw || first.lh!=v->lh || first.pw!=v->pw || first.ph!=v->ph) return 0;
    }
    if (!first.live) return 0;
    *out=first; return 1;
}
static inline int nx_rs_draw_sample(nx_rs_draw_samples *s,unsigned binding,float scale,int supported) {
    if (!s || !(scale>=0.5f && scale<=2.0f)) return 0;
    if (scale==1.0f) {
        for (unsigned i=0;i<s->count;i++) if (s->entries[i].binding==binding) return 0;
        return 1;
    }
    if (!supported) return 0;
    for (unsigned i=0;i<s->count;i++) if (s->entries[i].binding==binding) return s->entries[i].scale==scale;
    if (s->count>=72) return 0;
    s->entries[s->count++]=(nx_rs_draw_sample_entry){binding,scale}; return 1;
}
#endif
