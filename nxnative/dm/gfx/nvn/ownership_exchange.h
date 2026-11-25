#ifndef NX_RS_EXCHANGE_H
#define NX_RS_EXCHANGE_H
#include <stdint.h>
typedef struct { uintptr_t owner; uint32_t lw,lh,pw,ph; uint64_t bytes; } nx_rs_backing_state;
/* A transaction must cover distinct, equally sized logical attachments. */
static inline int nx_rs_exchange_plan(const nx_rs_backing_state *s, unsigned n, uint32_t w, uint32_t h, unsigned *changed) {
    if (!s || !n || n>9 || !changed || !w || !h || w>INT32_MAX || h>INT32_MAX) return 0;
    unsigned mask=0,live=0; uint32_t lw=0,lh=0;
    for (unsigned i=0;i<n;i++) if (s[i].owner) {
        if (!s[i].lw || !s[i].lh || !s[i].pw || !s[i].ph || !s[i].bytes) return 0;
        for (unsigned j=0;j<i;j++) if (s[j].owner==s[i].owner) return 0;
        if (!live++) { lw=s[i].lw; lh=s[i].lh; }
        if (lw!=s[i].lw || lh!=s[i].lh || (uint64_t)w*lh!=(uint64_t)h*lw ||
            (uint64_t)w*2<lw || (uint64_t)w>2ull*lw) return 0;
        if (s[i].pw!=w || s[i].ph!=h) mask|=1u<<i;
    }
    if (!live) return 0;
    *changed=mask; return 1;
}
/* Downscaling must reduce allocation requirements, not merely pixel count. */
static inline int nx_rs_exchange_benefit(const nx_rs_backing_state *s, uint32_t w, uint32_t h, uint64_t bytes) {
    if (!s || !bytes) return 0;
    return (uint64_t)w*h >= (uint64_t)s->pw*s->ph || bytes<s->bytes;
}
#endif
