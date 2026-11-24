"""Regression for the shell's portrait top-row overflow and display cutouts."""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHELL = ROOT / "android/app/src/main/java/com/nx/android/app/AppRoot.kt"
ACTIVITY = ROOT / "android/app/src/main/java/com/nx/android/MainActivity.kt"
SETTINGS = ROOT / "android/app/src/main/java/com/nx/android/feature/settings/SettingsScreen.kt"
ROWS = ROOT / "android/app/src/main/java/com/nx/android/core/ui/components/SettingsRows.kt"

class PortraitShellHeaderTests(unittest.TestCase):
    def test_header_is_responsive_and_respects_system_insets(self):
        src = SHELL.read_text(encoding="utf-8")
        header = src[src.index("private fun TopBar("):src.index("/** 手柄的入口：")]
        self.assertIn("BoxWithConstraints(", header)
        self.assertIn("if (maxWidth < 840.dp)", header)
        # Compose layout DSL markers hide the outer BoxWithConstraintsScope inside Column.
        self.assertLess(header.index("val narrow = maxWidth < 520.dp"), header.index("if (maxWidth < 840.dp)"))
        self.assertEqual(header.count("maxWidth < 520.dp"), 1)
        self.assertIn("WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal)", header)
        self.assertIn("TopBarBrand(Modifier.weight(1f), compact = true)", header)
        self.assertIn("TopBarTabs(selected, onSelect, compact = true)", header)
        compact = header.split("if (maxWidth < 840.dp)", 1)[1].split("} else {\\n            Row(", 1)[0]
        self.assertIn("Box(Modifier.weight(1f))", compact)
        self.assertLess(compact.index("EnvChip(chip, onChip)"), compact.index("TopBarTabs(selected, onSelect, compact = true)"))
        self.assertEqual(compact.count("Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically)"), 2)
        self.assertIn("Modifier.weight(1f).clickable { onSelect(tab) }", header)
        self.assertIn("overflow = TextOverflow.Ellipsis", header)
        # Both modes expose all actions, rather than hiding overflowed controls.
        compact, wide = header.split("} else {\n            Row(", 1)
        for control in ("onRefresh)", "onOpenPadCenter)", "onSettings)", "EnvChip(chip, onChip)"):
            self.assertIn(control, compact)
            self.assertIn(control, wide)

    def test_portrait_settings_uses_full_width_and_scrollable_sections(self):
        src = SETTINGS.read_text(encoding="utf-8")
        self.assertIn("BoxWithConstraints(Modifier.fillMaxSize())", src)
        self.assertIn("if (maxWidth < 640.dp)", src)
        self.assertIn("horizontalScroll(rememberScrollState())", src)
        self.assertIn("CompactSectionItem(", src)
        self.assertIn("Modifier.fillMaxWidth().weight(1f).verticalScroll(scroll)", src)
        self.assertIn("SettingsSectionContent(", src)
        self.assertIn("maxLines = 1", src[src.index("private fun CompactSectionItem("):src.index("/** 页头的关闭")])
        row = ROWS.read_text(encoding="utf-8")
        action = row[row.index("fun ActionRow("):row.index("/** 一行说明")]
        self.assertIn("if (maxWidth < 420.dp)", action)
        self.assertIn("Arrangement.End", action)

    def test_immersive_activity_still_requests_system_bars_hidden(self):
        src = ACTIVITY.read_text(encoding="utf-8")
        self.assertIn("WindowCompat.setDecorFitsSystemWindows(window, false)", src)
        self.assertIn("hide(WindowInsetsCompat.Type.systemBars())", src)

if __name__ == "__main__":
    unittest.main()
