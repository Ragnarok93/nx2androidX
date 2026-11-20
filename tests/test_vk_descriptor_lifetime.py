"""Execute descriptor allocation while the registration of its handle is replaced."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

import host_cc

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(host_cc.posix(), "POSIX C compiler required")
class DescriptorLifetime(unittest.TestCase):
    def test_prewarmer_can_replace_layout_counts_during_allocation(self):
        core = (ROOT / "nxnative/dm/android/gfx_vk_core.c").read_text()
        begin = core.index("enum { DP_SETS")
        production = core[begin:core.index("// ---------------- submissions: begin", begin)]
        harness = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <vulkan/vulkan_core.h>
#include "nvn_hmap.h"
#define die(...) abort()
#define VKCHK(call) assert((call) == VK_SUCCESS)
static struct { VkDevice dev; } V;
typedef struct {
    uint32_t dp_used[9], dp_sets;
    int dp_counted, dp_cur, ndp, capdp;
    VkDescriptorPool *dp;
} vksub_t;
static vksub_t sub;
static vksub_t *cur(void) { return &sub; }
static const VkDescriptorSetLayoutBinding binding = {
    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1
};
void vk_dsl_register(VkDescriptorSetLayout, const VkDescriptorSetLayoutBinding *, uint32_t);
VkResult vkCreateDescriptorPool(VkDevice d, const VkDescriptorPoolCreateInfo *i,
    const VkAllocationCallbacks *a, VkDescriptorPool *out) {
    (void)d; (void)i; (void)a;
    *out = (VkDescriptorPool)(uintptr_t)1;
    return VK_SUCCESS;
}
VkResult vkAllocateDescriptorSets(VkDevice d, const VkDescriptorSetAllocateInfo *i, VkDescriptorSet *out) {
    (void)d;
    // The driver call supplies a deterministic point for the same update
    // that can occur when a prewarm thread registers an identical layout.
    vk_dsl_register(i->pSetLayouts[0], &binding, 1);
    *out = (VkDescriptorSet)(uintptr_t)1;
    return VK_SUCCESS;
}
''' + production + r'''
int main(void) {
    VkDescriptorSetLayout layout = (VkDescriptorSetLayout)(uintptr_t)1;
    vk_dsl_register(layout, &binding, 1);
    assert(vk_alloc_set(layout));
    assert(sub.dp_used[VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER] == 1);
    free(nvn_hm_get(&g_dsl_need, &layout));
    free(g_dsl_need.keys); free(g_dsl_need.vals); free(sub.dp);
}
'''
        with tempfile.TemporaryDirectory(prefix="nx2a-dsl-") as tmp:
            src = Path(tmp) / "lifetime.c"
            src.write_text(harness)
            out = src.with_suffix("")
            flags = ["-fsanitize=address"] if host_cc.asan_ubsan("cc") else []
            subprocess.run(["cc", "-std=c11", "-pthread", *flags,
                            "-I", str(ROOT / "nxnative/dm/third_party/vulkan-headers/include"),
                            "-I", str(ROOT / "nxnative/dm/gfx/nvn"), str(src), "-o", str(out)],
                           check=True)
            result = subprocess.run([str(out)], text=True, capture_output=True, timeout=20,
                                    env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
