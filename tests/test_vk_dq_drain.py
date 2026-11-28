"""Run the recorder's gated POSIX-thread regression against production code."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(sys.platform.startswith('linux'), 'requires Linux pthread spinlocks')
class RecorderDrain(unittest.TestCase):
    def test_retired_targets_and_real_timeout(self):
        self.assertIsNotNone(shutil.which(os.environ.get('CC', 'cc')), 'C compiler required')
        result = subprocess.run(
            [sys.executable, str(ROOT / 'devtools/reproduce_dq_drain.py'), '--stress'],
            cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn('recording incomplete', result.stdout)
