"""Regression tests for standalone game APKs near Android's classic-ZIP limit.

No game dump or multi-gigabyte fixture is required: the archive writer is
stubbed at the boundary where an oversized game cannot fit in an APK.
"""
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from porter import pipeline


class FakePort:
    apk = pipeline.Port.apk
    fallback_split_apk = pipeline.Port.fallback_split_apk

    def __init__(self, root):
        self.aw = root / 'work'
        (self.aw / 'embed-pack').mkdir(parents=True)
        (self.aw / 'embed-pack' / 'nx2a-data.json').write_text('{}')
        self.o = SimpleNamespace(app=False, self_contained=True,
                                 out=root / 'exports', vercode_offset=0,
                                 vulkan_driver=None, external_display=False,
                                 native_keyboard=False, debug=False)
        self.o.out.mkdir()
        self.so = root / 'libdmhost.so'
        self.so.write_bytes(b'ELF')
        self.g = {'ipa': 'TestGame.ipa'}
        self.app = 0x0100000000010000
        self.app_name = 'Test Game'
        self.pkg_ver = 'v0'
        self.display = '1.0'
        self.package = 'org.nx2a.testgame'
        self.ap = SimpleNamespace(landscape=False)
        self.logs = root
        self.env = {}
        self.result = {}
        self.data_calls = 0

    def icon(self): return []
    def drivers(self): return []
    def loader_libs(self): return []
    def loader_so(self): return []
    # These tests stub APK assembly and test only the 4 GiB fallback. The real
    # Port validates and collects the loader/driver libraries separately.
    def apk_vulkan_libraries(self): return []
    def theme(self): return []
    def ui_font(self): return []
    def external_activity(self): return ['--dex', 'test-game-activity.dex', '--activity', 'org.nx2a.GameActivity']

    def data_dir(self):
        return self.aw / 'embed-pack' if self.o.self_contained else self.o.out / 'TestGame-data'

    def data(self, *, retry_split=False):
        self.data_calls += 1
        if not retry_split:
            raise AssertionError('fallback must reuse the completed compilation')
        if self.o.self_contained:
            raise AssertionError('fallback must switch off embedding before repacking')
        self.data_path = self.data_dir()
        self.data_path.mkdir()
        (self.data_path / 'nx2a-data.json').write_text('{}')
        self.result['data'] = str(self.data_path)


class ApkLimitFallbackTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.port = FakePort(self.root)
        self.progress = mock.patch.object(pipeline, 'PROG')
        self.progress.start()
        self.addCleanup(self.progress.stop)
        signing = mock.patch.object(pipeline.signing, 'export_key', return_value=self.root / 'signing.json')
        self.signing = signing.start()
        self.addCleanup(signing.stop)
        self.vercode = mock.patch.object(pipeline, 'version_code', return_value=1)
        self.vercode.start()
        self.addCleanup(self.vercode.stop)

    def fake_mkapk(self, cmd, *args, **kwargs):
        self.signing.assert_called_with('0100000000010000', package=self.port.package, previous_apk=Path(cmd[3]))
        self.assertEqual(cmd[4], self.root / 'signing.json')
        # Command index 3 is the exported APK path in the real mkapk invocation.
        self.assertIn('--data-root', cmd)
        self.assertIn('--dex', cmd)
        self.assertIn('nx2a-companion.json=', ' '.join(map(str, cmd)))
        Path(cmd[3]).write_bytes(b'signed APK')

    def test_oversized_embedded_game_exports_its_own_apk_with_data(self):
        error = ValueError('even maximum compatible ZIP compression cannot fit this game in one installable APK')
        with mock.patch.object(pipeline.datapack, 'write_embedded_zip', side_effect=error), \
             mock.patch.object(pipeline.shutil, 'disk_usage', return_value=SimpleNamespace(free=1 << 40)), \
             mock.patch.object(pipeline.nsp, 'run', side_effect=self.fake_mkapk):
            self.port.apk()

        self.assertEqual(self.port.data_calls, 1)
        self.assertFalse(self.port.o.self_contained)
        self.assertEqual(self.port.result['mode'], 'apk+data')
        self.assertFalse(self.port.result['self_contained'])
        self.assertTrue(Path(self.port.result['apk']).is_file())
        self.assertTrue(Path(self.port.result['data']).is_dir())
        self.assertFalse((self.port.o.out / '.TestGame.nx2a-data.zip.partial').exists())

    def test_low_export_space_falls_back_before_compression(self):
        with mock.patch.object(pipeline.datapack, 'write_embedded_zip') as compress, \
             mock.patch.object(pipeline.shutil, 'disk_usage', return_value=SimpleNamespace(free=1)), \
             mock.patch.object(pipeline.nsp, 'run', side_effect=self.fake_mkapk):
            self.port.apk()
        compress.assert_not_called()
        self.assertEqual(self.port.result['mode'], 'apk+data')
        self.assertTrue(Path(self.port.result['apk']).is_file())
        self.assertTrue(Path(self.port.result['data']).is_dir())

    def test_unrelated_archive_error_is_not_hidden_by_fallback(self):
        with mock.patch.object(pipeline.datapack, 'write_embedded_zip', side_effect=ValueError('bad input')), \
             mock.patch.object(pipeline.shutil, 'disk_usage', return_value=SimpleNamespace(free=1 << 40)):
            with self.assertRaises(pipeline.BuildError):
                self.port.apk()
        self.assertEqual(self.port.data_calls, 0)


if __name__ == '__main__':
    unittest.main()
