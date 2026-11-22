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
        # The standalone counter reports accepted output presents: source + LSFG.
        # Source-only FPS remains available separately through hud_count().
        self.assertIn("if (period >= 0)", present)
        self.assertNotIn("if (with_meta && period >= 0)", present)
        self.assertIn("present_one(NULL, gen[g]", src)
        self.assertIn("atomic_fetch_add_explicit(&g_fps_frames, 1, memory_order_relaxed)", present)
        self.assertNotIn("atomic_fetch_add(&g_fps_frames", src[src.index("uint64_t vk_present_vkimg("):])
        self.assertIn('atomic_load(&g_fps_frames)', src)

    def test_profile_enables_fps_without_overwriting_existing_settings(self):
        rows = PROFILE.read_text(encoding="utf-8").splitlines()
        self.assertIn("graphics\tnvn", rows)
        self.assertIn("setting\tfps=1", rows)
        self.assertIn("env\tDM_FFRS_S25FE_RADV_TUNE=1", rows)

    def test_geometry_probe_only_captures_scene_passes_with_a_bounded_budget(self):
        exec_src = (ROOT / "nxnative/dm/android/gfx_vk_exec.c").read_text(encoding="utf-8")
        part = exec_src[exec_src.index("static void pass_dump(void) {"):
                        exec_src.index("static void texdump_flush(void) {")]
        self.assertIn('getenv("DM_VK_PASSDUMP_MIN_COLORS")', part)
        self.assertIn('S.rt_has_col[i]', part)
        self.assertIn('if (have < need) return;', part)
        self.assertIn('g_npd >= pass_dump_limit()', part)
        self.assertIn('getenv("DM_VK_PASSDUMP_LIMIT")', exec_src)
        self.assertIn('g_npd < pass_dump_limit() - 1', exec_src)
        # Shader pipelines can be present while GPU culling emits zero
        # indirect primitives. The bounded probe reports arguments for the
        # first 48 draws on two presents, without a GPU readback stall.
        self.assertIn('DM_VK_INDIRECT_LOG_AT', exec_src)
        self.assertIn('indirect_log(d);', exec_src)
        self.assertIn('entries++ >= 48', exec_src)

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
        self.assertIn('if (fps && atoi(fps) == 60)', tune)
        self.assertIn('setenv("DM_ADAPTIVE_FPS", "0", 0)', tune)
        self.assertIn('setenv("DM_PRESENT_INTERVAL", "display", 0)', tune)
        self.assertIn('if (!getenv("DM_VK_ZEROCOPY_LAZY"))', tune)
        self.assertIn('setenv("DM_VK_ZEROCOPY_LAZY", "2", 0)', tune)
        self.assertIn('if (!getenv("RADV_XCLIPSE_DCC_SMALL"))', tune)
        self.assertIn('setenv("RADV_XCLIPSE_DCC_SMALL", "0", 0)', tune)
        self.assertIn('if (!getenv("DM_VK_RP"))', tune)
        self.assertIn('setenv("DM_VK_RP", "0", 0)', tune)
        self.assertIn('override with DM_VK_RP=1', tune)
        self.assertIn('if (!getenv("RADV_XCLIPSE_DLC"))', tune)
        self.assertIn('setenv("RADV_XCLIPSE_DLC", "1", 0)', tune)
        self.assertIn('override with RADV_XCLIPSE_DLC=0', tune)
        # The prior game capture had experimental two-stage replay enabled.
        # Default to correctness on this device, but allow a deliberate A/B
        # opt-in; other devices are rejected by the guards at the top.
        self.assertIn('getenv("DM_FFRS_S25FE_RADV_ALLOW_MT_REPLAY")', tune)
        self.assertIn('getenv("DM_VK_MT_REPLAY")', tune)
        self.assertIn('setenv("DM_VK_MT_REPLAY", "0", 1)', tune)
        start = src.index("static void host_start_common(")
        code = src[start:src.index("static void host_start(", start)]
        self.assertLess(code.index("env_file(p);"), code.index("tune_ffrs_s25fe_radv();"))
        self.assertLess(code.index("tune_ffrs_s25fe_radv();"), code.index("settings_load();"))


if __name__ == "__main__":
    unittest.main()
