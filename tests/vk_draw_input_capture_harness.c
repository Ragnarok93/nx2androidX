#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "gfx_vk_passdump_plan.h"

#define VKLOG(...) do { } while (0)
static struct { VkDevice dev; } V;
static uint64_t g_pd_bytes;
static int g_pass_wmem, g_tf_in_pass;
typedef struct { uint64_t lo, hi, base, boff, serial; VkBuffer buf; } vkpool_t;
static vkpool_t pool = {0x1000, 0x1060, 0x0800, 16, 7, (VkBuffer)1};
static uint8_t src[128], dst[96 * 256];
static VkBufferCopy recorded[96];
static unsigned ncopy, nbarrier;
static int complete, mapped;
static uint64_t budget = 128 * 1024 * 1024;
static uint64_t pass_dump_budget(void) { return budget; }
static vkpool_t *vk_pool_of(uint64_t guest, uint64_t size) {
    return guest >= pool.lo && guest < pool.hi && size <= pool.hi - guest ? &pool : NULL;
}
static VkCommandBuffer vk_cmd(void) { return (VkCommandBuffer)1; }
static uint32_t vk_mem_type(uint32_t bits, VkMemoryPropertyFlags want, VkMemoryPropertyFlags avoid) {
    (void)bits;
    (void)avoid;
    return want == VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ? 0 : UINT32_MAX;
}
static VkResult vkCreateBuffer(VkDevice dev, const VkBufferCreateInfo *in,
                               const VkAllocationCallbacks *alloc, VkBuffer *buf) {
    (void)dev; (void)alloc;
    assert(in->size == sizeof dst && in->usage == VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    *buf = (VkBuffer)2;
    return VK_SUCCESS;
}
static void vkGetBufferMemoryRequirements(VkDevice dev, VkBuffer buf, VkMemoryRequirements *req) {
    (void)dev;
    assert(buf == (VkBuffer)2);
    *req = (VkMemoryRequirements){sizeof dst + 4096, 4096, 1};
}
static VkResult vkAllocateMemory(VkDevice dev, const VkMemoryAllocateInfo *info,
                                 const VkAllocationCallbacks *alloc, VkDeviceMemory *mem) {
    (void)dev; (void)alloc;
    assert(info->allocationSize == sizeof dst + 4096);
    *mem = (VkDeviceMemory)1;
    return VK_SUCCESS;
}
static VkResult vkBindBufferMemory(VkDevice dev, VkBuffer buf, VkDeviceMemory mem, VkDeviceSize off) {
    (void)dev;
    assert(buf == (VkBuffer)2 && mem == (VkDeviceMemory)1 && off == 0);
    return VK_SUCCESS;
}
static VkResult vkMapMemory(VkDevice dev, VkDeviceMemory mem, VkDeviceSize off, VkDeviceSize size,
                            VkMemoryMapFlags flags, void **p) {
    (void)dev; (void)flags;
    assert(mem == (VkDeviceMemory)1 && off == 0 && size == VK_WHOLE_SIZE && !mapped);
    *p = dst;
    mapped = 1;
    return VK_SUCCESS;
}
static void vkDestroyBuffer(VkDevice dev, VkBuffer buf, const VkAllocationCallbacks *a) {
    (void)dev; (void)buf; (void)a;
    assert(!ncopy || complete);
}
static void vkFreeMemory(VkDevice dev, VkDeviceMemory mem, const VkAllocationCallbacks *a) {
    (void)dev; (void)mem; (void)a;
    assert(complete && mapped);
    mapped = 0;
}
static void vkCmdPipelineBarrier(VkCommandBuffer cb, VkPipelineStageFlags src_stage,
    VkPipelineStageFlags dst_stage, VkDependencyFlags dep, uint32_t count, const VkMemoryBarrier *bar,
    uint32_t nb, const VkBufferMemoryBarrier *b, uint32_t ni, const VkImageMemoryBarrier *im) {
    (void)cb; (void)dep; (void)b; (void)im;
    assert(count == 1 && nb == 0 && ni == 0);
    if (!nbarrier++) {
        assert(src_stage == VK_PIPELINE_STAGE_ALL_COMMANDS_BIT && dst_stage == VK_PIPELINE_STAGE_TRANSFER_BIT);
        assert(bar->srcAccessMask == VK_ACCESS_MEMORY_WRITE_BIT && bar->dstAccessMask == VK_ACCESS_TRANSFER_READ_BIT);
    } else {
        assert(src_stage == VK_PIPELINE_STAGE_TRANSFER_BIT);
        assert(dst_stage == (VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT));
        assert(bar->srcAccessMask == VK_ACCESS_TRANSFER_WRITE_BIT && bar->dstAccessMask == VK_ACCESS_HOST_READ_BIT);
    }
}
static void vkCmdCopyBuffer(VkCommandBuffer cb, VkBuffer from, VkBuffer to, uint32_t count, const VkBufferCopy *copy) {
    (void)cb;
    assert(from == (VkBuffer)1 && to == (VkBuffer)2 && count == 1 && nbarrier == 1 && !complete);
    recorded[ncopy++] = *copy;
}
static VkResult vkInvalidateMappedMemoryRanges(VkDevice dev, uint32_t count, const VkMappedMemoryRange *range) {
    (void)dev;
    assert(complete && count == 1 && range->offset == 0 && range->size == VK_WHOLE_SIZE);
    return VK_SUCCESS;
}

#include "gfx_vk_input_capture.inc"

int main(void) {
    const input_sample_t draw = {.present = 5, .draw = 19, .vs = 0x1234};
    setenv("DM_VK_PASSDUMP_INPUTS", "1", 1);
    input_capture_arm();
    assert(g_input_capture);
    assert(!input_capture_add(draw, "bad-range", 0x105e, pool.buf, 110, 4, 0));
    assert(!input_capture_add(draw, "bad-buffer", 0x1010, (VkBuffer)3, 32, 20, 0));
    assert(!input_capture_add(draw, "bad-offset", 0x1010, pool.buf, 0, 20, 0));

    assert(input_capture_add(draw, "discarded", 0x1010, pool.buf, 32, 20, 0));
    g_pass_wmem = 1;
    input_capture_end_pass();
    assert(g_input_rejected == 1 && !g_input_pending && !ncopy && !mapped);
    g_pass_wmem = 0;

    budget = sizeof dst;
    assert(input_capture_add(draw, "over-budget", 0x1010, pool.buf, 32, 20, 0));
    input_capture_end_pass();
    assert(!ncopy && !mapped && g_input_alloc_failed && !g_pd_bytes);

    budget *= 2;
    input_capture_arm();
    assert(input_capture_add(draw, "indexed-indirect", 0x1010, pool.buf, 32, 20, 0));
    assert(input_capture_add(draw, "index-binding-prefix", 0x1034, pool.buf, 68, 256, 1));
    input_capture_end_pass();
    assert(ncopy == 2 && nbarrier == 2 && mapped && recorded[1].size == 44);
    assert(g_pd_bytes == sizeof dst + 4096);

    // Simulate the producer's device writes occurring after CPU recording.
    memset(src + 32, 0xab, 20);
    memset(src + 68, 0xcd, 44);
    for (unsigned i = 0; i < ncopy; i++)
        memcpy(dst + recorded[i].dstOffset, src + recorded[i].srcOffset, recorded[i].size);
    complete = 1;
    input_capture_write();
    assert(!mapped && !g_input_capture);
}
