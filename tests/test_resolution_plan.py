"""Compile the resolution planner and check that its runtime seams are wired."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
class ResolutionPlanTests(unittest.TestCase):
    def test_planner(self):
        cc = shutil.which("cc") or shutil.which("clang")
        if not cc:
            self.skipTest("C compiler required")
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "test"
            subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT / "nxnative/dm/gfx/nvn"),
                            str(ROOT / "tests/resolution_plan.c"), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_live_integration(self):
        replay = (ROOT / "nxnative/dm/android/gfx_vk_exec.c").read_text()
        nvn = (ROOT / "nxnative/dm/gfx/nvn/hle_nvn.c").read_text()
        self.assertIn('resolution_observe_pass();', replay)
        self.assertIn('DM_VK_RESOLUTION_AUDIT', replay)
        self.assertIn('nx_rs_new_sample(&timing, measured_frame)', nvn)
        self.assertIn('if (!dm_render_scale_supported())', nvn)

if __name__ == "__main__":
    unittest.main()
