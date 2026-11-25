#ifndef NX_RS_SCOPE_H
#define NX_RS_SCOPE_H
#include <stdint.h>
// Closed lifetime: full initial overwrite, raster production, disjoint
// native consumer, then every original attachment explicitly discarded.
typedef struct { unsigned mask,cleared,discarded,phase,drew,sampled,original_bound; } nx_rs_scope;
static inline int nx_rs_scope_init(nx_rs_scope *s,unsigned mask) {
    if (!s || !mask || (mask&~511u)) return 0;
    *s=(nx_rs_scope){.mask=mask,.original_bound=1};return 1;
}
static inline int nx_rs_scope_clear(nx_rs_scope *s,unsigned bit,int full) {
    if (s->phase!=0 || !full || !bit || (bit&(bit-1)) || !(s->mask&bit)) return 0;
    s->cleared|=bit;return 1;
}
static inline int nx_rs_scope_draw(nx_rs_scope *s,int compatible,int samples_group) {
    if (!compatible) return 0;
    if (s->phase==0) {
        if (s->cleared!=s->mask || samples_group) return 0;
        s->drew=1;return 1;
    }
    if (s->phase==1 && !s->original_bound) { s->sampled|=samples_group!=0;return 1; }
    return 0;
}
static inline int nx_rs_scope_switch(nx_rs_scope *s,int original_group) {
    // Rebinding an identical group does not end its lifetime; likewise
    // any number of disjoint native passes may consume the scaled group.
    if (s->phase==0 && original_group) return 1;
    if (s->phase==0 && !original_group && s->drew && s->cleared==s->mask) { s->phase=1;s->original_bound=0;return 1; }
    if (s->phase==1 && !original_group) {s->original_bound=0;return 1;}
    if (s->phase==1 && original_group && s->sampled) { s->phase=2;s->original_bound=1;return 1; }
    return 0;
}
// Native-grid compute may sample the scaled producer without changing the
// bound graphics targets. Further raster writes to those targets are refused.
static inline int nx_rs_scope_dispatch(nx_rs_scope *s,int compatible,int samples_group) {
    if (!compatible || s->phase>1) return 0;
    if (s->phase==0) {
        // Independent native compute may run between producer draws.
        // Only actual sampling of the scaled group ends its write phase.
        if (!samples_group) return 1;
        if (!s->drew || s->cleared!=s->mask) return 0;
        s->phase=1;
    }
    s->sampled|=samples_group!=0;return 1;
}
// 0 rejected, 1 partial discard, 2 all discarded.
static inline int nx_rs_scope_discard(nx_rs_scope *s,unsigned bit) {
    if ((s->phase!=2 && !(s->phase==1 && s->original_bound && s->sampled)) ||
        !bit || (bit&(bit-1)) || !(s->mask&bit) || (s->discarded&bit)) return 0;
    s->phase=2;s->discarded|=bit;
    if (s->discarded!=s->mask) return 1;
    s->phase=3;return 2;
}
#endif
