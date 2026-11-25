// Ryujinx SupportBuffer.cs std140 layout; per-stage buffers use frag_scale_count=0.
#ifndef NX_RS_SUPPORT_H
#define NX_RS_SUPPORT_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
enum { NX_RS_SUPPORT_BYTES = 1392, NX_RS_SCALE_OFFSET = 192, NX_RS_SCALE_SLOTS = 73 };
enum { NX_RS_REF_UNKNOWN = 255, NX_RS_REF_UNSUPPORTED = 1, NX_RS_REF_NEEDS_SCALE = 2, NX_RS_REF_WRITES = 4 };
static inline void nx_rs_support_init(void *data) {
    memset(data, 0, NX_RS_SUPPORT_BYTES);
    const float one = 1.0f;
    for (unsigned i = 0; i < NX_RS_SCALE_SLOTS; i++) memcpy((uint8_t *)data + NX_RS_SCALE_OFFSET + 16*i, &one, 4);
}
static inline int nx_rs_support_set(void *data, unsigned slot, float scale) {
    if (!data || slot >= NX_RS_SCALE_SLOTS || !(scale >= 0.5f && scale <= 2.0f)) return 0;
    memcpy((uint8_t *)data + NX_RS_SCALE_OFFSET + 16*slot, &scale, 4);
    return 1;
}
static inline int nx_rs_ref_lookup(const uint8_t *p, size_t n, unsigned binding, unsigned *slot, unsigned *flags) {
    if (!p || n < 8 || memcmp(p, "RSCL\1\0", 6)) return 0;
    size_t count = (size_t)p[6] | (size_t)p[7] << 8;
    if (n != 8 + 4*count) return 0;
    for (size_t i = 0; i < count; i++) {
        const uint8_t *r = p + 8 + 4*i;
        if (((unsigned)r[0] | (unsigned)r[1] << 8) == binding) {
            if ((r[3] & ~15u) || (r[2] != NX_RS_REF_UNKNOWN && (r[2] < 1 || r[2] >= NX_RS_SCALE_SLOTS))) return 0;
            *slot = r[2]; *flags = r[3]; return 1;
        }
    }
    return 0;
}
#endif
