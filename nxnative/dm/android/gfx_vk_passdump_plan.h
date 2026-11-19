// Host-testable selection and byte-level verification for opt-in Vulkan pass captures.
#ifndef GFX_VK_PASSDUMP_PLAN_H
#define GFX_VK_PASSDUMP_PLAN_H
#include <stddef.h>
#include <stdint.h>

// The default preserves the old single-attachment capture; all_colors captures
// each actually bound MRT slot in its original order, including sparse slots.
static inline unsigned vk_pd_select_colors(const uint8_t bound[8], unsigned ncol,
                                            int selected, int all_colors,
                                            unsigned available, uint8_t slots[8]) {
    if (ncol > 8) ncol = 8;
    if (available > 8) available = 8;
    if (!available) return 0;
    if (!all_colors) {
        unsigned first = 0;
        while (first < ncol && !bound[first]) first++;
        if (selected >= 0 && selected < (int)ncol && bound[selected]) first = (unsigned)selected;
        if (first == ncol) return 0;
        slots[0] = (uint8_t)first;
        return 1;
    }
    unsigned count = 0;
    for (unsigned i = 0; i < ncol && count < available; ++i)
        if (bound[i]) slots[count++] = (uint8_t)i;
    return count;
}
static inline int vk_pd_budget_allows(uint64_t used, uint64_t requested, uint64_t budget) {
    return requested && used <= budget && requested <= budget - used;
}
// Hash the exact GPU readback bytes, not an 8-bit preview with clipped HDR.
static inline uint64_t vk_pd_hash(const void *data, size_t size) {
    const uint8_t *p = (const uint8_t *)data;
    uint64_t h = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < size; ++i) h = (h ^ p[i]) * UINT64_C(1099511628211);
    return h;
}
#endif
