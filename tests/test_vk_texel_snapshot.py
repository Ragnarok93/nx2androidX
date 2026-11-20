"""Exercise the production texel fallback against a recording Vulkan device."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

import host_cc

ROOT = Path(__file__).resolve().parents[1]
DM = ROOT / "nxnative/dm"


@unittest.skipUnless(host_cc.posix(), "POSIX C compiler required")
class TexelSnapshot(unittest.TestCase):
    def test_gpu_copy_order_and_vulkan_13_alignment(self):
        src = (DM / "android/gfx_vk_tex.c").read_text()
        implementation = src[src.index("static VkDeviceSize texel_alignment("):
                             src.index("typedef struct { uint64_t mem, pool;")]
        code = r'''
#define VK_NO_PROTOTYPES
#include <assert.h>
#include <stdint.h>
#include <vulkan/vulkan_core.h>
#include "nvn_obj.h"

typedef struct { VkFormat vk; unsigned bpb; } vkfmt_t;
static const vkfmt_t format = {VK_FORMAT_R32_SFLOAT, 4};
static const vkfmt_t *vk_fmt(int id) { return &format; }
static struct {
    VkDevice dev;
    VkPhysicalDeviceProperties props;
    VkPhysicalDeviceTexelBufferAlignmentProperties texel_alignment;
} V;
static unsigned step;
static VkDeviceSize source_offset=36, source_bytes=176;
static int vk_buf_at(uint64_t addr, uint64_t n, VkBuffer *buf, VkDeviceSize *off) {
    if (n > source_bytes) return 0;
    *buf = (VkBuffer)(uintptr_t)1;
    *off = source_offset;
    return 1;
}
static void vk_end_pass_land(void) { assert(++step == 1); }
static void vk_scratch(VkDeviceSize n, VkDeviceSize align, VkBuffer *buf, VkDeviceSize *off) {
    assert(++step == 2 && n == source_bytes && align == 64);
    *buf = (VkBuffer)(uintptr_t)2;
    *off = 256;
}
static VkResult vkCreateBufferView(VkDevice dev, const VkBufferViewCreateInfo *ci, const void *al, VkBufferView *view) {
    assert(++step == 3 && ci->offset == 256 && ci->range == source_bytes && ci->format == VK_FORMAT_R32_SFLOAT);
    *view = (VkBufferView)(uintptr_t)3;
    return VK_SUCCESS;
}
static VkCommandBuffer vk_cmd(void) { return (VkCommandBuffer)(uintptr_t)4; }
static void vk_img_barrier_all(VkCommandBuffer cb) { assert(++step == 4 || step == 6); }
static void vkCmdCopyBuffer(VkCommandBuffer cb, VkBuffer src, VkBuffer dst, unsigned n, const VkBufferCopy *copy) {
    assert(++step == 5 && n == 1 && src == (VkBuffer)(uintptr_t)1 && dst == (VkBuffer)(uintptr_t)2);
    assert(copy->srcOffset == source_offset && copy->dstOffset == 256 && copy->size == source_bytes);
}
static void vk_defer_free_bview(VkBufferView view) { assert(++step == 7 && view == (VkBufferView)(uintptr_t)3); }
''' + implementation + r'''
int main(void) {
    V.props.limits.minTexelBufferOffsetAlignment = 64;
    V.props.limits.maxTexelBufferElements = 4096;
    tex_t t = {.mem=(uint8_t *)(uintptr_t)4096, .d={.width=44}};
    int direct=0;
    assert(vk_tbuf_snapshot(&t, &direct) != VK_NULL_HANDLE && !direct && step == 7);

    step=0;
    V.texel_alignment = (VkPhysicalDeviceTexelBufferAlignmentProperties){
        .uniformTexelBufferOffsetAlignmentBytes=64, .storageTexelBufferOffsetAlignmentBytes=64,
        .uniformTexelBufferOffsetSingleTexelAlignment=VK_TRUE, .storageTexelBufferOffsetSingleTexelAlignment=VK_TRUE};
    assert(vk_tbuf_snapshot(&t, &direct) == VK_NULL_HANDLE && direct && step == 0);

    direct=0;
    V.texel_alignment.storageTexelBufferOffsetSingleTexelAlignment=VK_FALSE;
    assert(vk_tbuf_snapshot(&t, &direct) != VK_NULL_HANDLE && !direct && step == 7);
    step=0;
    t.d.width=45;
    assert(vk_tbuf_snapshot(&t, &direct) == VK_NULL_HANDLE && !direct && step == 0);
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="nx2a-texel-snapshot-") as tmp:
            src = Path(tmp) / "check.c"
            binary = src.with_suffix("")
            src.write_text(code)
            subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Werror",
                            "-I", str(DM / "third_party/vulkan-headers/include"),
                            "-I", str(DM / "gfx/nvn"), "-I", str(DM / "gfx/nvn/include"),
                            str(src), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)
