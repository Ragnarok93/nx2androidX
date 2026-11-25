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
#endif
