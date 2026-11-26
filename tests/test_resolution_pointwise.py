"""Host proof and production seams for same-grid pointwise dispatches."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

class PointwiseTests(unittest.TestCase):
    def test_structural_proof(self):
        cc=shutil.which("cc") or shutil.which("clang")
        if not cc:self.skipTest("C compiler required")
        with tempfile.TemporaryDirectory() as d:
            exe=Path(d)/"pointwise"
            subprocess.run([cc,"-std=c11","-Wall","-Wextra","-Werror",
                "-I",str(ROOT/"nxnative/dm/gfx/nvn"),
                str(ROOT/"tests/resolution_pointwise.c"),"-o",str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_storage_formats(self):
        cc=shutil.which("cc") or shutil.which("clang")
        if not cc:self.skipTest("C compiler required")
        with tempfile.TemporaryDirectory() as d:
            exe=Path(d)/"storage_formats"
            subprocess.run([cc,"-std=c11","-Wall","-Wextra","-Werror",
                "-I",str(ROOT/"nxnative/dm/gfx/nvn"),
                str(ROOT/"tests/resolution_storage_format.c"),"-o",str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_production_seams(self):
        base=ROOT/"nxnative/dm/android"
        scope=(base/"gfx_vk_resolution_scope.inc").read_text()
        replay=(base/"gfx_vk_exec.c").read_text()
        self.assertIn("if (proof==2)",scope)
        self.assertIn("if (src<=0 || dst<0 || src==dst",scope)
        self.assertIn("vk_rs_preflight_view(&pair[0],&views[0],1,&source)!=1",scope)
        self.assertIn("vk_rs_preflight_view(&pair[1],&views[1],1,&extent)!=1",scope)
        self.assertIn("source.pw==extent.pw && source.ph==extent.ph",scope)
        self.assertIn("!nx_rs_scope_sample(&life,sampled) || (writes && !nx_rs_scope_compute_write",scope)
        self.assertIn("nx_rs_compute_pointwise_format(cs->spv",replay)
        self.assertIn("nx_rs_storage_spv_format(t->d.format)!=bind[1]->fmt",replay)
        tex=(base/"gfx_vk_tex.c").read_text()
        self.assertIn("!nx_rs_storage_spv_format(logical->nvn_format) || logical->fmt!=fmt->vk",tex)
