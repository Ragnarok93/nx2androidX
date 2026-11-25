"""Standalone unlocked-mode integration contract (device behavior still needs validation)."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "nxnative/dm"
class UnlockedRateTest(unittest.TestCase):
    def test_menu_cycles_and_persists(self):
        s = (ROOT / "android/front_android.c").read_text()
        self.assertIn("v == -1 || v == 30", s)
        self.assertIn("previous == 60) next = direction < 0 ? 40 : -1", s)
        self.assertIn("previous == -1) next = direction < 0 ? 60 : 30", s)
        self.assertIn("fps_mode=%d", s)
    def test_uncapped_engine_and_host(self):
        s = (ROOT / "host/hle_fpslock.c").read_text()
        self.assertIn("target == -1 ? 0.0f", s)
        self.assertIn("target == -1 ? 0u : target == 30", s)
        self.assertIn('if (unlocked && !dm_fps_lock_supported()) return;', s)
        self.assertIn("if (fixed == -1) return dm_present_period_adapt(0);", (ROOT / "gfx/nvn/hle_nvn.c").read_text())
        s = (ROOT / "android/perf_android.c").read_text()
        self.assertIn("if (fixed == -1) return 0;", s)
        self.assertIn("dm_graphics_target_fps() == -1 || !adaptive_enabled()", s)
    def test_ui_and_fg(self):
        s = (ROOT / "android/gfx_vk_present.c").read_text()
        self.assertIn("M3_ON_PRIMARY[4] = {255, 255, 255, 255}", s)
        self.assertIn("M3_ON_SECONDARY[4] = {255, 255, 255, 255}", s)
        self.assertIn('dm_menu_rate() == -1) snprintf(value, sizeof value, "Unlocked")', s)
        self.assertIn("double fps = unlocked ? 0.0 : window_fps(wperiod);", s)
        self.assertIn("uncapped source rate; select a fixed FPS mode", (ROOT / "android/dm_framegen.c").read_text())
if __name__ == "__main__":
    unittest.main()
