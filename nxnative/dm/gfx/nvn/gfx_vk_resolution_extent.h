#ifndef NX_RS_EXTENT_H
#define NX_RS_EXTENT_H
#include "gfx_vk_resolution_raster.h"
// Quantize a requested reduction to the nearest exact aspect-ratio lattice
// point that round-trips through the existing scalar shader ABI. Never
// silently substitute native resolution or move by over 2.5 percentage points.
static inline int nx_rs_choose_extent(nx_rs_raster *r,uint32_t lw,uint32_t lh,unsigned pct,uint32_t mw,uint32_t mh) {
    if (!r || !lw || !lh || lw>INT32_MAX || lh>INT32_MAX || pct<50 || pct>=100 || !mw || !mh) return 0;
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
        if (w<=mw && h<=mh && nx_rs_raster_init(r,lw,lh,w,h)) return 1;
    }
    return 0;
}
#endif
