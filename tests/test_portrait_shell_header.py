"""Regression coverage for adaptive builder navigation and safe drawing insets."""
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "android/app/src/main"
KOTLIN = APP / "java/com/nx/android"


class PortraitShellHeaderTests(unittest.TestCase):
    def test_adaptive_navigation_uses_one_destination_list(self):
        source = (KOTLIN / "app/AppRoot.kt").read_text()
        self.assertIn("WindowInsets.safeDrawing", source)
        self.assertIn("NavigationRailItem(", source)
        self.assertIn("NavigationBarItem(", source)
        self.assertEqual(source.count("Tab.entries.forEach"), 2)
        self.assertIn("TopAppBarDefaults.exitUntilCollapsedScrollBehavior()", source)
        self.assertIn("Icons.Outlined.Build", source)
        for destination in ("BUILD", "UPGRADE", "SETTINGS", "DEBUG"):
            self.assertIn(f"Tab.{destination}", source)

    def test_settings_is_scrollable_without_an_inner_rail(self):
        source = (KOTLIN / "feature/settings/SettingsScreen.kt").read_text()
        components = (KOTLIN / "core/ui/components/BuilderComponents.kt").read_text()
        self.assertIn("BuilderPage {", source)
        self.assertIn(".verticalScroll(rememberPageScroll())", components)
        self.assertNotIn("NavigationRail", source)

    def test_builder_entry_points_exclude_legacy_gameplay(self):
        source = (KOTLIN / "MainActivity.kt").read_text()
        self.assertIn("enableEdgeToEdge()", source)
        for entry in ("dispatchKeyEvent", "onGenericMotionEvent", "GameActivity", "checkQuietly"):
            self.assertNotIn(entry, source)
        ns = "{http://schemas.android.com/apk/res/android}"
        activities = ET.parse(APP / "AndroidManifest.xml").findall("./application/activity")
        self.assertEqual([node.get(ns + "name") for node in activities], [".MainActivity"])


if __name__ == "__main__":
    unittest.main()
