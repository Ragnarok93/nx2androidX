// Correct only Xclipse RADV's observed nonconforming D24 depth transfer
// when returning depth texels to the Switch guest CPU. Vulkan buffer copies
// require packed 24-bit UNORM; this driver returned 32-bit float depth.
#ifndef GFX_VK_D24_COPY_H
#define GFX_VK_D24_COPY_H
#include <stdint.h>
static inline uint32_t vk_d24_copy_float_to_unorm(float d) {
    if (!(d > 0.f)) return 0;          // -Inf, zero, negative, NaN
    if (d >= 1.f) return UINT32_C(0xffffff);
    return (uint32_t)(d * 16777215.0 + 0.5);
}
#endif
