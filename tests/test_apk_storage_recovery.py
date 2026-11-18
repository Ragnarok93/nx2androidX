"""Regression coverage for interrupted self-contained APK exports on Android."""
import errno
import json
import os
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from porter import datapack, pipeline


class ApkStorageRecoveryTests(unittest.TestCase):
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

    def test_old_export_pack_is_only_removed_when_owned_and_without_saves(self):
        with tempfile.TemporaryDirectory() as root:
            p = object.__new__(pipeline.Port)
            p.o = SimpleNamespace(self_contained=True, out=Path(root))
            owned = Path(root) / "Example-data"
            owned.mkdir()
            (owned / "nx2a-data.json").write_text(json.dumps({"title_id": "0100000000000001", "package": "org.nx2a.example"}))
            (owned / "romfs.bin").write_bytes(b"old generated data")
            p.cleanup_legacy_self_contained_pack("Example", "0100000000000001", "org.nx2a.example")
            self.assertFalse(owned.exists())
            owned.mkdir()
            (owned / "nx2a-data.json").write_text(json.dumps({"title_id": "0100000000000001", "package": "org.nx2a.example"}))
            (owned / "saves").mkdir()
            p.cleanup_legacy_self_contained_pack("Example", "0100000000000001", "org.nx2a.example")
            self.assertTrue(owned.exists())


if __name__ == "__main__":
    unittest.main()
