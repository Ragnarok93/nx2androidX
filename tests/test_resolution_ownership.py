import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
class ResolutionOwnershipTests(unittest.TestCase):
    def test_ownership_graph(self):
        cc = shutil.which('cc') or shutil.which('clang')
        if not cc: self.skipTest('C compiler required')
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / 'ownership'
            subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'nxnative/dm/gfx/nvn'), str(ROOT / 'tests/resolution_ownership.c'), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
if __name__ == '__main__': unittest.main()
