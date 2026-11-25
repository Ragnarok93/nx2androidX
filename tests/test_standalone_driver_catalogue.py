"""Source-level regression checks; Android device tests still required."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "nxnative/dm/android"

class StandaloneDriverCatalogueTests(unittest.TestCase):
    def setUp(self):
        self.java = (ROOT / "java/org/nx2a/DriverDocuments.java").read_text()
        self.gfx = (ROOT / "gfx_vk_present.c").read_text()

    def test_one_ui_accent_and_dark_dialog(self):
        self.assertIn("M3_PRIMARY[4] = {3, 129, 254, 255}", self.gfx)
        self.assertIn("Color.rgb(3, 129, 254)", self.java)
        self.assertIn("panel.setBackground(surface)", self.java)
        self.assertIn("new ColorDrawable(Color.TRANSPARENT)", self.java)
        self.assertNotIn("new AlertDialog.Builder", self.java)

    def test_import_publishes_catalogue_before_selection(self):
        src = self.java.split("boolean onResult(", 1)[1].split("private void toast(", 1)[0]
        self.assertIn('new File(root, ".partial-" + id)', src)
        self.assertLess(src.index("writeEntry(stage, library, display)"), src.index("stage.renameTo(target)"))
        self.assertLess(src.index("stage.renameTo(target)"), src.index('commit("drivers/" + id'))
        self.assertIn("if (!committed && stage != null)", src)

    def test_old_packages_and_system_remain_selectable(self):
        self.assertIn("private List<DriverEntry> installed(", self.java)
        self.assertIn('current.startsWith(prefix)', self.java)
        self.assertIn('meta.optString("libraryName")', self.java)
        self.assertIn('new DriverEntry("system", "System Vulkan")', self.java)
        self.assertIn("commit(entry.path)", self.java)
        self.assertIn("ScrollView scroll", self.java)

if __name__ == "__main__":
    unittest.main()
