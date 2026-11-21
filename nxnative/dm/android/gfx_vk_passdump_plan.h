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
// Opt-in scene capture should leave its bounded budget for real geometry.
static inline int vk_pd_pass_has_draws(uint64_t submitted_draws, unsigned minimum) {
    return submitted_draws >= minimum;
}
// Depth prepasses have no color attachments. Capturing just their first
// qualifying depth target establishes whether it was already empty.
static inline int vk_pd_depth_prepass_eligible(unsigned ncolor,
                                             int has_depth, uint64_t draws,
                                             unsigned min_draws, int already_done) {
    return !ncolor && has_depth && !already_done && draws >= min_draws;
}
// Preserve the image budget for composition. Allow smaller two-target
// effects passes (e.g. layered fog) as well as half-size and larger outputs.
static inline int vk_pd_effect_color(unsigned colors, uint32_t width, uint32_t height,
                                     uint32_t scene_width, uint32_t scene_height) {
    return scene_width && scene_height && colors && colors <= 2 &&
           (colors == 2 || (width >= (scene_width + 1) / 2 && height >= (scene_height + 1) / 2));
}
// In VK_FORMAT_D24_UNORM_S8_UINT depth-aspect transfers the 24-bit UNORM
// value occupies the low 24 bits of each 32-bit word. High bits undefined.
// Allow +/- 1 ULP for conversion/rounding to the expected known-depth clear.
static inline unsigned vk_pd_d24_matches(const uint32_t *data, unsigned count,
                                          uint32_t expected, uint32_t tolerance) {
    unsigned hits = 0;
    for (unsigned i = 0; i < count; ++i) {
        uint32_t value = data[i] & UINT32_C(0xffffff);
        uint32_t diff = value > expected ? value - expected : expected - value;
        if (diff <= tolerance) hits++;
    }
    return hits;
}
static inline unsigned vk_pd_d24_float_matches(const uint32_t *words, unsigned n,
                                                float known) {
    uint32_t bits = 0; __builtin_memcpy(&bits, &known, sizeof bits);
    unsigned hits = 0;
    for (unsigned i = 0; i < n; ++i) if (words[i] == bits) ++hits;
    return hits;
}
static inline int vk_pd_budget_allows(uint64_t used, uint64_t requested, uint64_t budget) {
    return requested && used <= budget && requested <= budget - used;
}
// A menu capture waits for depth-tested scene work before starting the
// next frame. The bound also allows captures of 2D games and clear-only bugs.
static inline int vk_pd_wait_for_scene(unsigned *waited, unsigned limit, int has_scene) {
    if (has_scene || *waited >= limit) return 0;
    ++*waited;
    return 1;
}
// Hash the exact GPU readback bytes, not an 8-bit preview with clipped HDR.
static inline uint64_t vk_pd_hash(const void *data, size_t size) {
    const uint8_t *p = (const uint8_t *)data;
    uint64_t h = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < size; ++i) h = (h ^ p[i]) * UINT64_C(1099511628211);
    return h;
}
#endif
