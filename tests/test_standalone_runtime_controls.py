"""Standalone APK in-game performance menu and runtime driver reporting."""
import unittest
from pathlib import Path

DM = Path(__file__).resolve().parents[1] / "nxnative" / "dm" / "android"


class StandaloneRuntimeControlsTest(unittest.TestCase):
    def test_fps_atlas_rasterized_before_upload(self):
        c = (DM / "gfx_vk_present.c").read_text()
        self.assertEqual(c.count("ov_fps_glyphs();"), 1)
        self.assertLess(c.index("memset(g_ov.px +"), c.index("ov_fps_glyphs();"))
        self.assertIn("if (fps && menu < 0) ov_fps(cb, now);", c)

    def test_standalone_menu_has_live_frame_generation_controls(self):
        header = (DM / "overlay_layout.h").read_text()
        front = (DM / "front_android.c").read_text()
        draw = (DM / "gfx_vk_present.c").read_text()
        self.assertIn("#define OVM_TABS 3", header)
        self.assertIn("OVM_FG, OVM_FG_MULT, OVM_FG_FLOW, OVM_DRIVER", header)
        self.assertIn('"Performance"', header)
        self.assertIn('dm_fg_set(atomic_load(&g_set_fg)', front)
        self.assertIn("fg_mult_step(1)", front)
        self.assertIn("fg_flow_step(1)", front)
        for x in ("fg=%d", "fg_mult=%d", "fg_flow=%d"):
            self.assertIn(x, front)
        self.assertIn('"%s/Lossless.dll"', front)
        self.assertIn('"Need DLL"', draw)
        self.assertIn("dm_vk_driver_status()", draw)

    def test_actual_custom_driver_status_or_fallback(self):
        core = (DM / "gfx_vk_core.c").read_text()
        self.assertIn("custom_driver_loaded = 1;", core)
        self.assertIn('"System (fallback)"', core)
        self.assertIn('"Custom: %.100s"', core)
        self.assertIn("atomic_store_explicit(&g_vk_driver_status_ready, 1, memory_order_release)", core)
        self.assertIn('VKLOG("[driver] ACTIVE:', core)


if __name__ == "__main__":
    unittest.main()
