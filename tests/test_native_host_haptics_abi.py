"""Native game host ABI regression checks (Android linker crash).

DropBox reported the exported game's libdmhost.so failed to load:
'cannot locate symbol dm_haptics_android_device'. The host's standalone
front-end still called the previous one-player ABI, while the Android
backend only defined dm_haptics_android_device_player(player, device).

These checks run even when a complete NDK game rebuild is unavailable.
Full game exports additionally fail at link time on ANY unresolved symbol.
"""
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ANDROID = ROOT / "nxnative" / "dm" / "android"


class NativeHostHapticsAbiTests(unittest.TestCase):
    def test_standalone_front_uses_current_haptics_backend_abi(self):
        front = (ANDROID / "front_android.c").read_text(encoding="utf-8")
        backend = (ANDROID / "haptics_android.c").read_text(encoding="utf-8")
        self.assertRegex(
            backend,
            r"\bvoid\s+dm_haptics_android_device_player\s*\(\s*int\s+player\s*,\s*int\s+id\s*\)",
        )
        self.assertNotRegex(front, r"\bdm_haptics_android_device\s*\(")
        self.assertEqual(
            len(re.findall(r"\bdm_haptics_android_device_player\s*\(\s*0\s*,", front)),
            4,
            "single-player standalone host must route every controller change to player 0",
        )
        self.assertIn("dm_pad_players(1)", front)

    def test_shared_host_linker_rejects_undefined_symbols(self):
        build = (ANDROID / "build.py").read_text(encoding="utf-8")
        self.assertIn("'-Wl,--no-undefined'", build)
        self.assertIn("[CC] + CC_EXTRA + LDFLAGS + ['-shared', '-Wl,-soname,libdmhost.so'", build)
        self.assertIn("[CC] + CC_EXTRA + LDFLAGS + ['-shared', '-Wl,-soname,libdmplay.so'", build)

    def test_phone_export_still_builds_and_links_standalone_native_library(self):
        pipeline = (ROOT / "porter" / "pipeline.py").read_text(encoding="utf-8")
        self.assertIn("'--lib'", pipeline)
        self.assertIn("self.so = out / ('libdmplay.so' if self.o.app else 'libdmhost.so')", pipeline)


if __name__ == "__main__":
    unittest.main()
