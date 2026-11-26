#ifndef NX_RS_EXTENT_H
#define NX_RS_EXTENT_H
#include "gfx_vk_resolution_raster.h"
// Quantize a requested reduction to the nearest exact aspect-ratio lattice
// point that round-trips through the existing scalar shader ABI. Never
// silently substitute native resolution or move by over 2.5 percentage points.
static inline int nx_rs_choose_extent_aligned(nx_rs_raster *r,uint32_t lw,uint32_t lh,unsigned pct,uint32_t mw,uint32_t mh,uint32_t ax,uint32_t ay) {
    if (!r || !lw || !lh || lw>INT32_MAX || lh>INT32_MAX || pct<50 || pct>=100 || !mw || !mh || !ax || !ay || lw%ax || lh%ay) return 0;
    uint32_t a=lw,b=lh;
    while (b) { uint32_t rem=a%b; a=b; b=rem; }
    uint32_t g=a, uw=lw/g, uh=lh/g;
    uint64_t desired=(uint64_t)g*pct;
    uint32_t lo=(uint32_t)(desired/100), hi=lo+1;
    // Bounded search, only in the requested scale's immediate neighborhood.
    for (unsigned i=0;i<64;i++) {
        uint64_t dl=lo ? desired-(uint64_t)lo*100 : UINT64_MAX;
        uint64_t dh=(uint64_t)hi*100-desired;
        int upper=dh<=dl;
        uint64_t delta=upper?dh:dl;
        if (delta>UINT64_MAX/2 || delta*2>(uint64_t)g*5) return 0;
        uint32_t k=upper?hi++:lo--;
        if (k>=g || (uint64_t)k*2<g) continue;
        uint32_t w=uw*k,h=uh*k;
        if (w<=mw && h<=mh && !(w%ax) && !(h%ay) && nx_rs_raster_init(r,lw,lh,w,h)) return 1;
    }
    return 0;
}
static inline int nx_rs_choose_extent(nx_rs_raster *r,uint32_t lw,uint32_t lh,unsigned pct,uint32_t mw,uint32_t mh) {
    return nx_rs_choose_extent_aligned(r,lw,lh,pct,mw,mh,1,1);
}
static inline uint32_t nx_rs_grid_lcm(uint32_t a,uint32_t b) {
    if (!a) a=1;
    if (!b) return 0;
    uint32_t x=a,y=b;
    while (y) {uint32_t rem=x%y;x=y;y=rem;}
    uint64_t lcm=(uint64_t)(a/x)*b;
    return lcm<=UINT32_MAX?(uint32_t)lcm:0;
}
// 0: impossible, 1: retain current extent, 2: extent changed, rescan all
// earlier dispatches and consumers before publishing any physical image.
static inline int nx_rs_require_grid(nx_rs_raster *r,unsigned pct,uint32_t mw,uint32_t mh,
                                     uint32_t *ax,uint32_t *ay,uint32_t x,uint32_t y) {
    if (!r || !ax || !ay) return 0;
    uint32_t a=nx_rs_grid_lcm(*ax,x),b=nx_rs_grid_lcm(*ay,y);
    if (!a || !b || r->lw%a || r->lh%b) return 0;
    if (!(r->pw%a) && !(r->ph%b)) {*ax=a;*ay=b;return 1;}
    nx_rs_raster next;
    if (!nx_rs_choose_extent_aligned(&next,r->lw,r->lh,pct,mw,mh,a,b)) return 0;
    *r=next;*ax=a;*ay=b;return 2;
}
#endif
