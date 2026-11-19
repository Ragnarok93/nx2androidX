"""APK Vulkan loader/ICD packaging regressions (no device or NDK required)."""
import os
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
os.environ.setdefault('NX2A_HOME', str(Path(tempfile.gettempdir()) / 'nx2a_test_vulkan_home'))

from porter.pipeline import ADRENOTOOLS_LIBS, Port, BuildError  # noqa: E402


class VulkanApkLibraryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='nx2a-vulkan-apk-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.host = self.root / 'host'
        self.host.mkdir()
        self.loader = self.root / 'prebuilt'
        self.loader.mkdir()
        for name in ADRENOTOOLS_LIBS:
            (self.loader / name).write_bytes(b'prebuilt:' + name.encode())
        self.port = object.__new__(Port)
        self.port.so = self.host / 'libdmhost.so'
        self.port.so.write_bytes(b'host')
        self.port.o = SimpleNamespace(vulkan_driver=None, vulkan_driver_dir=None)
        self.port.loader_libs = lambda: [self.loader / name for name in ADRENOTOOLS_LIBS[1:]]
        self.port.loader_so = lambda: [self.loader / ADRENOTOOLS_LIBS[0]]

    def test_radv_xclipse_uses_prebuilt_loader_not_missing_host_copy(self):
        driver = self.root / 'vulkan.radeon.so'
        driver.write_bytes(b'xclipse-radv')
        self.port.o.vulkan_driver = driver
        libs = self.port.apk_vulkan_libraries()
        names = [p.name for p in libs]
        self.assertEqual(len(names), len(set(names)))
        self.assertEqual(set(names), set(ADRENOTOOLS_LIBS) | {'libvulkan.radeon.so'})
        self.assertEqual((self.host / 'libvulkan.radeon.so').read_bytes(), b'xclipse-radv')
        self.assertFalse((self.host / 'libadrenotools.so').exists())
        for lib in libs:
            self.assertTrue(lib.is_file(), lib)

    def test_every_driver_from_directory_is_android_extractable(self):
        drivers = self.root / 'drivers'
        drivers.mkdir()
        for name in ('vulkan.radeon.so', 'vulkan.freedreno.so'):
            (drivers / name).write_bytes(name.encode())
        self.port.o.vulkan_driver_dir = drivers
        names = {p.name for p in self.port.apk_vulkan_libraries()}
        self.assertIn('libvulkan.radeon.so', names)
        self.assertIn('libvulkan.freedreno.so', names)
        self.assertNotIn('vulkan.radeon.so', names)

    def test_loader_is_packaged_even_without_a_default_driver(self):
        self.assertEqual(
            {p.name for p in self.port.apk_vulkan_libraries()},
            set(ADRENOTOOLS_LIBS),
        )

    def test_missing_prebuilt_loader_fails_early(self):
        (self.loader / 'libadrenotools.so').unlink()
        with self.assertRaisesRegex(BuildError, 'libadrenotools.so'):
            self.port.apk_vulkan_libraries()

    def test_conflicting_driver_names_cannot_override_loader(self):
        driver = self.root / 'libadrenotools.so'
        driver.write_bytes(b'not-loader')
        self.port.o.vulkan_driver = driver
        with self.assertRaisesRegex(BuildError, 'conflicts with the port runtime'):
            self.port.apk_vulkan_libraries()

    def test_duplicate_driver_name_with_different_content_is_rejected(self):
        one = self.root / 'one'
        two = self.root / 'two'
        one.mkdir()
        two.mkdir()
        (one / 'vulkan.radeon.so').write_bytes(b'one')
        (two / 'vulkan.radeon.so').write_bytes(b'two')
        self.port.o.vulkan_driver = one / 'vulkan.radeon.so'
        self.port.o.vulkan_driver_dir = two
        with self.assertRaisesRegex(BuildError, 'filename collision'):
            self.port.apk_vulkan_libraries()


if __name__ == '__main__':
    unittest.main()
