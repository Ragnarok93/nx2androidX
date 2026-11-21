"""gfx_vk_tex.c ds_texels: a depth / stencil ReadTexels rebuilt in the guest's layout from the aspects' buffer copies, cut from the source."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import host_cc   # noqa: E402

TEX = Path(__file__).resolve().parents[1] / 'nxnative/dm/android/gfx_vk_tex.c'
HARNESS = r'''
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "gfx_vk_d24_copy.h"

static struct { VkPhysicalDevice pd; } V;
static VkFormatFeatureFlags d24_features, d32_features;
static VkResult d24_result = VK_SUCCESS, d32_result = VK_SUCCESS;
static unsigned queries;
static void vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice pd, VkFormat fmt, VkFormatProperties *props) {
    (void)pd;
    *props = (VkFormatProperties){.optimalTilingFeatures = fmt == VK_FORMAT_D24_UNORM_S8_UINT ? d24_features : d32_features};
}
static VkResult vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice pd, VkFormat fmt, VkImageType type,
        VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties *props) {
    (void)pd; (void)props;
    assert(type == VK_IMAGE_TYPE_2D && tiling == VK_IMAGE_TILING_OPTIMAL && flags == 0);
    assert(usage == (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT));
    queries++;
    return fmt == VK_FORMAT_D24_UNORM_S8_UINT ? d24_result : d32_result;
}
CUT
int main(void) {
    // the depth aspect's copy: 4 bytes a texel (floats, or D24 in the low 24 bits); the stencil aspect's: 1 byte a texel
    float df[3] = {0.25f, 1.0f, 0.0f}; uint8_t st[3] = {7, 0xff, 0x80}, out[24];
    memset(out, 0xee, sizeof out); ds_texels(0x36, VK_FORMAT_D32_SFLOAT_S8_UINT, (const uint8_t *)df, st, 3, out);   // DEPTH32F_STENCIL8: 8 bytes a texel
    for (int i = 0; i < 3; i++) { float x; memcpy(&x, out + 8 * i, 4); assert(x == df[i] && out[8 * i + 4] == st[i] && !out[8 * i + 5] && !out[8 * i + 6] && !out[8 * i + 7]); }
    uint32_t d24[3] = {0x00123456, 0xff000001, 0x00ffffff}, w[3];   // (the copy's top byte undefined: 0xff in the second)
    ds_texels(0x35, VK_FORMAT_D24_UNORM_S8_UINT, (const uint8_t *)d24, st, 3, (uint8_t *)w);   // DEPTH24_STENCIL8: stencil in the top byte
    assert(w[0] == 0x07123456 && w[1] == 0xff000001 && w[2] == 0x80ffffff);
    memset(out, 0, sizeof out); ds_texels(0x31, VK_FORMAT_D24_UNORM_S8_UINT, NULL, st, 3, out); assert(!memcmp(out, st, 3) && !out[3]);   // STENCIL8 in a combined image
    ds_texels(0x33, VK_FORMAT_X8_D24_UNORM_PACK32, (const uint8_t *)d24, NULL, 3, (uint8_t *)w); assert(w[0] == 0x123456 && w[1] == 1 && w[2] == 0xffffff);   // DEPTH24
    float dx[4] = {0.5f, 1.5f, -1.0f, 0.0f / 0.0f}; uint32_t w4[4];
    ds_texels(0x33, VK_FORMAT_D32_SFLOAT, (const uint8_t *)dx, NULL, 4, (uint8_t *)w4); assert(w4[0] == 0x800000 && w4[1] == 0xffffff && !w4[2] && !w4[3]);   // DEPTH24 kept as D32F
    uint16_t d16[2] = {0x1234, 0xfedc}, o16[2]; ds_texels(0x32, VK_FORMAT_D16_UNORM, (const uint8_t *)d16, NULL, 2, (uint8_t *)o16); assert(o16[0] == 0x1234 && o16[1] == 0xfedc);
    ds_texels(0x34, VK_FORMAT_D32_SFLOAT, (const uint8_t *)df, NULL, 3, (uint8_t *)w); assert(!memcmp(w, df, sizeof df));   // DEPTH32F
    ds_texels(0x36, VK_FORMAT_D32_SFLOAT_S8_UINT, NULL, NULL, 3, out); assert(!out[0] && !out[4]);   // an aspect the image lacks reads 0
    const VkFormatFeatureFlags all = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    d24_features = d32_features = all; queries = 0;
    assert(vk_d24s8_attachment_format() == VK_FORMAT_D24_UNORM_S8_UINT && queries == 1);
    d24_features = 0; queries = 0;
    assert(vk_d24s8_attachment_format() == VK_FORMAT_D32_SFLOAT_S8_UINT && queries == 1);
    d24_features = all; d24_result = VK_ERROR_FORMAT_NOT_SUPPORTED; queries = 0;
    assert(vk_d24s8_attachment_format() == VK_FORMAT_D32_SFLOAT_S8_UINT && queries == 2);
    d32_result = VK_ERROR_FORMAT_NOT_SUPPORTED;
    assert(vk_d24s8_attachment_format() == VK_FORMAT_UNDEFINED);

    uint32_t guest[] = {0x5a000000, 0x079fffff, 0xff000001, 0x81fffffe, 0x80ffffff};
    uint8_t stencil[5]; uint32_t host[5], roundtrip[5];
    memcpy(host, guest, sizeof guest);
    vk_d24s8_unpack_upload((uint8_t *)host, stencil, 5);
    assert(stencil[0] == 0x5a && stencil[1] == 7 && stencil[2] == 0xff && stencil[3] == 0x81 && stencil[4] == 0x80);
    ds_texels(0x35, VK_FORMAT_D32_SFLOAT_S8_UINT, (uint8_t *)host, stencil, 5, (uint8_t *)roundtrip);
    assert(!memcmp(roundtrip, guest, sizeof guest));
    puts("ds ok"); return 0;
}
'''


@unittest.skipUnless(shutil.which('cc') and host_cc.posix(), 'a POSIX host C compiler is required')
class DepthStencilReadback(unittest.TestCase):
    def test_guest_layouts(self):
        headers = TEX.parents[1] / 'third_party/vulkan-headers/include'
        src = TEX.read_text()
        cut = ''
        for name in ('vk_d24s8_attachment_format', 'vk_d24s8_unpack_upload', 'ds_texels'):
            start = src.rindex('\nstatic ', 0, src.index(f' {name}('))
            cut += src[start:src.index('\n}\n', start) + 3]
        with tempfile.TemporaryDirectory(prefix='nx2a-ds-') as tmp:
            c, exe = Path(tmp) / 'check.c', Path(tmp) / 'check'
            c.write_text(HARNESS.replace('CUT', cut))
            subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-I', str(headers), '-I', str(TEX.parent), str(c), '-o', str(exe)], check=True)
            out = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
            self.assertEqual(out.returncode, 0, out.stderr)
            self.assertIn('ds ok', out.stdout)


if __name__ == '__main__':
    unittest.main()
