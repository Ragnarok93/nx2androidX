#ifndef NX_RS_PREFLIGHT_H
#define NX_RS_PREFLIGHT_H
#include <stdint.h>
/* Non-overflowing overlap for ranges in one guest memory pool. */
static inline int nx_rs_range_overlap(uint64_t a,uint64_t an,uint64_t b,uint64_t bn) {
    if (!an || !bn) return 0;
    return a<=b ? b-a<an : a-b<bn;
}
/* 0 = native/no scaled overlap, 1 = compatible scaled owner, 2 = restore. */
static inline int nx_rs_preflight_overlap(uint64_t a,uint64_t an,uint64_t b,uint64_t bn,int exact,int supported) {
    if (!nx_rs_range_overlap(a,an,b,bn)) return 0;
    return exact && supported ? 1 : 2;
}
#endif
