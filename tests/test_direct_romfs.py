import tempfile
import unittest
from pathlib import Path

from porter import direct_romfs


class DirectRomfsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.dst = Path(self.temp.name) / 'Game-data'
        self.identity = dict(title_id='0123456789ABCDEF', package='org.example.game', version='v65536')

    def test_staged_romfs_is_reused_without_copy_or_public_manifest(self):
        rom = direct_romfs.begin(self.dst, self.identity)
        self.assertIsNotNone(rom)
        (rom / 'game.bin').write_bytes(b'game')
        workspace = Path(self.temp.name) / 'private'
        workspace.symlink_to(rom, target_is_directory=True)
        self.assertIsNone(direct_romfs.ready_receipt(self.dst))
        direct_romfs.finish(self.dst, self.identity)
        self.assertEqual(direct_romfs.ready_receipt(self.dst)['version'], 'v65536')
        self.assertTrue((workspace / 'game.bin').samefile(rom / 'game.bin'))
        self.assertFalse((self.dst / 'nx2a-data.json').exists())
        self.assertIsNone(direct_romfs.begin(self.dst, self.identity))
        self.assertEqual((rom / 'game.bin').read_bytes(), b'game')

    def test_interrupted_extraction_restarts_only_its_own_pending_tree(self):
        rom = direct_romfs.begin(self.dst, self.identity)
        self.assertIsNotNone(rom)
        (rom / 'incomplete.bin').write_bytes(b'partial')
        self.assertIsNone(direct_romfs.ready_receipt(self.dst))
        self.assertIsNone(direct_romfs.begin(self.dst, dict(self.identity, version='v0')))
        self.assertTrue((rom / 'incomplete.bin').exists())
        self.assertEqual(direct_romfs.begin(self.dst, self.identity), rom)
        self.assertFalse((rom / 'incomplete.bin').exists())
        direct_romfs.discard_pending(self.dst, self.identity)
        self.assertFalse(self.dst.exists())

    def test_existing_pack_and_unknown_files_are_not_modified(self):
        self.dst.mkdir()
        manifest = self.dst / 'nx2a-data.json'
        manifest.write_text('existing')
        self.assertIsNone(direct_romfs.begin(self.dst, self.identity))
        self.assertEqual(manifest.read_text(), 'existing')
        manifest.unlink()
        self.assertIsNone(direct_romfs.begin(self.dst, self.identity))
        self.dst.rmdir()
        rom = direct_romfs.begin(self.dst, self.identity)
        self.assertIsNotNone(rom)
        (self.dst / 'unrelated.txt').write_text('keep')
        self.assertIsNone(direct_romfs.begin(self.dst, self.identity))
        direct_romfs.discard_pending(self.dst, self.identity)
        self.assertTrue((self.dst / 'unrelated.txt').exists())
        self.assertTrue(rom.is_dir())

    def test_symlink_is_never_used_as_output(self):
        other = Path(self.temp.name) / 'other'
        other.mkdir()
        self.dst.symlink_to(other, target_is_directory=True)
        self.assertIsNone(direct_romfs.begin(self.dst, self.identity))
        self.assertEqual(list(other.iterdir()), [])


if __name__ == '__main__':
    unittest.main()
