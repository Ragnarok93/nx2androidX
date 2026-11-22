"""Guard the consolidated Xclipse correctness, APK and memory feature surface."""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ANDROID = ROOT / "nxnative/dm/android"

class FinalFantasyResonanceUnifiedFeatures(unittest.TestCase):
    def test_in_game_pacing_graphics_and_diagnostics_coexist(self):
        header = (ANDROID / "overlay_layout.h").read_text()
        front = (ANDROID / "front_android.c").read_text()
        raster = (ANDROID / "gfx_vk_present.c").read_text()
        self.assertIn("#define OVM_TABS 4", header)
        self.assertIn("#define OVM_PAGES (OVM_TABS + 2)", header)
        for k in ("OVM_FSR", "OVM_FSR_SHARP", "OVM_RATE", "OVM_DRS_MIN",
                  "OVM_DIAG_OPEN", "OVM_DIAG_CAPTURE", "OVM_DIAG_EXPORT",
                  "OVM_DIAG_FLAGS", "OVM_FLAG_BC"):
            self.assertIn(k, header)
            self.assertIn(k, raster)
        self.assertIn("menu_active_tab()", front)
        self.assertIn("diag_capture_arm(", front)
        self.assertIn("menu_drs_toggle(", front)
        self.assertIn("menu_rate_step(", front)
        self.assertIn("OVM_DIAG_STATE", raster)

    def test_graphics_translator_and_cpu_bc_fallback_remain_present(self):
        root = ROOT / "nxnative/dm"
        game = (root / "games/0100606026F12000/game.tsv").read_text()
        profile = (ANDROID / "front_profile.h").read_text()
        renderer = (ANDROID / "gfx_vk_tex.c").read_text()
        translator = (root / "third_party/ryujinx/src/Ryujinx.Graphics.Shader/Translation/Optimizations/BindlessElimination.cs").read_text()
        self.assertIn("android_dynamic_slots\ton", game)
        self.assertIn("DM_VK_ASYNC_PIPE", profile)
        self.assertIn("DM_VK_ZEROCOPY_LAZY", profile)
        self.assertIn("DM_VK_UNCACHED_MIP_INIT", profile)
        self.assertIn("DM_VK_LRZ_TOUCH", profile)
        self.assertIn("BC", renderer)
        self.assertIn("class BindlessElimination", translator)

    def test_bounded_porting_path_not_replaced_by_old_graphics_branch(self):
        porter = (ROOT / "porter/pipeline.py").read_text()
        uepak = (root := ROOT / "nxnative/dm/gfx/tools/uepak.py").read_text()
        self.assertIn("with out.open('rb') as inp:", porter)
        self.assertIn("(64 << 20)", porter)
        self.assertIn("def io_archive_file(", uepak)

    def test_diagnostic_export_and_save_documents_both_reachable(self):
        for p in ("GameActivity.java", "ExternalActivity.java"):
            java = (ANDROID / "java/org/nx2a" / p).read_text()
            self.assertIn("DiagnosticsExport.onResult", java)
            self.assertIn("SaveDocuments.onActivityResult", java)
        self.assertEqual((ANDROID / "java/org/nx2a/GameActivity.java").read_text().count("GameKeyboard keyboard"), 1)
        self.assertIn('static void preserveVulkanCache(File files, File previousPack)', java)

if __name__ == "__main__":
    unittest.main()
