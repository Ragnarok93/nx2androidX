import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class ResolutionExtentTests(unittest.TestCase):
    def test_abi_compatible_extent(self):
        cc=shutil.which("cc") or shutil.which("clang")
        if not cc: self.skipTest("C compiler required")
        with tempfile.TemporaryDirectory() as d:
            exe=Path(d)/"extent"
            subprocess.run([cc,"-std=c11","-Wall","-Wextra","-Werror","-I",str(ROOT/"nxnative/dm/gfx/nvn"),str(ROOT/"tests/resolution_extent.c"),"-o",str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_compute_grid_extent(self):
        cc=shutil.which("cc") or shutil.which("clang")
        if not cc: self.skipTest("C compiler required")
        with tempfile.TemporaryDirectory() as d:
            exe=Path(d)/"grid"
            subprocess.run([cc,"-std=c11","-Wall","-Wextra","-Werror","-I",str(ROOT/"nxnative/dm/gfx/nvn"),str(ROOT/"tests/resolution_grid_extent.c"),"-o",str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_grid_retry_wiring(self):
        scope=(ROOT/"nxnative/dm/android/gfx_vk_resolution_scope.inc").read_text()
        self.assertIn("nx_rs_require_grid(&raster,g_rs_frame.target",scope)
        self.assertIn("if (geometry!=1) return geometry;",scope)
        self.assertIn('if (proof==2)',scope)
        self.assertIn("ext=(nx_rs_extent){group.pw,group.ph};",scope)
