#ifndef NX_RS_COMPUTE_H
#define NX_RS_COMPUTE_H
#include <stdint.h>
/* Only a logically addressed SPIR-V module can be admitted by descriptor
 * preflight. Reject malformed instructions, missing/duplicate memory models,
 * and physical addressing (including PhysicalStorageBuffer64). */
static inline int nx_rs_compute_logical(const uint32_t *w, uint32_t n) {
    if (!w || n<5 || w[0]!=0x07230203u) return 0;
    unsigned model=0;
    for (uint32_t i=5;i<n;) {
        uint32_t wc=w[i]>>16, op=w[i]&0xffffu;
        if (!wc || wc>n-i) return 0;
        if (op==14) { /* OpMemoryModel */
            if (wc!=3 || model || w[i+1]!=0) return 0;
            model=1;
        }
        i+=wc;
    }
    return model!=0;
}
/* Geometry only, NOT shader authorization. A 2D grid must cover the native
 * image exactly, and both extents must be multiples of the fixed local size.
 * No padded lanes, partial tiles, z layers, or nonuniform scaling. */
static inline int nx_rs_compute_grid(const uint32_t groups[3],const uint32_t local[3],
                                     uint32_t lw,uint32_t lh,uint32_t pw,uint32_t ph,
                                     uint32_t out[3]) {
    if (!groups || !local || !out || !lw || !lh || !pw || !ph ||
        !local[0] || !local[1] || local[2]!=1 || groups[2]!=1 ||
        pw>lw || ph>lh || (uint64_t)pw*lh!=(uint64_t)ph*lw ||
        (uint64_t)groups[0]*local[0]!=lw || (uint64_t)groups[1]*local[1]!=lh ||
        pw%local[0] || ph%local[1]) return 0;
    out[0]=pw/local[0]; out[1]=ph/local[1]; out[2]=1;
    return 1;
}
#endif
