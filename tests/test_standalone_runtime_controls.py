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
        self.assertIn("#define OVM_TABS 4", header)
        self.assertIn("OVM_RATE, OVM_FG, OVM_FG_MULT, OVM_FG_FLOW", header)
        self.assertIn('"Graphics"', header)
        self.assertNotIn('"Performance"', header)
        self.assertIn("OVM_FSR, OVM_FSR_SHARP", header)
        self.assertIn("OVM_FPS, OVM_MODE", header)
        self.assertIn('[OVM_DIAG_CAPTURE]="Start capture"', draw)
        self.assertIn('[OVM_DIAG_SNAPSHOT]="Debug level"', draw)
        self.assertIn('[OVM_DIAG_DELAY]="Snapshot delay"', draw)
        self.assertNotIn('"Capture graphics"', draw)
        self.assertNotIn('"Delayed GPU snapshots"', draw)
        self.assertIn('vk_pass_dump_user_begin_level(delay,level)', front)
        self.assertIn('if(diag_controls_locked())return;', front)
        self.assertIn('else if (item == OVM_DIAG_EXPORT) { diag_export_request(); return; }',front)
        self.assertIn('vk_pass_dump_export_begin()',front)
        self.assertIn('vk_pass_dump_export_end()',front)
        self.assertNotIn("OVM_DIAG_TRACE", header)
        self.assertNotIn("OVM_DIAG_EFFECTS", header)
        self.assertIn('dm_fg_set(atomic_load(&g_set_fg)', front)
        self.assertIn("fg_mult_step(1)", front)
        self.assertIn("fg_flow_step(1)", front)
        for x in ("fg=%d", "fg_mult=%d", "fg_flow=%d", "fg_fixed=%d", "fg_preset=%d"):
            self.assertIn(x, front)
        self.assertIn('"%s/Lossless.dll"', front)
        self.assertIn('"Need DLL"', draw)
        self.assertIn("dm_vk_driver_status()", draw)

    def test_user_can_import_lossless_dll_from_game_menu(self):
        java = (DM / "java/org/nx2a/GameActivity.java").read_text()
        front = (DM / "front_android.c").read_text()
        fg = (DM / "dm_framegen.c").read_text()
        self.assertIn('Intent.ACTION_OPEN_DOCUMENT', java)
        self.assertIn('OpenableColumns.DISPLAY_NAME', java)
        self.assertIn('importLosslessDll(data.getData())', java)
        self.assertIn('Lossless.dll.partial', java)
        self.assertIn('losslessDllReady(target.getAbsolutePath())', java)
        self.assertIn('Java_org_nx2a_GameActivity_losslessDllReady', front)
        self.assertIn('jni_fg_dll_picker();', front)
        self.assertIn('dm_fg_retry();', front)
        self.assertIn('atomic_exchange(&g_reprobe, 0)', fg)

    def test_actual_custom_driver_status_or_fallback(self):
        core = (DM / "gfx_vk_core.c").read_text()
        self.assertIn("custom_driver_loaded = 1;", core)
        self.assertIn('"System (fallback)"', core)
        self.assertIn('"Custom: %.100s"', core)
        self.assertIn("atomic_store_explicit(&g_vk_driver_status_ready, 1, memory_order_release)", core)
        self.assertIn('VKLOG("[driver] ACTIVE:', core)


if __name__ == "__main__":
    unittest.main()
