"""P1 private variant budget and retirement contract."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class ResolutionBudgetTests(unittest.TestCase):
    def test_budget(self):
        cc = shutil.which("cc") or shutil.which("clang")
        if not cc:
            self.skipTest("C compiler required")
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "test"
            subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT / "nxnative/dm/gfx/nvn"),
                            str(ROOT / "tests/resolution_budget.c"), "-o", str(out)], check=True)
            subprocess.run([str(out)], check=True)
    def test_probe_and_retirement(self):
        android = ROOT / "nxnative/dm/android"
        tex = (android / "gfx_vk_tex.c").read_text()
        core = (android / "gfx_vk_core.c").read_text()
        replay = (android / "gfx_vk_exec.c").read_text()
        self.assertIn("nx_rs_reserve(&g_rs_budget, req.size", tex)
        self.assertIn("nx_rs_release(&g_rs_budget, v->bytes)", tex)
        self.assertIn("df_add(5, v, NULL)", core)
        self.assertIn("d->kind == 5) vk_rs_variant_destroy", core)
        self.assertIn("DM_VK_RESOLUTION_PROBE", replay)
        self.assertIn("vk_rs_variant_destroy(staged.slot[i])", tex)
        self.assertIn("if (!changed) return 1;", tex)
        self.assertIn("if (old) vk_rs_variant_retire(old);", tex)
        self.assertIn("vk_rs_cache_exchange(owners, half.w, half.h)", replay)
        self.assertIn("if (g_rs_observe) resolution_observe_pass(); // before opening", replay)
        self.assertIn("g_rs_observe = g_rs_audit || g_rs_probe;", replay)
        self.assertIn("g_rs_observe = g_rs_audit;", replay)
if __name__ == "__main__":
    unittest.main()
