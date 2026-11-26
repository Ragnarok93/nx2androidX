"""Bounded two-submission scaling window and descriptor version contract."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

class WindowTests(unittest.TestCase):
    def test_window_contract(self):
        cc=shutil.which("cc") or shutil.which("clang")
        if not cc:self.skipTest("C compiler required")
        with tempfile.TemporaryDirectory() as d:
            exe=Path(d)/"window"
            subprocess.run([cc,"-std=c11","-Wall","-Wextra","-Werror",
                "-I",str(ROOT/"nxnative/dm/gfx/nvn"),
                str(ROOT/"tests/resolution_window.c"),"-o",str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_production_seams(self):
        base=ROOT/"nxnative/dm"
        hle=(base/"gfx/nvn/hle_nvn.c").read_text()
        tex=(base/"android/gfx_vk_tex.c").read_text()
        replay=(base/"android/gfx_vk_exec.c").read_text()
        self.assertIn("nx_rs_window_handles(window",hle)
        self.assertIn("vk_rs_scope_cross_submission_active()",hle)
        self.assertIn("mtl_exec_rest(h+n,window_n-n)",hle)
        self.assertIn("nx_rs_window_descriptor(job,nvn_rs_window_last_job()",tex)
        self.assertIn("while (g_rs_sync_freeze) pthread_cond_wait",tex)
        self.assertIn("return g_rs_scope_end!=NULL;",replay)
