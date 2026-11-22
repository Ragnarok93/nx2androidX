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
        cls.fsr = (ANDROID / "gfx_vk_fsr.h").read_text()

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

    def test_graphics_tab_contains_no_duplicate_fps(self):
        self.assertIn('{"Gamepad", "Game", "Graphics"}', self.menu)
        self.assertIn("{OVM_FSR, OVM_FSR_SHARP, OVM_FG, OVM_FG_MULT, OVM_FG_FLOW, OVM_DRIVER}", self.menu)
        self.assertIn("{OVM_FPS, OVM_MODE, OVM_EXPORT, OVM_IMPORT, OVM_AUDIO, -1}", self.menu)
        self.assertIn("ov_menu_sharp_value", self.menu)
        self.assertIn("fsr_sharp=%d", self.front)

    def test_fsr_native_pipeline_has_true_easu_and_rcas_stages(self):
        easu = (ANDROID / "shaders/fsr_easu.frag").read_text()
        rcas = (ANDROID / "shaders/fsr_rcas.frag").read_text()
        self.assertIn("FsrEasuF(", easu)
        self.assertIn("FsrRcasF(", rcas)
        self.assertIn("fsr_draw(cb, 0,", self.fsr)
        self.assertIn("fsr_draw(cb, 1,", self.fsr)
        self.assertIn("if (!V.has_dynrender", self.fsr)
        self.assertIn("VK_ATTACHMENT_LOAD_OP_LOAD", self.fsr)
        self.assertIn("if (flip_y)", self.fsr)
        self.assertIn("VK_FILTER_LINEAR", self.gfx)  # graceful fallback

    def test_fps_counts_generated_only_on_accepted_presents(self):
        src = self.gfx[self.gfx.index("static void present_one("):self.gfx.index("static void present_window(")]
        self.assertLess(src.index("r = vkQueuePresentKHR"), src.index("if (period >= 0)"))
        self.assertIn("if (r >= 0)", src)
        self.assertIn("hud_count(with_meta, period", src)
        self.assertIn("atomic_fetch_add_explicit(&g_fps_frames, 1, memory_order_relaxed)", src)
        self.assertNotIn("if (with_meta && period >= 0)", src)


if __name__ == "__main__":
    unittest.main()
