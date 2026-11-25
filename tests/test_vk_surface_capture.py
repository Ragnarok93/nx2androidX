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
        replay = (DM / "gfx_vk_exec.c").read_text()
        dump_type = replay[replay.index("typedef struct { VkBuffer b; VkDeviceMemory m; void *p;"):
                           replay.index("enum { PD_MAX = 96 }")]
        host_barrier = replay[replay.index("static void pass_dump_host_barrier("):
                              replay.index("// Known-value depth transfer control:")]
        full_capture = host_barrier + replay[replay.index("static int pass_dump_color_preview("):
                              replay.index("static void pass_dump_effects(")]
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
static struct { int pass_open, rp_pass, rt_has_col[8], rt_has_dep, samples, ncol, pass_w, pass_h;
    view_t rt_cview[8], rt_dview; uint8_t rt_has_cview[8], rt_has_dview; struct { uint64_t key; } *sh[6]; } S;
static struct { vkimg_t *im; VkFormat fmt; VkImageSubresourceRange range; } P_att[9];
static VkRenderingInfo P_ri;
static VkRenderingAttachmentInfo P_ca[8], P_da, P_sa;
static uint8_t P_alias[8];
static VkFormat vk_view_fmt;
static VkImageSubresourceRange vk_view_range;
static uint64_t g_tex_frame = 42, g_set_epoch = 3, g_pd_bytes;
static int g_sub_pass = 8;
static unsigned copies, invalidates;
static VkBufferImageCopy captured[32];
static VkDeviceSize requested_size;
static unsigned char memory[16 * 1024 * 1024];
static const vkfmt_t rgba = {.vk=VK_FORMAT_R8G8B8A8_UNORM, .bpb=4};
static const vkfmt_t bc = {.vk=VK_FORMAT_BC1_RGBA_UNORM_BLOCK, .compressed=1, .bpb=8, .bw=4, .bh=4};
static const vkfmt_t *vk_fmt(int fmt) { return fmt == 99 ? &bc : &rgba; }
static uint64_t pass_dump_budget(void) { return 128 * 1024 * 1024; }
static uint32_t vk_mem_type(uint32_t types, VkMemoryPropertyFlags a, VkMemoryPropertyFlags b) { return 0; }
static VkCommandBuffer vk_cmd(void) { return (VkCommandBuffer)(uintptr_t)1; }
static void vk_img_barrier_all(VkCommandBuffer cb) { assert(cb); }
static VkResult vkCreateBuffer(VkDevice d, const VkBufferCreateInfo *ci, const void *a, VkBuffer *out) {
    requested_size = ci->size;
    *out = (VkBuffer)(uintptr_t)1;
    return VK_SUCCESS;
}
static void vkGetBufferMemoryRequirements(VkDevice d, VkBuffer b, VkMemoryRequirements *rq) {
    *rq = (VkMemoryRequirements){requested_size, 64, 1};
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
    assert(n == 1 && copies < 32 && r->imageExtent.depth == 1 && layout == VK_IMAGE_LAYOUT_GENERAL);
    captured[copies++] = *r;
    memset(memory + r->bufferOffset, 0x50 + copies, 4);
}
static unsigned uniform_copies;
static void vkCmdCopyBuffer(VkCommandBuffer cb, VkBuffer src, VkBuffer dst, unsigned n, const VkBufferCopy *r) {
    assert(n == 1 && r->srcOffset == 256 && r->size == 4096);
    memset(memory + r->dstOffset, 0xa8, r->size);
    uniform_copies++;
}
static const char *vk_pass_dump_root(void) { return getenv("DM_ROOT"); }
#include "gfx_vk_surface_capture.inc"
''' + dump_type + r'''
static pdump_t g_pd[96];
static int g_npd, g_pd_full=1, g_pd_prepass_written, g_pd_mrt_written;
static int g_pd_effect_passes, g_pd_small_effect_passes;
static uint32_t g_pd_scene_w, g_pd_scene_h;
static uint64_t g_pass_draws=12;
static char g_pd_size[128];
static int pass_dump_limit(void) { return 96; }
''' + full_capture + r'''
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
    surface_uniform_queue(0x1234, 8, 2, (VkDescriptorBufferInfo){(VkBuffer)(uintptr_t)2, 256, 8192});
    surface_uniform_queue(0x1234, 9, 3, (VkDescriptorBufferInfo){(VkBuffer)(uintptr_t)2, 1, 4096});
    surface_compute_uniforms(vk_cmd());
    assert(uniform_copies == 1 && g_sc.uniform_count == 0);
    surface_capture_write();
    assert(invalidates == 1);

    // The menu capture runs first depth/stencil, an MRT, and a later effect.
    vkimg_t ds = {.w=96, .h=48, .d=1, .layers=1, .levels=1, .samples=1,
        .fmt=VK_FORMAT_D32_SFLOAT_S8_UINT, .usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .layout=VK_IMAGE_LAYOUT_GENERAL, .aspect=VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT};
    P_da.storeOp = P_sa.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    P_ri.pDepthAttachment = &P_da;
    P_ri.pStencilAttachment = &P_sa;
    P_att[8].im = &ds;
    P_att[8].fmt = ds.fmt;
    pass_dump_full_attachments();
    assert(g_npd == 2 && g_pd[0].target == -1 && g_pd[1].target == -3);
    assert(g_pd[0].bytes == 96*48*4 && g_pd[1].bytes == 96*48);

    vkimg_t color = ds;
    color.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    color.fmt = VK_FORMAT_R8G8B8A8_UNORM;
    S.ncol = 3; S.pass_w = 96; S.pass_h = 48;
    for (int i=0;i<3;i++) {
        P_att[i].im = &color; P_att[i].fmt = color.fmt; P_alias[i] = i;
        P_ca[i].imageView = (VkImageView)(uintptr_t)(i+1);
        P_ca[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    }
    pass_dump_full_attachments();
    assert(g_npd == 5 && g_pd[2].target == 0 && g_pd[4].target == 2);
    // A run of small dual-output fog passes must leave capacity for
    // subsequent scene-resolution composition and translucency.
    vkimg_t fog = color;
    fog.w = fog.h = 16;
    S.ncol = 2; S.pass_w = S.pass_h = 16;
    P_att[0].im = P_att[1].im = &fog;
    for (int i=0;i<8;i++) pass_dump_full_attachments();
    assert(g_npd == 9 && g_pd_small_effect_passes == 2);
    assert(g_pd[5].target == 0 && g_pd[6].target == 1);
    assert(g_pd[5].w == 16 && g_pd[6].h == 16);

    S.pass_w = 96; S.pass_h = 48;
    P_att[0].im = P_att[1].im = &color;
    pass_dump_full_attachments();
    assert(g_npd == 11 && g_pd_effect_passes == 1);
    assert(g_pd[9].target == 0 && g_pd[10].target == 1);
    assert(g_pd[9].w == 96 && g_pd[10].h == 48);

    // Scene images must leave space for a source image and its reported crop.
    g_pd_bytes = pass_dump_budget() - 16u*1024u*1024u;
    assert(!pass_dump_color(0));
    const int crop[4] = {2, 4, 90, 40};
    assert(pass_dump_image(&color,color.fmt,color.aspect,0,0,-202,-4,crop));
    assert(g_npd == 12 && captured[copies-1].imageExtent.width == 96);
    assert(g_pd[11].target == -4 && g_pd[11].crop[0] == 2 && g_pd[11].crop[3] == 40);
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
            self.assertIn("completed_samples=4", usage)
            self.assertIn("compute-uniform", usage)
            self.assertIn("ubo:2176:4096", usage)
            raw = (root / "surface_samples.raw").read_bytes()
            self.assertEqual(raw[:4], b"QQQQ")
            self.assertEqual(raw[1024:1028], b"RRRR")
            self.assertEqual(raw[2048:2052], b"SSSS")
            self.assertEqual(raw[2176:], b"\xa8" * 4096)
