"""Source regressions for standalone SAF import/export, FSR1, and output FPS.

Actual runtime behavior must also be checked on a physical Android device.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
ANDROID = ROOT / "nxnative/dm/android"


class StandaloneGraphicsAndSaveTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.menu = (ANDROID / "overlay_layout.h").read_text()
        cls.front = (ANDROID / "front_android.c").read_text()
        cls.java = (ANDROID / "java/org/nx2a/SaveDocuments.java").read_text()
        cls.gfx = (ANDROID / "gfx_vk_present.c").read_text()
        cls.spatial = (ANDROID / "gfx_vk_spatial.h").read_text()

    def test_android_picker_for_import_and_export(self):
        self.assertIn("Intent.ACTION_CREATE_DOCUMENT", self.java)
        self.assertIn("Intent.ACTION_OPEN_DOCUMENT", self.java)
        self.assertIn('i.setType("application/zip")', self.java)
        self.assertIn('jni_save_document_picker("chooseSaveExport")', self.front)
        self.assertIn('jni_save_document_picker("chooseSaveImport")', self.front)
        self.assertIn("getContentResolver().openOutputStream(uri, \"w\")", self.java)
        self.assertIn("getContentResolver().openInputStream(uri)", self.java)

    def test_import_is_bounded_and_uses_existing_safe_swap(self):
        self.assertIn("MAX_IMPORT_BYTES", self.java)
        self.assertIn("MAX_IMPORT_ENTRIES", self.java)
        self.assertIn("getCanonicalPath()", self.java)
        self.assertIn("saveImportStage(name)", self.java)
        self.assertIn("dm_saves_request_import(n)", self.front)
        self.assertIn("dm_saves_export", self.front)
        self.assertIn("Java_org_nx2a_GameActivity_saveArchiveRoot", self.front)
        self.assertIn("GameActivity.saveArchiveRoot()", self.java)
        self.assertNotIn("getExternalFilesDir(null)", self.java)
        external = (ANDROID / "java/org/nx2a/ExternalActivity.java").read_text()
        self.assertIn("SaveDocuments.onActivityResult(this", external)
        self.assertIn("chooseSaveExport()", external)
        self.assertIn("chooseSaveImport()", external)

    def test_graphics_and_pacing_tab_controls(self):
        self.assertIn('{"Gamepad", "Game", "Graphics", "Pacing"}', self.menu)
        # Check the actual tab membership, independent of padding or new controls.
        import re
        table = self.menu.split("OVM_TAB_ITEMS[OVM_PAGES][OVM_PER_TAB] = {", 1)[1].split("};", 1)[0]
        rows = [re.findall(r"OVM_\w+", row) for row in re.findall(r"\{([^{}]+)\}", table)]
        self.assertEqual(rows[1], ["OVM_FPS", "OVM_MODE", "OVM_EXPORT", "OVM_IMPORT", "OVM_AUDIO"])
        self.assertEqual(rows[2], ["OVM_VIVID", "OVM_FSR", "OVM_FSR_SHARP", "OVM_DRS",
                                   "OVM_DRS_MIN", "OVM_RS_MAX", "OVM_DRS_NOW", "OVM_DRS_HOOK", "OVM_DRIVER"])
        self.assertEqual(rows[3], ["OVM_RATE", "OVM_FG", "OVM_FG_MULT", "OVM_FG_FLOW", "OVM_DIAG_OPEN"])
        self.assertIn('[OVM_VIVID]="HDR"', self.gfx)
        self.assertIn("vivid=%d", self.front)
        self.assertEqual(sum(row.count("OVM_FPS") for row in rows), 1)
        self.assertIn("ov_menu_sharp_value", self.menu)
        self.assertIn("fsr_sharp=%d", self.front)
        self.assertIn("upscaler=%d", self.front)
        self.assertIn("v ? SP_FSR1 : SP_NATIVE", self.front)

    def test_fsr_native_pipeline_has_true_easu_and_rcas_stages(self):
        easu = (ANDROID / "shaders/fsr_easu.frag").read_text()
        rcas = (ANDROID / "shaders/fsr_rcas.frag").read_text()
        self.assertIn("FsrEasuF(", easu)
        self.assertIn("FsrRcasF(", rcas)
        self.assertIn("spatial_draw(cb, SP_RCAS, g_spatial.set[SP_INTERMEDIATE]", self.spatial)
        self.assertIn("mode == SP_FSR1 ? SP_EASU : SP_GSR1", self.spatial)
        self.assertIn("gsr1_frag_spv", self.spatial)
        self.assertIn("if (g_spatial.ticket == ticket) return output", self.spatial)
        self.assertIn("if (flip_y)", self.spatial)
        self.assertNotIn("vkDeviceWaitIdle", self.spatial)

    def test_processing_order_and_redraw_exclusion(self):
        window = self.gfx.split("static void present_window(", 1)[1].split("static void *present_thread(", 1)[0]
        present = self.gfx.split("static void present_one(", 1)[1].split("// headless frame shots", 1)[0]
        self.assertEqual(window.count("spatial_reconstruct("), 1)
        self.assertLess(window.index("if (redraw)"), window.index("spatial_reconstruct("))
        self.assertLess(window.index("spatial_reconstruct("), window.index("dm_fg_frame("))
        self.assertLess(window.index("if (redraw || period < 0)"), window.index("dm_fg_frame("))
        self.assertIn("dm_fg_frame(source, rect, flip_y", window)
        self.assertNotIn("spatial_reconstruct(", present)
        self.assertLess(present.index("vivid_record("), present.index("ov_render("))

    def test_fps_counts_generated_only_on_accepted_presents(self):
        src = self.gfx[self.gfx.index("static void present_one("):self.gfx.index("// headless frame shots")]
        self.assertLess(src.index("r = vkQueuePresentKHR"), src.index("if (period >= 0)"))
        self.assertIn("if (r >= 0)", src)
        self.assertIn("hud_count(with_meta, period", src)
        hud = self.gfx.split("static void hud_count(", 1)[1].split("uint64_t vk_refresh_cycle_ns", 1)[0]
        self.assertLess(hud.index("if (period < 0) return"), hud.index("atomic_fetch_add_explicit(&g_hud_gen"))
        self.assertIn("atomic_fetch_add_explicit(&g_fps_frames, 1, memory_order_relaxed)", src)
        self.assertNotIn("if (with_meta && period >= 0)", src)


if __name__ == "__main__":
    unittest.main()
