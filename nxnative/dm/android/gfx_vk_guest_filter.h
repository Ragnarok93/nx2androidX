#pragma once
#include <stddef.h>
#include <stdint.h>

// Only call after certifying the original program and retaining its IDs.
// Bypass the cubic warp of each texel fraction before the single LOD-zero
// sample. The clamps, NaN handling and all four output stores remain intact.
static int dm_guest_bilinear_patch(uint32_t *words, size_t count) {
    size_t warp[2] = {0};
    for (size_t at = 5; at < count;) {
        uint32_t width = words[at] >> 16, opcode = words[at] & 0xffff;
        if (!width || width > count - at) return 0;
        if (opcode == 12 && width == 8 && words[at + 4] == 50) { // GLSL.std.450 Fma
            if (words[at + 2] == 84 && words[at + 7] == 83) warp[0] = at;
            if (words[at + 2] == 104 && words[at + 7] == 103) warp[1] = at;
        }
        at += width;
    }
    if (!warp[0] || !warp[1]) return 0;
    for (unsigned i = 0; i < 2; ++i) {
        size_t at = warp[i];
        words[at] = (4u << 16) | 83u; // OpCopyObject with original type and result
        words[at + 3] = i ? 102 : 82; // the unwarped texel position
        for (unsigned j = 4; j < 8; ++j) words[at + j] = 1u << 16; // OpNop
    }
    return 1;
}
