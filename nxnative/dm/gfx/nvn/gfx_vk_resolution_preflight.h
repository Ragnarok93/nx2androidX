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
/* Descriptor start is aligned relative to the backing chunk, not address 0.
 * Reject unknown lengths and cross-chunk ranges instead of guessing. */
static inline int nx_rs_buffer_span(uint64_t lo,uint64_t hi,uint64_t addr,uint64_t len,uint64_t align,uint64_t *start,uint64_t *size) {
    if (!start || !size || !align || !len || lo>=hi || addr<lo || addr>=hi || len>hi-addr) return 0;
    uint64_t pad=(addr-lo)%align;
    *start=addr-pad; *size=len+pad;
    return 1;
}
// All indirect draw argument reads are explicitly bounded. The first
// count-based record may consume the full array; later records are either
// skipped by the count path or read individually by the fallback.
static inline int nx_rs_indirect_span(int indexed,uint64_t ind,uint64_t count,
                                      int i,int max,int stride,uint64_t *bytes) {
    if (!bytes) return 0;
    *bytes=0;
    if (!ind) return !count;
    if (ind&3) return 0;
    unsigned record=indexed?20:16;
    *bytes=record;
    if (count) {
        if ((count&3) || max<=0 || i<0 || i>=max || stride<(int)record || (stride&3)) return 0;
        if (!i) *bytes=(uint64_t)(unsigned)max*(unsigned)stride;
    }
    return *bytes<=UINT64_MAX-ind;
}
#endif
