"""60 FPS is a deliberate fixed-rate build option, not an adaptive 60/40/30 mode."""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PORTER = ROOT / "porter/pipeline.py"
PROFILE = ROOT / "nxnative/dm/games/0100606026F12000/game.tsv"
ANDROID_PROFILE = ROOT / "nxnative/dm/games/0100606026F12000/android.tsv"

class Explicit60FpsModeTests(unittest.TestCase):
    def test_porter_fixed_60_paths(self):
        src = PORTER.read_text(encoding="utf-8")
        path = src[src.index("    def fps_unlock(self):"):src.index("    # ---- rewrite:", src.index("    def fps_unlock(self):"))]
        self.assertIn("self.fps_env = ['DM_FPS_TARGET=60', 'DM_ADAPTIVE_FPS=0']", path)
        self.assertIn("self.g.id.upper() == '0100606026F12000'", path)
        self.assertIn("self.fps_env.append('DM_PRESENT_INTERVAL=display')", path)
        self.assertIn("'DM_FPS_CEILING=0', 'DM_ADAPTIVE_FPS=0'", path)
        self.assertIn("['DM_FPS_TARGET=60', 'DM_ADAPTIVE_FPS=0', f'DM_FPS_LOCKS=", path)

    def test_ffrs_profile_has_valid_engine_target_and_restricted_compat_tune(self):
        self.assertIn("fps_target\t60", PROFILE.read_text(encoding="utf-8"))
        self.assertIn("env\tDM_FFRS_S25FE_RADV_TUNE=1", ANDROID_PROFILE.read_text(encoding="utf-8"))

if __name__ == "__main__":
    unittest.main()
