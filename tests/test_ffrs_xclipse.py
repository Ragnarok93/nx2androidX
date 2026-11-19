"""FINAL FANTASY RESONANCE / RADV Xclipse regressions.

These source checks catch the NVN FPS-0 regression and ensure the Exynos-only
compatibility defaults are not silently applied to Snapdragon devices.
A phone-side render comparison is still required for visual fidelity.
"""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PRESENT = ROOT / "nxnative/dm/android/gfx_vk_present.c"
FRONT = ROOT / "nxnative/dm/android/front_android.c"
PROFILE = ROOT / "nxnative/dm/games/0100606026F12000/android.tsv"


class ResonanceXclipseTests(unittest.TestCase):
    def test_fps_counts_successful_source_presents_not_only_gl(self):
        src = PRESENT.read_text(encoding="utf-8")
        present = src[src.index("static void present_one("):src.index("static void present_window(")]
        self.assertLess(present.index("r = vkQueuePresentKHR"), present.index("hud_count(with_meta, period, mono_ns());"))
        self.assertIn("if (r >= 0)", present)
        self.assertIn("if (with_meta && period >= 0)", present)
        self.assertIn("atomic_fetch_add_explicit(&g_fps_frames, 1, memory_order_relaxed)", present)
        self.assertNotIn("atomic_fetch_add(&g_fps_frames", src[src.index("uint64_t vk_present_vkimg("):])
        self.assertIn('atomic_load(&g_fps_frames)', src)

    def test_profile_enables_fps_without_overwriting_existing_settings(self):
        rows = PROFILE.read_text(encoding="utf-8").splitlines()
        self.assertIn("graphics\tnvn", rows)
        self.assertIn("setting\tfps=1", rows)
        self.assertIn("env\tDM_FFRS_S25FE_RADV_TUNE=1", rows)

    def test_xclipse_tuning_is_device_scoped_and_opt_out(self):
        src = FRONT.read_text(encoding="utf-8")
        tune = src[src.index("static void tune_ffrs_s25fe_radv(void) {"):src.index("static void host_start_common(")]
        self.assertIn('strncmp(model, "SM-S731", 7)', tune)
        self.assertIn('strstr(driver, "radeon")', tune)
        self.assertIn('strstr(driver, "radv")', tune)
        self.assertIn('strcmp(enabled, "1")', tune)
        self.assertIn('if (!getenv("DM_FPS_TARGET"))', tune)
        self.assertIn('setenv("DM_FPS_TARGET", "30", 0)', tune)
        self.assertIn('if (!getenv("DM_OPMODE"))', tune)
        self.assertIn('setenv("DM_OPMODE", "0", 0)', tune)
        self.assertIn('if (!getenv("RADV_XCLIPSE_DCC_SMALL"))', tune)
        self.assertIn('setenv("RADV_XCLIPSE_DCC_SMALL", "0", 0)', tune)
        start = src.index("static void host_start_common(")
        code = src[start:src.index("static void host_start(", start)]
        self.assertLess(code.index("env_file(p);"), code.index("tune_ffrs_s25fe_radv();"))
        self.assertLess(code.index("tune_ffrs_s25fe_radv();"), code.index("settings_load();"))


if __name__ == "__main__":
    unittest.main()
