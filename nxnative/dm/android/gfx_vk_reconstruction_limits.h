#ifndef DM_VK_RECONSTRUCTION_LIMITS_H
#define DM_VK_RECONSTRUCTION_LIMITS_H
#include <stdint.h>
#define DM_RECON_MIB UINT64_C(1048576)
/* Each reconstruction pool may use at most 1/32 of the largest device-local
 * heap. This is a conservative policy, not a free-memory measurement; actual
 * Vulkan allocations and device resource limits remain authoritative. There is
 * no fixed byte or pixel ceiling independent of device capacity. */
static inline uint64_t dm_recon_budget(uint64_t local_heap) {
    return local_heap / 32;
}
/* Fit the actual drawable surface without floating rounding on the limiting
 * axis. Products of two uint32_t values fit in uint64_t. */
static inline void dm_recon_picture_extent(uint32_t iw, uint32_t ih,
        uint32_t sw, uint32_t sh, uint32_t *ow, uint32_t *oh) {
    *ow = *oh = 0;
    if (!iw || !ih || !sw || !sh) return;
    if ((uint64_t)sw * ih <= (uint64_t)sh * iw) {
        *ow = sw;
        *oh = (uint32_t)((uint64_t)sw * ih / iw);
    } else {
        *ow = (uint32_t)((uint64_t)sh * iw / ih);
        *oh = sh;
    }
}
static inline int dm_recon_extent_ok(uint32_t w, uint32_t h, uint32_t max_w, uint32_t max_h) {
    return w && h && w <= max_w && h <= max_h;
}
static inline int dm_recon_fits(uint64_t budget, uint64_t used, uint64_t bytes) {
    return used <= budget && bytes <= budget - used;
}
static inline float dm_recon_sharpness(int pct) {
    return pct <= 0 ? 0.0f : pct >= 100 ? 1.0f : pct / 100.0f;
}
/* A transient shared-image allocation failure is not a permanent format
 * capability decision. Scope suppression to format and output geometry. */
typedef struct {
    uint64_t after;
    uint32_t format, w, h;
} dm_recon_shared_retry;
static inline int dm_recon_shared_allowed(const dm_recon_shared_retry *r,
        uint32_t format, uint32_t w, uint32_t h, uint64_t ticket) {
    return !r->after || r->format != format || r->w != w || r->h != h ||
        ticket >= r->after;
}
static inline void dm_recon_shared_failed(dm_recon_shared_retry *r,
        uint32_t format, uint32_t w, uint32_t h, uint64_t ticket) {
    *r = (dm_recon_shared_retry){.after=ticket > UINT64_MAX-120 ? UINT64_MAX : ticket+120,
        .format=format, .w=w, .h=h};
}
static inline void dm_recon_shared_succeeded(dm_recon_shared_retry *r) {
    *r = (dm_recon_shared_retry){0};
}
#endif
