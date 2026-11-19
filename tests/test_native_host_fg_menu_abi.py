"""Regression for the alpha31 on-device Oodle-host linker failure.

gfx_vk_present.c always references the three FG menu query functions, including
when linked to front_player.c (headless dmhost and in-app player). Only the
standalone front supplied them before; alpha31 failed at UE shader decode.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ANDROID = ROOT / "nxnative" / "dm" / "android"
FG_MENU_SYMBOLS = ("dm_menu_fg_mult", "dm_menu_fg_flow", "dm_menu_fg_has_dll")


class NativeHostFgMenuAbiTests(unittest.TestCase):
    def test_each_front_end_defines_fg_menu_queries(self):
        for filename in ("front_android.c", "front_player.c"):
            source = (ANDROID / filename).read_text(encoding="utf-8")
            for symbol in FG_MENU_SYMBOLS:
                with self.subTest(front=filename, symbol=symbol):
                    definitions = re.findall(
                        rf"(?m)^int\s+{symbol}\s*\(\s*void\s*\)\s*\{{", source
                    )
                    self.assertEqual(len(definitions), 1, f"{filename} must define {symbol} once")

    def test_presenter_references_fg_menu_queries(self):
        source = (ANDROID / "gfx_vk_present.c").read_text(encoding="utf-8")
        for symbol in FG_MENU_SYMBOLS:
            self.assertRegex(source, rf"\b{symbol}\s*\(\s*\)")

    def test_ci_links_every_host_front_end(self):
        build = (ANDROID / "build.py").read_text(encoding="utf-8")
        self.assertIn("'-Wl,--no-undefined'", build)
        self.assertIn("if a.exe: exe_front", build)
        self.assertIn("if a.lib: lib_front", build)
        self.assertIn("if a.play:", build)
        workflow = (ROOT / ".github/workflows/android-apk.yml").read_text(encoding="utf-8")
        self.assertIn("--exe --lib --play --release", workflow)
        self.assertIn('test -s "$RUNNER_TEMP/nx2a-native-host-smoke/dmhost"', workflow)
        self.assertIn('test -s "$RUNNER_TEMP/nx2a-native-host-smoke/libdmplay.so"', workflow)


if __name__ == "__main__":
    unittest.main()
