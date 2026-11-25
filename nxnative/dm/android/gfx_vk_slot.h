#pragma once
#include <stdint.h>

// c2 combined slots 32–39 share their words with image slots 0–7.
static inline uint64_t vk_tex_slot_handle(const uint64_t *tex, const uint64_t *img, unsigned reg) {
    return reg >= 32 && reg < 40 && img[reg - 32] ? img[reg - 32] : reg < 256 ? tex[reg] : 0;
}

// Dynamic arrays use a different priority for texel buffers. Keep replay
// and lifetime proof on the same handle selection.
static inline uint64_t vk_dyn_tex_slot_handle(const uint64_t *tex,unsigned slot,int texel) {
    if (slot>=32) return 0;
    unsigned first=texel?128+slot:slot, second=texel?slot:128+slot;
    return tex[first]?tex[first]:tex[second];
}
