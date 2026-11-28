// Idle Vulkan AHardwareBuffer reuse budget; never trims active pool storage.
// Inputs in KiB, output in bytes. Standalone so host tests exercise the same
// thresholds used on Android without a Vulkan SDK or device.
#ifndef NX2A_VK_POOL_PRESSURE_H
#define NX2A_VK_POOL_PRESSURE_H
#include <stdint.h>
static inline uint64_t vk_pool_reuse_budget_bytes(uint64_t available_kb, uint64_t total_kb) {
    const uint64_t full = 384ull << 20;
    if (!total_kb) return full;
    uint64_t low = total_kb / 5, critical = total_kb / 8;
    if (low < (512ull << 10)) low = 512ull << 10;
    if (low > (1536ull << 10)) low = 1536ull << 10;
    if (critical < (384ull << 10)) critical = 384ull << 10;
    if (critical > (1024ull << 10)) critical = 1024ull << 10;
    if (available_kb < critical) return 0;
    if (available_kb < low) return 64ull << 20;
    return full;
}
// Tighten immediately; relax only after three consecutive observations
// agree that the higher tier is safe. A single low sample cancels recovery.
typedef struct { uint64_t candidate; unsigned count; } vk_pool_recovery;
static inline uint64_t vk_pool_reuse_budget_update(vk_pool_recovery *s,
        uint64_t prior,uint64_t available_kb,uint64_t total_kb) {
    uint64_t wanted=vk_pool_reuse_budget_bytes(available_kb,total_kb);
    if(wanted<=prior){s->count=0;s->candidate=0;return wanted;}
    if(s->candidate!=wanted){s->candidate=wanted;s->count=0;}
    if(++s->count<3)return prior;
    s->count=0;s->candidate=0;return wanted;
}
#endif
