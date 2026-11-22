"""Regression for the shell's portrait top-row overflow and display cutouts."""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHELL = ROOT / "android/app/src/main/java/com/nx/android/app/AppRoot.kt"
ACTIVITY = ROOT / "android/app/src/main/java/com/nx/android/MainActivity.kt"

class PortraitShellHeaderTests(unittest.TestCase):
    def test_header_is_responsive_and_respects_system_insets(self):
        src = SHELL.read_text(encoding="utf-8")
        header = src[src.index("private fun TopBar("):src.index("/** 手柄的入口：")]
        self.assertIn("BoxWithConstraints(", header)
        self.assertIn("if (maxWidth < 840.dp)", header)
        self.assertIn("WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal)", header)
        self.assertIn("TopBarBrand(Modifier.weight(1f), compact = true)", header)
        self.assertIn("TopBarTabs(selected, onSelect, compact = true)", header)
        self.assertIn("Modifier.weight(1f).clickable { onSelect(tab) }", header)
        self.assertIn("overflow = TextOverflow.Ellipsis", header)
        # Both modes expose all actions, rather than hiding overflowed controls.
        compact, wide = header.split("} else {\n            Row(", 1)
        for control in ("onRefresh)", "onOpenPadCenter)", "onSettings)", "EnvChip(chip, onChip)"):
            self.assertIn(control, compact)
            self.assertIn(control, wide)

    def test_immersive_activity_still_requests_system_bars_hidden(self):
        src = ACTIVITY.read_text(encoding="utf-8")
        self.assertIn("WindowCompat.setDecorFitsSystemWindows(window, false)", src)
        self.assertIn("hide(WindowInsetsCompat.Type.systemBars())", src)

if __name__ == "__main__":
    unittest.main()
