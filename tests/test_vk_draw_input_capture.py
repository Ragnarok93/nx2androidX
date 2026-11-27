"""Execute the production capture against a deferred Vulkan buffer sink."""
from pathlib import Path
import csv
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
DM = ROOT / "nxnative/dm"


@unittest.skipUnless(shutil.which("cc"), "host C compiler required")
class DrawInputCapture(unittest.TestCase):
    def test_production_copy_range_lifetime_and_report(self):
        with tempfile.TemporaryDirectory() as folder:
            tmp = Path(folder)
            exe = tmp / "capture"
            subprocess.run([
                "cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                "-Wno-missing-field-initializers",
                "-I" + str(DM / "third_party/vulkan-headers/include"),
                "-I" + str(DM / "android"), str(ROOT / "tests/vk_draw_input_capture_harness.c"),
                "-o", str(exe),
            ], check=True)
            subprocess.run([exe], check=True, env={**os.environ, "DM_ROOT": folder})
            with (tmp / "gpu_draw_inputs.tsv").open() as report:
                rows = list(csv.DictReader(report, delimiter="\t"))
            self.assertEqual([row["kind"] for row in rows],
                             ["indexed-indirect", "index-binding-prefix", "indexed-direct"])
            self.assertEqual([rows[2][key] for key in
                              ("count", "stride", "binding_extent", "vertex_offset", "first_instance", "instances")],
                             ["3", "2", "6", "-2", "7", "1"])
            for row, offset, value in zip(rows, [32, 68, 60], [0xAB, 0xCD, 0xEF]):
                self.assertEqual(int(row["vk_buffer_offset"]), offset)
                self.assertEqual(row["draw"], "19")
                raw = bytes.fromhex(row["raw_hex"])
                self.assertEqual(raw, bytes([value]) * int(row["bytes"]))
                digest = 0xCBF29CE484222325
                for byte in raw:
                    digest = ((digest ^ byte) * 0x100000001B3) & ((1 << 64) - 1)
                self.assertEqual(int(row["fnv1a64"], 16), digest)
            self.assertFalse((tmp / "gpu_draw_inputs.tsv.tmp").exists())
