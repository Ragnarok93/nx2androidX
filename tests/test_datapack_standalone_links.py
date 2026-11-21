"""Android private app storage may reject hard links despite sharing a filesystem."""
import errno
import tempfile
import unittest
import zipfile
from pathlib import Path
from types import SimpleNamespace
from unittest import mock
from porter import datapack


class StandaloneLinkRecoveryTests(unittest.TestCase):
    def test_permission_denied_hardlink_stages_reference_without_copy(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            source = root / 'work' / 'patched' / 'mods'
            source.mkdir(parents=True)
            original = source / 'main.patched.bin'
            original.write_bytes(b'compiled module' * 8192)
            stage = root / 'work' / 'embed-pack.partial' / 'mods'
            with mock.patch.object(datapack.os, 'link', side_effect=PermissionError(errno.EACCES, 'denied')):
                count, byte_count = datapack.link_tree(source, stage, require_links=True)
            staged = stage / original.name
            self.assertTrue(staged.is_symlink(), 'fallback must not double the game data')
            self.assertEqual(staged.read_bytes(), original.read_bytes())
            self.assertEqual((count, byte_count), (1, original.stat().st_size))
            (root / 'work' / 'embed-pack.partial').rename(root / 'work' / 'embed-pack')
            self.assertEqual((root / 'work' / 'embed-pack' / 'mods' / original.name).read_bytes(), original.read_bytes())

    def test_matching_prior_companion_keeps_manifest_and_skips_copy(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            romfs = root / 'RomFS'
            romfs.mkdir()
            (romfs / 'game.bin').write_bytes(b'game')
            profile = SimpleNamespace(package='org.game', id='0000000000000001', name='Game',
                                      env=[], settings={}, requires=[], status='playable', graphics='nvn', lan=False)
            destination = root / 'Game-data'
            params = dict(profile=profile, version='v0', display='1.0', patched=None,
                          romfs=romfs, shaders=None, pack_base='.')
            with mock.patch.object(datapack.os, 'link', side_effect=PermissionError):
                datapack.build(destination, **params)
            manifest = destination / datapack.MANIFEST
            original = manifest.read_bytes()
            saves = destination / 'saves'
            saves.mkdir()
            (saves / 'backup').write_text('save')
            with mock.patch.object(datapack, 'link_or_copy', side_effect=AssertionError('recopied')):
                datapack.build(destination, **params)
            self.assertEqual(manifest.read_bytes(), original)
            self.assertEqual((saves / 'backup').read_text(), 'save')
            (destination / 'RomFS/game.bin').write_bytes(b'bad!')
            datapack.build(destination, **params)
            self.assertEqual((destination / 'RomFS/game.bin').read_bytes(), b'game')
            self.assertEqual((saves / 'backup').read_text(), 'save')

    def test_embedded_archive_contains_resolved_file_contents_not_symlink(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            source = root / 'patched'
            source.mkdir()
            (source / 'main.patched.bin').write_bytes(b'game module data')
            stage = root / 'embed-pack'
            with mock.patch.object(datapack.os, 'link', side_effect=PermissionError(errno.EPERM, 'denied')):
                datapack.link_tree(source, stage / 'mods', require_links=True)
            (stage / 'nx2a-data.json').write_text('{}')
            output = root / 'game.zip'
            with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED) as zf:
                for path in datapack.embedded_files(stage):
                    zf.write(path, path.relative_to(stage).as_posix())
            with zipfile.ZipFile(output) as zf:
                self.assertEqual(zf.read('mods/main.patched.bin'), b'game module data')
                self.assertEqual(zf.read('nx2a-data.json'), b'{}')
                self.assertIsNone(zf.testzip())

    def test_non_standalone_pack_copies_when_link_unavailable(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            src, dst = root / 'source', root / 'target'
            src.write_bytes(b'ordinary pack')
            with mock.patch.object(datapack.os, 'link', side_effect=OSError(errno.EXDEV, 'different volume')):
                datapack.link_or_copy(src, dst)
            self.assertEqual(dst.read_bytes(), src.read_bytes())
            self.assertFalse(dst.is_symlink())

    def test_never_silently_copies_when_hardlinks_and_symlinks_are_denied(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            src, dst = root / 'source', root / 'target'
            src.write_bytes(b'avoid duplicate storage')
            with mock.patch.object(datapack.os, 'link', side_effect=PermissionError(errno.EACCES, 'denied')), \
                 mock.patch.object(datapack.os, 'symlink', side_effect=PermissionError(errno.EACCES, 'denied')):
                with self.assertRaisesRegex(OSError, 'symbolic link'):
                    datapack.link_or_copy(src, dst, require_links=True)
            self.assertFalse(dst.exists())


if __name__ == '__main__':
    unittest.main()
