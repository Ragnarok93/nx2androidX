"""Vulkan low-memory policy: native C thresholds and safe cache retirement."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "nxnative/dm/android/gfx_vk_core.c"
HEADER = ROOT / "nxnative/dm/android/gfx_vk_pool_pressure.h"

class PoolPressureTests(unittest.TestCase):
    def test_policy_compiles_and_handles_phone_pressure(self):
        cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
        if not cc:
            self.skipTest("C compiler unavailable")
        program = """
#include <stdint.h>
#include <assert.h>
#include "gfx_vk_pool_pressure.h"
int main(void) {
    uint64_t t = 8ull * 1024 * 1024;
    assert(vk_pool_reuse_budget_bytes(2ull * 1024 * 1024, t) == (384ull << 20));
    assert(vk_pool_reuse_budget_bytes(1400ull * 1024, t) == (64ull << 20));
    assert(vk_pool_reuse_budget_bytes(800ull * 1024, t) == 0);
    assert(vk_pool_reuse_budget_bytes(2ull * 1024 * 1024, 0) == (384ull << 20));
    assert(vk_pool_reuse_budget_bytes(500ull * 1024, 4ull * 1024 * 1024) == 0);
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "policy.c"
            binary = Path(tmp) / "policy"
            source.write_text(program)
            subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-I", str(HEADER.parent), str(source), "-o", str(binary)],
                           check=True, capture_output=True, text=True)
            subprocess.run([str(binary)], check=True, capture_output=True, text=True)

    def test_cache_trim_requires_completed_gpu_fence(self):
        text = CORE.read_text(encoding="utf-8")
        trim = text[text.index("static void zcf_trim_to("):text.index("static void *zcf_pressure_main(")]
        self.assertIn("vk_fence_done()", trim)
        self.assertIn("if (g_zcf[i].fence > done)", trim)
        self.assertLess(trim.index("pthread_mutex_unlock(&g_zcf_mu)"),
                        trim.index("zcf_free_now(&old[i])"))
        self.assertIn("zcf_pressure_start();", text)
        self.assertIn("DM_VK_POOL_PRESSURE_TRIM", text)
        monitor = text[text.index("static void *zcf_pressure_main("):]
        self.assertIn("vk_pool_reuse_budget_update(&recovery,", monitor)
        self.assertIn("available,total);", monitor)
        self.assertIn("if (budget < ZCF_BYTES) zcf_trim_to(budget);", monitor)
        self.assertIn("reuse cache: %.0f MB retained", text)

if __name__ == "__main__":
    unittest.main()
