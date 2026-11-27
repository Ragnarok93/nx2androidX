"""Compile the production replay source proof and submission ownership hooks."""
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
DM = ROOT / "nxnative/dm"
ANDROID = DM / "android"


def replay_function(code, signature):
    start = code.index(signature)
    return code[start:code.index("\n}", start) + 2]


class GuestTemporalReplay(unittest.TestCase):
    def test_source_and_submission_contracts(self):
        code = (ANDROID / "gfx_vk_exec.c").read_text()
        end = code.index("} guest_flow;") + len("} guest_flow;")
        flow = code[code.rindex("static struct {", 0, end):end]
        memo = next(line for line in code.splitlines() if line.endswith("} set_memo_t;"))
        source = r'''
#define VK_NO_PROTOTYPES
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <vulkan/vulkan_core.h>
#include "gfx_vk_flow_budget.h"
#include "gfx_vk_gsr2.h"
#include "spatial_upscaler.h"

typedef struct {
    VkImage img;
    VkFormat fmt;
    VkImageLayout layout;
    uint32_t w, h, d, levels, layers, samples, novl;
    VkImageUsageFlags usage;
    uint64_t pool, wseq, guest_clear_write, guest_clear_frame, guest_clear_submit;
    void *rs_backing, *bc_of;
} vkimg_t;
static struct { vkimg_t *im; uint32_t w, h; } P_att[1];
static VkRenderingInfo P_ri;
static VkRenderingAttachmentInfo P_ca[1];
static struct { VkImage image; VkExtent2D extent; } g_guest_scene;
static int g_guest_source_ok;
static uint64_t g_tex_frame, g_guest_sharpen_pending, g_guest_sharpen_submitted, g_guest_temporal_submitted;
static uint64_t g_guest_rejected_submit, g_guest_clear_revision = 1;
static uint64_t submitted = 4;
static uint64_t mtl_last_fence(void) { return submitted; }
__thread VkFormat vk_view_fmt;
__thread VkComponentMapping vk_view_swizzle;
__thread VkImageSubresourceRange vk_view_range;

typedef struct { const unsigned *b; unsigned ndyn; } vkshader_t;
enum { MEMO_CB = 16 };
static uint64_t g_set_epoch, g_bind_gen[6], g_tex_gen, g_samp_gen, g_ubo_gen[6], g_ubd_gen;
static uint64_t g_num_um, g_num_im, g_num_r32;
static uint32_t g_draw_binst;
static uint32_t vk_pool_gen(void) { return 0; }
static void cbuf_desc(int st, vkshader_t *s, const unsigned *b, VkDescriptorBufferInfo *out) {
    (void)st; (void)s; (void)b; (void)out; assert(0);
}

static struct { VkDevice dev; } V;
static int mode = SP_SGSR2, commits, last_commit, drops, query_drops;
static uint64_t completed;
static uint64_t vk_fence_done(void) { return completed; }
int dm_spatial_mode(void) { return mode; }
void vk_gsr2_commit(vk_gsr2 *ctx, int accepted) { assert(ctx); ++commits; last_commit = accepted; }
void vk_gsr2_destroy(vk_gsr2 *ctx) { assert(ctx); ++drops; }
static void vkDestroyQueryPool(VkDevice dev, VkQueryPool pool, const VkAllocationCallbacks *alloc) {
    assert(dev == V.dev && pool && !alloc); ++query_drops;
}
'''
        source += "\n".join((
            flow,
            memo,
            "static set_memo_t g_smemo[3];",
            replay_function(code, "static int set_memo_get("),
            replay_function(code, "static void guest_store_clear("),
            replay_function(code, "static void guest_check_source("),
            replay_function(code, "static void guest_flow_drop("),
            replay_function(code, "unsigned vk_guest_reconstruction_flags("),
            replay_function(code, "void vk_guest_flow_submitted("),
        ))
        source += r'''

static int source_admitted(const vkimg_t *image) {
    g_guest_scene.image = VK_NULL_HANDLE;
    guest_check_source(image, (VkImageView)(uintptr_t)1);
    return g_guest_scene.image != VK_NULL_HANDLE;
}

static void source_proof(void) {
    vkimg_t scene = {.img = (VkImage)(uintptr_t)1, .fmt = VK_FORMAT_R8G8B8A8_UNORM,
        .layout = VK_IMAGE_LAYOUT_GENERAL, .w = 1728, .h = 972,
        .d = 1, .levels = 1, .layers = 1, .samples = 1, .pool = 1, .wseq = 20,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT};
    vkimg_t dst = {.img = (VkImage)(uintptr_t)2, .pool = 2};
    g_tex_frame = 10;
    P_att[0].im = &scene;
    P_ri = (VkRenderingInfo){.renderArea = {{0, 0}, {1728, 971}}, .layerCount = 1,
        .colorAttachmentCount = 1};
    P_ca[0] = (VkRenderingAttachmentInfo){.imageView = (VkImageView)(uintptr_t)1,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE};
    guest_store_clear();
    assert(!scene.guest_clear_write);
    P_ri.renderArea.extent.height = 972;
    P_ca[0].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    guest_store_clear();
    assert(!scene.guest_clear_write);
    P_ca[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    guest_store_clear();
    assert(scene.guest_clear_write == 20 && scene.guest_clear_frame == 11);
    assert(scene.guest_clear_submit == 5 && g_guest_clear_revision == 2);

    P_att[0].im = &dst;
    P_att[0].w = 1920;
    P_att[0].h = 1080;
    vk_view_fmt = scene.fmt;
    vk_view_range = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    assert(source_admitted(&scene));
    assert(g_guest_scene.extent.width == 1728 && g_guest_scene.extent.height == 972);

    vk_view_swizzle.r = VK_COMPONENT_SWIZZLE_B;
    assert(!source_admitted(&scene) && g_guest_source_ok);
    vk_view_swizzle.r = VK_COMPONENT_SWIZZLE_R;
    assert(source_admitted(&scene));
    vk_view_fmt = VK_FORMAT_R8G8B8A8_SRGB;
    assert(!source_admitted(&scene));
    vk_view_fmt = scene.fmt;
    vk_view_range.baseMipLevel = 1;
    assert(!source_admitted(&scene));
    vk_view_range.baseMipLevel = 0;
    scene.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    assert(!source_admitted(&scene) && g_guest_source_ok);
    scene.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    ++scene.wseq;
    assert(!source_admitted(&scene));
    scene.wseq = scene.guest_clear_write;
    scene.novl = 1;
    assert(!source_admitted(&scene));
    scene.novl = 0;
    dst.pool = scene.pool;
    assert(!source_admitted(&scene));
    dst.pool = 2;
    assert(source_admitted(&scene));
    submitted = 5;
    vk_guest_flow_submitted(0, submitted);
    assert(!source_admitted(&scene));
    P_att[0].im = &scene;
    guest_store_clear();
    P_att[0].im = &dst;
    assert(scene.guest_clear_submit == 6 && source_admitted(&scene));
    ++g_tex_frame;
    assert(!source_admitted(&scene));
}

static void submission_ownership(void) {
    guest_flow.ctx = (vk_gsr2 *)(uintptr_t)1;
    guest_flow.queries = (VkQueryPool)(uintptr_t)1;
    guest_flow.slot = 2;
    guest_flow.active = 1;

    guest_flow.pending = 1;
    guest_flow.slots[2].ticket = 11;
    vk_guest_flow_submitted(1, 7);
    assert(commits == 1 && !last_commit && guest_flow.slots[2].fence == 7);
    assert(!guest_flow.slots[2].ticket && !guest_flow.pending);

    guest_flow.pending = guest_flow.drawn = 1;
    g_guest_sharpen_pending = g_tex_frame + 1;
    vk_guest_flow_submitted(0, 8);
    assert(commits == 2 && !last_commit && !vk_guest_reconstruction_flags());

    guest_flow.pending = guest_flow.drawn = 1;
    guest_flow.ticket = guest_flow.slots[2].ticket = g_tex_frame + 1;
    g_guest_sharpen_pending = g_tex_frame + 1;
    vk_guest_flow_submitted(1, 9);
    assert(commits == 3 && last_commit &&
           vk_guest_reconstruction_flags() == (SP_REPLAY_SHARPENED | SP_REPLAY_TEMPORAL));
    assert(guest_flow.slots[2].ticket == g_tex_frame + 1 && guest_flow.slots[2].fence == 9);
    vk_guest_flow_submitted(1, 10);
    assert(commits == 3 && guest_flow.retire == 9);
    ++g_tex_frame;
    assert(!vk_guest_reconstruction_flags());

    mode = SP_FSR1;
    completed = 8;
    vk_guest_flow_submitted(1, 11);
    assert(guest_flow.ctx && guest_flow.epoch == 1 && !drops);
    completed = 9;
    vk_guest_flow_submitted(1, 12);
    assert(!guest_flow.ctx && guest_flow.epoch == 1 && drops == 1 && query_drops == 1);
}

static void memo_clear_revision(void) {
    vkshader_t s = {.ndyn = 1};
    uint32_t offsets[1] = {0};
    g_smemo[0] = (set_memo_t){.s = &s, .clear_revision = g_guest_clear_revision, .dofs = {32}};
    assert(set_memo_get(0, &s, offsets) && offsets[0] == 32);
    ++g_guest_clear_revision;
    assert(!set_memo_get(0, &s, offsets));
    g_smemo[0].clear_revision = 0;
    assert(set_memo_get(0, &s, offsets) && offsets[0] == 32);
}

int main(void) {
    source_proof();
    memo_clear_revision();
    submission_ownership();
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            # The replay fixture only uses the Vulkan core declarations.
            headers = root / "vulkan"
            headers.mkdir()
            (headers / "vulkan.h").write_text("#include <vulkan/vulkan_core.h>\n")
            fixture = root / "guest_replay.c"
            executable = fixture.with_suffix("")
            fixture.write_text(source)
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(root), "-I", str(ANDROID), "-I", str(DM / "third_party/vulkan-headers/include"),
                str(fixture), "-o", str(executable),
            ], check=True)
            subprocess.run([str(executable)], check=True, timeout=10)
