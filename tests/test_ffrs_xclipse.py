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

    def test_geometry_probe_only_captures_scene_passes_with_a_bounded_budget(self):
        exec_src = (ROOT / "nxnative/dm/android/gfx_vk_exec.c").read_text(encoding="utf-8")
        part = exec_src[exec_src.index("static void pass_dump(void) {"):
                        exec_src.index("static void texdump_flush(void) {")]
        self.assertIn('getenv("DM_VK_PASSDUMP_MIN_COLORS")', part)
        self.assertIn('S.rt_has_col[i]', part)
        self.assertIn('if (have < need) return;', part)
        self.assertIn('getenv("DM_VK_PASSDUMP_MIN_DRAWS")', part)
        self.assertIn('vk_pd_pass_has_draws(g_pass_draws', part)
        self.assertIn('vk_pd_depth_prepass_eligible', part)
        self.assertIn('pass_dump_depth_control(VK_FORMAT_D24_UNORM_S8_UINT);', part)
        self.assertIn('getenv("DM_VK_PASSDUMP_DEPTH_CONTROL")', part)
        self.assertIn('vk_pd_d24_matches', exec_src)
        self.assertIn('[depthcontrol] RESULT:', exec_src)
        self.assertIn('pass_dump_depth_aspect(VK_IMAGE_ASPECT_DEPTH_BIT)', part)
        self.assertIn('g_npd >= pass_dump_limit()', part)
        self.assertIn('getenv("DM_VK_PASSDUMP_LIMIT")', exec_src)
        self.assertIn('g_npd < pass_dump_limit() - 1', exec_src)
        # Shader pipelines can be present while GPU culling emits zero
        # indirect primitives. The bounded probe reports arguments for the
        # first 48 draws on two presents, without a GPU readback stall.
        self.assertIn('DM_VK_INDIRECT_LOG_AT', exec_src)
        self.assertIn('indirect_log(d);', exec_src)
        self.assertIn('entries++ >= 48', exec_src)

    def test_xclipse_tuning_is_device_scoped_without_unproven_driver_defaults(self):
        src = FRONT.read_text(encoding="utf-8")
        tune = src[src.index("static void tune_ffrs_s25fe_radv(void) {"):
                   src.index("static void host_start_common(")]
        self.assertIn('strncmp(model, "SM-S731", 7)', tune)
        self.assertIn('strstr(driver, "radeon")', tune)
        self.assertIn('strstr(driver, "radv")', tune)
        self.assertIn('strcmp(enabled, "1")', tune)
        self.assertIn('if (!getenv("DM_FPS_TARGET"))', tune)
        self.assertIn('setenv("DM_FPS_TARGET", "30", 0)', tune)
        self.assertIn('if (!getenv("DM_VK_ASYNC_PIPE"))', tune)
        self.assertIn('setenv("DM_VK_ASYNC_PIPE", "0", 0)', tune)
        self.assertIn('setenv("DM_VK_LRZ_TOUCH", "0", 0)', tune)
        self.assertNotIn('setenv("DM_VK_D24_FLOAT_COPY",', tune)
        for speculative in ("DM_OPMODE", "RADV_XCLIPSE_DCC_SMALL",
                            "DM_VK_RP", "RADV_XCLIPSE_DLC", "DM_VK_MT_REPLAY"):
            self.assertNotIn(f'setenv("{speculative}"', tune)
        start = src.index("static void host_start_common(")
        code = src[start:src.index("static void host_start(", start)]
        self.assertLess(code.index("env_file(p);"), code.index("tune_ffrs_s25fe_radv();"))
        self.assertLess(code.index("tune_ffrs_s25fe_radv();"), code.index("settings_load();"))


if __name__ == "__main__":
    unittest.main()
