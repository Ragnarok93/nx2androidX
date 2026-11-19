"""Regression checks for APK + companion folder launches on modern Android.

These intentionally use no commercial game dump or multigigabyte fixtures.
They protect the exported manifest, public destination and movable pack paths.
"""
import ast
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from porter import pipeline


ROOT = Path(__file__).resolve().parents[1]


class SplitGameExportTests(unittest.TestCase):
    def test_phone_private_exports_are_moved_to_public_download_on_same_volume(self):
        cases = [
            ("/storage/emulated/0/Android/data/com.nx.android/files/exports",
             "/storage/emulated/0/Download/nx2android-ports"),
            ("/storage/ABCD-1234/Android/data/com.nx.android/files/exports",
             "/storage/ABCD-1234/Download/nx2android-ports"),
            ("/sdcard/Android/obb/com.nx.android/exports",
             "/sdcard/Download/nx2android-ports"),
            ("/data/user/0/com.nx.android/files/builds",
             "/sdcard/Download/nx2android-ports"),
            ("/data/data/com.nx.android/files/builds",
             "/sdcard/Download/nx2android-ports"),
        ]
        for source, expected in cases:
            with self.subTest(source=source):
                self.assertEqual(pipeline.public_split_export_dir(Path(source)), Path(expected))

    def test_explicit_public_or_desktop_destination_is_not_modified(self):
        for path in (Path("/storage/emulated/0/Download/my-ports"),
                     Path("/storage/ABCD-1234/Games"),
                     Path("/home/user/ports"), Path("C:/Users/Game/Exports")):
            with self.subTest(path=path):
                self.assertEqual(pipeline.public_split_export_dir(path), path)

    def test_split_data_stage_uses_public_export_and_relative_data_base(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            # Use an Android/data-shaped subtree inside the fixture to avoid
            # interacting with actual device storage.
            private = root / "storage" / "ABCD-1234" / "Android" / "data" / "com.nx.android" / "files" / "exports"
            private.mkdir(parents=True)
            obj = SimpleNamespace()
            obj.o = SimpleNamespace(app=False, self_contained=False, out=private,
                                    jobs=1, mt_replay=False, touch=False,
                                    vulkan_driver=None)
            obj.button_font = lambda: ([], {})
            obj.shared_font = lambda: ([], {})
            obj.fps_files = {}
            obj.game = root / "game"
            obj.patched = root / "patched"
            obj.ap = SimpleNamespace(romfs_zstd=False)
            obj.dmsv = None
            obj.aoc = None
            obj.sysdata = None
            obj.lang = "en-US"
            obj.package = "org.nx2a.fixture"
            obj.pkg_ver = "v0"
            obj.display = "1.0"
            obj.fps_env = []
            obj.profile_h = None
            obj.lists = lambda: {}
            obj.data_dir = lambda: obj.o.out / "Fixture-data"
            obj.result = {}
            obj.work = root
            obj.aw = root
            expected = root / "storage" / "ABCD-1234" / "Download" / "nx2android-ports"
            with mock.patch.object(pipeline.datapack, "build",
                                   return_value={"files": 0, "bytes": 0}) as build, \
                 mock.patch.object(pipeline, "PROG"), \
                 mock.patch.object(pipeline, "say"):
                pipeline.Port.data(obj, retry_split=True)
            self.assertEqual(obj.o.out, expected)
            self.assertTrue(expected.is_dir())
            self.assertEqual(obj.result["data"], str(expected / "Fixture-data"))
            self.assertEqual(build.call_args.kwargs["pack_base"], ".")
            self.assertFalse(build.call_args.kwargs["require_links"])

    def test_companion_descriptor_defers_native_start_until_java_importer(self):
        java = (ROOT / "nxnative/dm/android/java/org/nx2a/CompanionImporter.java").read_text(encoding="utf-8")
        front = (ROOT / "nxnative/dm/android/front_android.c").read_text(encoding="utf-8")
        launcher = (ROOT / "nxnative/dm/android/java/org/nx2a/GameActivity.java").read_text(encoding="utf-8")
        self.assertIn('AAssetManager_open(a->assetManager, "nx2a-companion.json"', front)
        self.assertIn('if (!atomic_load(&g_java_pack_ready)) return 0;', front)
        self.assertIn('atomic_store(&g_java_pack_ready, 1);', front)
        self.assertIn('CompanionImporter.prepare(activity)', launcher)
        self.assertIn('DocumentsContract.buildChildDocumentsUriUsingTree', java)
        self.assertIn('Intent.ACTION_OPEN_DOCUMENT_TREE', java)
        self.assertIn('manifest_sha256', java)
        self.assertIn('GameActivity.embeddedPackReady()', java)
        self.assertIn('new Thread(', java)
        self.assertIn('bakedDataPath(activity)', java)
        self.assertIn('GameActivity.companionPackReady(', java)
        self.assertIn('Java_org_nx2a_GameActivity_companionPackReady', front)
        self.assertIn('Not enough free storage', java)
        self.assertIn('if (!root.exists() && backup.isDirectory() && !backup.renameTo(root))', java)
        self.assertLess(java.index('GameActivity.deleteTree(partial);'), java.index('long free = new StatFs('))

    def test_bootstrap_draws_before_starting_game_native_activity(self):
        java = (ROOT / "nxnative/dm/android/java/org/nx2a/BootstrapActivity.java").read_text(encoding="utf-8")
        self.assertLess(java.index("setContentView(status"), java.index("CompanionImporter.prepare(this)"))
        self.assertIn("intent.setClassName(getPackageName(), activityName)", java)
        self.assertIn("void launchGame(String verifiedDataPath)", java)

    def test_standalone_apk_matches_builder_legacy_runtime_target(self):
        mkapk = (ROOT / "nxnative/dm/android/apk/mkapk.py").read_text(encoding="utf-8")
        builder = (ROOT / "android/app/build.gradle.kts").read_text(encoding="utf-8")
        self.assertIn("MIN_SDK, TARGET_SDK = 29, 28", mkapk)
        self.assertIn("targetSdk = 28", builder)
        self.assertIn("requestLegacyExternalStorage", mkapk)

    def test_all_standalone_pack_paths_can_be_relocated(self):
        code = (ROOT / "porter/pipeline.py").read_text(encoding="utf-8")
        front = (ROOT / "nxnative/dm/android/front_android.c").read_text(encoding="utf-8")
        self.assertIn("pack_base=str(dst) if self.o.app else '.'", code)
        self.assertIn("if not self.o.app: return ['DM_SHARED_FONT=standard.ttf']", code)
        self.assertIn('DM_SHARED_FONT_EXT={"nintendo_ext.ttf" if not self.o.app', code)
        self.assertIn('value[0] !=', front)
        self.assertIn('snprintf(resolved, sizeof resolved, "%s/%s", g_root, value)', front)


if __name__ == "__main__":
    unittest.main()
