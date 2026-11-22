"""Exercise production depth/stencil pass transitions and clear handling."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

DM = Path(__file__).resolve().parents[1] / "nxnative/dm"


@unittest.skipUnless(shutil.which("cc"), "host C compiler required")
class DepthStore(unittest.TestCase):
    def test_read_only_depth_pass_preserves_clears_and_write_transitions(self):
        replay = (DM / "android/gfx_vk_exec.c").read_text()
        plan = replay[replay.index("static int depth_stencil_read_only("):
                      replay.index("static VkRect2D scissor_rect(")]
        clear = replay[replay.index("static void clear_ds("):
                       replay.index("// ---------------- formats and enums")]
        source = r'''
#define VK_NO_PROTOTYPES
#include <assert.h>
#include <vulkan/vulkan_core.h>
#include "nvn_obj.h"
#define VKLOG(...) ((void)0)
static struct { VkPhysicalDeviceProperties props; } V;
static struct { ds_st ds; int pass_open, swmask[2]; VkImageAspectFlags daspect; } S;
static struct { VkImageUsageFlags usage; } depth = {VK_IMAGE_USAGE_SAMPLED_BIT};
static struct { typeof(depth) *im; } P_att[9];
static VkRenderingAttachmentInfo P_da, P_sa;
static VkRenderingInfo P_ri = {.layerCount = 1};
static struct { unsigned folded; } g_vkst;
static int g_x_depth, g_x_tracepass, g_sub_pass, P_cleared;
static VkAttachmentStoreOp store[16];
static VkAttachmentLoadOp load[16];
static unsigned begins, ends, clears;
static int begin_pass(void) {
    if (!S.pass_open) {
        P_att[8].im = &depth;
        P_da = P_sa = (VkRenderingAttachmentInfo){
            .imageView = (VkImageView)1,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        };
        P_cleared = 0;
        S.pass_open = 2;
    }
    return 1;
}
static int pass_commit(void) {
    if (S.pass_open == 2) {
        assert(P_da.storeOp == P_sa.storeOp && begins < 16);
        store[begins] = P_da.storeOp;
        load[begins++] = P_da.loadOp;
        S.pass_open = 1;
    }
    return S.pass_open == 1;
}
static void vk_end_pass(void) {
    if (S.pass_open == 2 && P_cleared) pass_commit();
    if (S.pass_open == 1) ends++;
    S.pass_open = 0;
}
static VkRect2D scissor_rect(void) { return (VkRect2D){{0,0},{128,128}}; }
static int pass_whole(VkRect2D rect) { return rect.extent.width == 128; }
static VkCommandBuffer vk_cmd(void) { return (VkCommandBuffer)1; }
static void vkCmdClearAttachments(VkCommandBuffer cb, uint32_t n, const VkClearAttachment *a,
                                   uint32_t nr, const VkClearRect *r) {
    assert(cb && n == 1 && nr == 1 && P_da.storeOp == VK_ATTACHMENT_STORE_OP_STORE);
    clears++;
}
''' + plan + clear + r'''
int main(void) {
    V.props.apiVersion = VK_API_VERSION_1_3;
    S.daspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    S.ds.dt = S.ds.st = 1;
    S.swmask[0] = S.swmask[1] = 0xff;
    S.ds.fail_f = S.ds.zfail_f = S.ds.zpass_f = NVN_STENCIL_OP_KEEP;
    S.ds.fail_b = S.ds.zfail_b = S.ds.zpass_b = NVN_STENCIL_OP_KEEP;
    p_clear_ds c = {.depth_mask = 1};
    clear_ds(&c);
    assert(S.pass_open == 2 && P_cleared && begins == 0);
    assert(begin_draw_pass() && begins == 2 && ends == 1);
    assert(store[0] == VK_ATTACHMENT_STORE_OP_STORE && load[0] == VK_ATTACHMENT_LOAD_OP_CLEAR);
    assert(store[1] == VK_ATTACHMENT_STORE_OP_NONE && load[1] == VK_ATTACHMENT_LOAD_OP_LOAD);
    assert(begin_draw_pass() && begins == 2);

    S.ds.dw = 1;
    assert(begin_draw_pass() && begins == 3 && store[2] == VK_ATTACHMENT_STORE_OP_STORE);
    S.ds.dw = 0;
    S.ds.zpass_b = NVN_STENCIL_OP_REPLACE;
    assert(begin_draw_pass() && begins == 3);
    S.swmask[1] = 0;
    assert(begin_draw_pass() && begins == 4 && store[3] == VK_ATTACHMENT_STORE_OP_NONE);

    clear_ds(&c);
    assert(P_cleared && S.pass_open == 2);
    assert(begin_draw_pass() && begins == 6 && load[4] == VK_ATTACHMENT_LOAD_OP_CLEAR);
    assert(store[4] == VK_ATTACHMENT_STORE_OP_STORE && store[5] == VK_ATTACHMENT_STORE_OP_NONE);

    // A masked stencil clear cannot fold and needs a writable pass.
    c = (p_clear_ds){.stencil_mask = 1};
    clear_ds(&c);
    assert(clears == 1 && begins == 7 && store[6] == VK_ATTACHMENT_STORE_OP_STORE);
    vk_end_pass();
    V.props.apiVersion = VK_API_VERSION_1_2;
    assert(begin_draw_pass() && begins == 8 && store[7] == VK_ATTACHMENT_STORE_OP_STORE);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "depth_store.c"
            exe = src.with_suffix("")
            src.write_text(source)
            subprocess.run(["cc", "-std=gnu11", "-I", str(DM / "third_party/vulkan-headers/include"),
                            "-I", str(DM / "gfx/nvn"), "-I", str(DM / "gfx/nvn/include"),
                            str(src), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True, timeout=10)
