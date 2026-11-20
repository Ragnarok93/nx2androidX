// Host-testable allocator plan used by the Vulkan small-image slot allocator.
// The slot stride, and thus every image-memory bind offset, must satisfy
// the image's VkMemoryRequirements::alignment (VUID-vkBindImageMemory-memoryOffset-01046).
#ifndef GFX_VK_IMG_ALLOC_PLAN_H
#define GFX_VK_IMG_ALLOC_PLAN_H
#include <stdint.h>
static inline uint64_t vk_img_aligned_stride(uint64_t class_size, uint64_t alignment) {
    if (!alignment) alignment = 1;
    // Vulkan VkMemoryRequirements.alignment is always a power of two.
    return (class_size + alignment - 1) & ~(alignment - 1);
}
#endif
