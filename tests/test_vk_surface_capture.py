"""Execute the packed surface capture against a recording Vulkan test device."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

import host_cc

ROOT = Path(__file__).resolve().parents[1]
DM = ROOT / "nxnative/dm/android"


@unittest.skipUnless(host_cc.posix(), "POSIX C compiler required")
class SurfaceCapture(unittest.TestCase):
    def test_readbacks_cover_array_and_volume_subresources(self):
        header = (DM / "gfx_vk.h").read_text()
        types = header[header.index("typedef struct { VkFormat vk;"):header.index("const vkfmt_t *vk_fmt")]
        types += header[header.index("typedef struct vkimg vkimg_t;"):header.index("vkimg_t *vk_tex(")]
        source = r'''
#define _POSIX_C_SOURCE 200809L
#define VK_NO_PROTOTYPES
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vulkan/vulkan_core.h>
#include "gfx_vk_passdump_plan.h"
#include "nxnative/dm/gfx/nvn/nvn_obj.h"
#define VKLOG(...) ((void)0)
''' + types + r'''
static struct { VkDevice dev; } V;
static struct { uint64_t draws, dispatches; } g_vkst;
static struct { int pass_open, rp_pass, rt_has_col[8], rt_has_dep, samples; struct { uint64_t key; } *sh[6]; } S;
static struct { vkimg_t *im; VkFormat fmt; VkImageSubresourceRange range; } P_att[9];
static VkRenderingInfo P_ri;
static VkRenderingAttachmentInfo P_ca[8], P_da, P_sa;
static uint8_t P_alias[8];
static VkFormat vk_view_fmt;
static VkImageSubresourceRange vk_view_range;
static uint64_t g_tex_frame = 42, g_set_epoch = 3, g_pd_bytes;
static int g_sub_pass = 8;
static unsigned copies, invalidates;
static VkBufferImageCopy captured[8];
static unsigned char memory[16 * 1024 * 1024];
static const vkfmt_t rgba = {.vk=VK_FORMAT_R8G8B8A8_UNORM, .bpb=4};
static const vkfmt_t bc = {.vk=VK_FORMAT_BC1_RGBA_UNORM_BLOCK, .compressed=1, .bpb=8, .bw=4, .bh=4};
static const vkfmt_t *vk_fmt(int fmt) { return fmt == 99 ? &bc : &rgba; }
static uint64_t pass_dump_budget(void) { return 128 * 1024 * 1024; }
static uint32_t vk_mem_type(uint32_t types, VkMemoryPropertyFlags a, VkMemoryPropertyFlags b) { return 0; }
static VkCommandBuffer vk_cmd(void) { return (VkCommandBuffer)(uintptr_t)1; }
static void vk_img_barrier_all(VkCommandBuffer cb) { assert(cb); }
static VkResult vkCreateBuffer(VkDevice d, const VkBufferCreateInfo *ci, const void *a, VkBuffer *out) {
    assert(ci->size == sizeof memory);
    *out = (VkBuffer)(uintptr_t)1;
    return VK_SUCCESS;
}
static void vkGetBufferMemoryRequirements(VkDevice d, VkBuffer b, VkMemoryRequirements *rq) {
    *rq = (VkMemoryRequirements){sizeof memory, 64, 1};
}
static VkResult vkAllocateMemory(VkDevice d, const VkMemoryAllocateInfo *ai, const void *a, VkDeviceMemory *out) {
    *out = (VkDeviceMemory)(uintptr_t)2;
    return VK_SUCCESS;
}
static VkResult vkBindBufferMemory(VkDevice d, VkBuffer b, VkDeviceMemory m, VkDeviceSize off) { return VK_SUCCESS; }
static VkResult vkMapMemory(VkDevice d, VkDeviceMemory m, VkDeviceSize o, VkDeviceSize n, unsigned flags, void **out) {
    *out = memory;
    return VK_SUCCESS;
}
static void vkDestroyBuffer(VkDevice d, VkBuffer b, const void *a) {}
static void vkFreeMemory(VkDevice d, VkDeviceMemory m, const void *a) {}
static VkResult vkInvalidateMappedMemoryRanges(VkDevice d, unsigned n, const VkMappedMemoryRange *r) {
    assert(n == 1 && r->size == VK_WHOLE_SIZE);
    invalidates++;
    return VK_SUCCESS;
}
static void vkCmdPipelineBarrier(VkCommandBuffer cb, VkPipelineStageFlags a, VkPipelineStageFlags b,
    VkDependencyFlags c, unsigned n, const VkMemoryBarrier *m, unsigned q,
    const VkBufferMemoryBarrier *y, unsigned z, const VkImageMemoryBarrier *im) {
    assert(a == VK_PIPELINE_STAGE_TRANSFER_BIT && (b & VK_PIPELINE_STAGE_HOST_BIT));
    assert(n == 1 && m->dstAccessMask == VK_ACCESS_HOST_READ_BIT);
}
static void vkCmdCopyImageToBuffer(VkCommandBuffer cb, VkImage im, VkImageLayout layout, VkBuffer b, unsigned n, const VkBufferImageCopy *r) {
    assert(n == 1 && copies < 8 && r->imageExtent.depth == 1 && layout == VK_IMAGE_LAYOUT_GENERAL);
    captured[copies++] = *r;
    memset(memory + r->bufferOffset, 0x50 + copies, 4);
}
#include "gfx_vk_surface_capture.inc"

int main(int argc, char **argv) {
    assert(argc == 2);
    setenv("DM_ROOT", argv[1], 1);
    assert(surface_capture_arm());
    vkimg_t im = {.w=96, .h=48, .d=1, .layers=6, .levels=4, .samples=1,
                  .vtype=VK_IMAGE_VIEW_TYPE_2D_ARRAY, .fmt=VK_FORMAT_R8G8B8A8_UNORM,
                  .usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT, .layout=VK_IMAGE_LAYOUT_GENERAL};
    VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, 2, 2};
    surface_read("color", &im, surface_binding(&im, NULL, rgba.vk, range, 0, -1, 0), NULL, NULL);
    assert(copies == 2 && captured[0].imageSubresource.baseArrayLayer == 2);
    assert(captured[1].imageSubresource.baseArrayLayer == 3 && captured[1].bufferOffset == 1024);
    assert(captured[0].imageOffset.x == 16 && captured[0].imageOffset.y == 4);
    im.vtype = VK_IMAGE_VIEW_TYPE_3D;
    im.layers = 1; im.d = 16;
    im.nvn_format = 99; im.fmt = bc.vk;
    range.baseArrayLayer = 4; range.layerCount = 1;
    surface_read("color", &im, surface_binding(&im, NULL, bc.vk, range, 1, -1, 0), NULL, NULL);
    assert(copies == 3 && captured[2].imageSubresource.baseArrayLayer == 0);
    assert(captured[2].imageOffset.z == 4 && captured[2].imageOffset.y % 4 == 0);
    im.samples = 4;
    surface_read("color", &im, surface_binding(&im, NULL, bc.vk, range, 2, -1, 0), NULL, NULL);
    assert(copies == 3 && invalidates == 0);
    surface_capture_write();
    assert(invalidates == 1);
}
'''
        with tempfile.TemporaryDirectory(prefix="nx2a-surfaces-") as tmp:
            root = Path(tmp)
            src = root / "surface.c"
            src.write_text(source)
            out = src.with_suffix("")
            subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-I", str(ROOT),
                            "-I", str(DM.parent / "gfx/nvn/include"),
                            "-I", str(DM), "-I", str(DM.parent / "third_party/vulkan-headers/include"),
                            str(src), "-o", str(out)], check=True)
            subprocess.run([str(out), tmp], check=True, timeout=10)
            usage = (root / "surface_usage.tsv").read_text()
            self.assertIn("tile:0:1024:16:4:16:16", usage)
            self.assertIn("multisampled", usage)
            self.assertIn("completed_samples=3", usage)
            raw = (root / "surface_samples.raw").read_bytes()
            self.assertEqual(raw[:4], b"QQQQ")
            self.assertEqual(raw[1024:1028], b"RRRR")
            self.assertEqual(raw[2048:2052], b"SSSS")
            self.assertEqual(len(raw), 2176)
