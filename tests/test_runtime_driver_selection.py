"""Standalone Vulkan driver selection, independent of game data and the Android NDK."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "nxnative/dm/android"

HARNESS = r'''
#include "driver_selection.h"
#include <stdio.h>
int main(int argc, char **argv) {
    char path[1024] = "";
    if (argc != 2) return 1;
    int selected = dm_driver_selection(argv[1], path, sizeof path);
    printf("%d\n%s\n", selected, selected == DM_DRIVER_CUSTOM ? path : "");
    return 0;
}
'''

class DriverSelectionTest(unittest.TestCase):
    def test_selection_survives_pack_replacement_and_rejects_invalid_paths(self):
        cc = shutil.which("cc") or shutil.which("clang")
        if not cc:
            self.skipTest("C compiler not available")
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "selection.c"
            source.write_text(HARNESS)
            executable = root / "selection"
            subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-I", str(HEADER), str(source), "-o", str(executable)], check=True)
            marker = root / "driver-selection"
            def selected():
                return subprocess.check_output([str(executable), str(root)], text=True).splitlines()
            self.assertEqual(selected()[0], "0")  # preserve APK default
            marker.write_text("system\n")
            self.assertEqual(selected()[0], "1")
            relative = "drivers/" + "a" * 32 + "/vulkan.radeon.so"
            library = root / relative
            library.parent.mkdir(parents=True)
            library.write_bytes(b"driver")
            marker.write_text(relative + "\n")
            self.assertEqual(selected(), ["2", str(library)])
            pack = root / "nx2a-data"
            pack.mkdir()
            shutil.rmtree(pack)
            pack.mkdir()
            self.assertEqual(selected(), ["2", str(library)])
            for invalid in ("../driver.so", "/system/lib64/libvulkan.so",
                            "drivers/" + "a"*32 + "/../bad.so",
                            "drivers/" + "a"*32 + "/missing.so", relative + "\x00hidden",
                            "x"*500):
                marker.write_text(invalid)
                self.assertEqual(selected()[0], "3", invalid)
            marker.write_text("system")
            self.assertEqual(selected()[0], "1")

if __name__ == "__main__":
    unittest.main()
