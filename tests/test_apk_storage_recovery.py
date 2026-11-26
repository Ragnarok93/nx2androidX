"""Regression coverage for interrupted self-contained APK exports on Android."""
import errno
import json
import os
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from porter import cli, datapack, pipeline
from porter.upstream import BuildError, nsp


class ApkStorageRecoveryTests(unittest.TestCase):
    def test_tool_kill_propagates_as_interruption(self):
        for code, expected in ((-9, 137), (-15, 143), (1, 1), (-11, 1)):
            with self.subTest(code=code):
                def build():
                    try:
                        raise nsp.BuildToolError("compiler stopped", code)
                    except BuildError as error:
                        raise BuildError("stage failed") from error
                self.assertEqual(cli._with_progress("off", build), expected)

    def test_standalone_pack_is_staged_in_private_workspace(self):
        with tempfile.TemporaryDirectory() as root:
            port = object.__new__(pipeline.Port)
            port.o = SimpleNamespace(app=False, self_contained=True, out=Path(root) / "Downloads")
            port.aw = Path(root) / "work" / "android"
            port.g = {"ipa": "Example.ipa"}
            port.app_name = "Example"
            self.assertEqual(port.data_dir(), port.aw / "embed-pack")

    def test_private_staging_uses_hardlinks_not_another_romfs_copy(self):
        with tempfile.TemporaryDirectory() as root:
            source = Path(root) / "extract"
            source.mkdir()
            original = source / "romfs.bin"
            original.write_bytes(b"romfs data" * 4096)
            destination = Path(root) / "stage"
            datapack.link_tree(source, destination, require_links=True)
            self.assertEqual(original.stat().st_ino, (destination / "romfs.bin").stat().st_ino)

    def test_denied_hardlink_does_not_silently_copy_gigabytes(self):
        with tempfile.TemporaryDirectory() as root:
            src = Path(root) / "src"
            dst = Path(root) / "dst"
            src.write_bytes(b"data")
            with mock.patch.object(datapack.os, "link", side_effect=OSError(errno.EACCES, "permission denied")):
                datapack.link_or_copy(src, dst, require_links=True)
            self.assertTrue(dst.is_symlink())
            self.assertEqual(dst.read_bytes(), src.read_bytes())

    def test_android_private_and_emulated_storage_share_a_free_space_budget(self):
        with mock.patch.object(pipeline.resources, "is_android", return_value=True):
            self.assertTrue(pipeline.shares_disk_budget(
                Path("/data/user/0/com.nx.android/files/work"),
                Path("/storage/emulated/0/Download")))
            self.assertFalse(pipeline.shares_disk_budget(
                Path("/data/user/0/com.nx.android/files/work"),
                Path("/storage/0123-4567/Ports")))

    def test_interrupted_archive_cleanup_keeps_previous_export(self):
        with tempfile.TemporaryDirectory() as root:
            p = object.__new__(pipeline.Port)
            p.o = SimpleNamespace(self_contained=True, out=Path(root))
            owned = Path(root) / "Example-data"
            owned.mkdir()
            (owned / "nx2a-data.json").write_text(json.dumps({"title_id": "0100000000000001", "package": "org.nx2a.example"}))
            interrupted = Path(root) / ".Example.nx2a-data.zip.partial"
            interrupted.write_bytes(b"partial")
            p.cleanup_interrupted_archive("Example")
            self.assertTrue(owned.exists())
            self.assertFalse(interrupted.exists())

    def test_embedded_archive_retries_stronger_deflate_and_remains_readable(self):
        import random
        import zipfile
        with tempfile.TemporaryDirectory() as root:
            folder = Path(root)
            pack = folder / "pack"
            pack.mkdir()
            rng = random.Random(2)
            data = bytearray()
            for _ in range(1200):
                chunk = rng.randbytes(64)
                data.extend(chunk + b"A" * 500 + chunk + b"B" * 500)
            (pack / "game.bin").write_bytes(data)
            paths = []
            for level in (1, 9):
                output = folder / f"trial-{level}.zip"
                with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED,
                                     compresslevel=level) as zf:
                    zf.write(pack / "game.bin", "game.bin")
                paths.append(output.stat().st_size)
            self.assertLess(paths[1], paths[0])
            budget = sum(paths) // 2
            output = folder / "embedded.zip"
            size, level = datapack.write_embedded_zip(pack, output, budget)
            self.assertEqual(level, 9)
            self.assertLess(size, budget)
            with zipfile.ZipFile(output) as zf:
                self.assertIsNone(zf.testzip())
                self.assertEqual(zf.read("game.bin"), data)

    def test_uncompressible_pack_reports_limit_instead_of_false_success(self):
        with tempfile.TemporaryDirectory() as root:
            pack = Path(root) / "pack"
            pack.mkdir()
            (pack / "random.bin").write_bytes(os.urandom(256 * 1024))
            with self.assertRaisesRegex(ValueError, "cannot fit this game"):
                datapack.write_embedded_zip(pack, Path(root) / "oversized.zip", 16 * 1024)

    def test_large_incompressible_file_stops_at_budget_instead_of_writing_entire_file(self):
        with tempfile.TemporaryDirectory() as root:
            pack = Path(root) / "pack"
            pack.mkdir()
            (pack / "already-compressed.ucas").write_bytes(os.urandom(12 << 20))
            archive = Path(root) / "partial.zip"
            with self.assertRaisesRegex(ValueError, "cannot fit this game"):
                datapack.write_embedded_zip(pack, archive, 1 << 20)
            # The 12 MiB IoStore-like file must be stopped mid-stream rather
            # than using zf.write() to exhaust the APK export volume.
            self.assertLess(archive.stat().st_size, 3 << 20)

    def test_empty_apk_data_budget_is_rejected_before_writing(self):
        with tempfile.TemporaryDirectory() as root:
            pack = Path(root) / "pack"
            pack.mkdir()
            output = Path(root) / "embedded.zip"
            with self.assertRaisesRegex(ValueError, "no space"):
                datapack.write_embedded_zip(pack, output, 0)
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
