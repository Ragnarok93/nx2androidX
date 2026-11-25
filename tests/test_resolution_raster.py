import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
class ResolutionRasterTests(unittest.TestCase):
    def test_raster(self):
        cc = shutil.which('cc') or shutil.which('clang')
        if not cc: self.skipTest('C compiler required')
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / 'raster'
            subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'nxnative/dm/gfx/nvn'), str(ROOT / 'tests/resolution_raster.c'), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
if __name__ == '__main__': unittest.main()
