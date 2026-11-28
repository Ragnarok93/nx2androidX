"""Exercise fixed source cadence against the actual runtime hold expression."""
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class FGCadenceFeedback(unittest.TestCase):
    def test_fixed_hold_recovers_after_transient_backpressure(self):
        run = subprocess.run([sys.executable, str(ROOT / 'devtools/reproduce_fg_cadence.py')],
                             cwd=ROOT, capture_output=True, text=True, timeout=20)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
