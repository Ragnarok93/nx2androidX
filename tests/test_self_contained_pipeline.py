import ast
import importlib.util
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class SelfContainedPipelineTests(unittest.TestCase):
    def test_pack_is_built_before_apk_assembly(self):
        tree = ast.parse((ROOT / "porter/pipeline.py").read_text(encoding="utf-8"))
        port = next(node for node in tree.body if isinstance(node, ast.ClassDef) and node.name == "Port")
        run = next(node for node in port.body if isinstance(node, ast.FunctionDef) and node.name == "_run")
        calls = [
            node.func.attr for node in sorted(ast.walk(run), key=lambda item: getattr(item, 'lineno', -1))
            if isinstance(node, ast.Call)
            and isinstance(node.func, ast.Attribute)
            and isinstance(node.func.value, ast.Name)
            and node.func.value.id == "self"
            and node.func.attr in {"host", "data", "apk", "install"}
        ]
        self.assertLess(calls.index("host"), calls.index("data"))
        self.assertLess(calls.index("data"), calls.index("apk"))
        self.assertLess(calls.index("apk"), calls.index("install"))

    def test_storage_budget_includes_embedded_archive_and_apk(self):
        source = (ROOT / "porter/pipeline.py").read_text(encoding="utf-8")
        self.assertIn("copies = 1 if self.o.app else 3 if self.o.self_contained else 2", source)
        self.assertIn("archive = game if self.o.self_contained else 0", source)
        self.assertIn("game * (2 if self.o.self_contained else 1)", source)

    def test_phone_payload_bundles_the_compiled_activity_dex(self):
        assets = (ROOT / "packaging/android-apk/build_assets.py").read_text(encoding="utf-8")
        pipeline = (ROOT / "porter/pipeline.py").read_text(encoding="utf-8")
        self.assertIn("compile_game_activity_dex(tool)", assets)
        self.assertIn("'game-activity.dex'", assets)
        self.assertIn("DM.parent.parent / 'tools' / 'game-activity.dex'", pipeline)
        self.assertIn("if prebuilt.is_file():", pipeline)

    def test_phone_build_requests_self_contained_apk(self):
        source = (ROOT / "android/app/src/main/java/com/nx/android/data/PortRunner.kt").read_text(encoding="utf-8")
        self.assertIn('"--self-contained-apk"', source)
        self.assertNotIn("NX2A_PACK_DIR", source)
        self.assertNotIn("packDir:", source)

    def test_native_activity_prefers_extracted_private_pack(self):
        source = (ROOT / "nxnative/dm/android/front_android.c").read_text(encoding="utf-8")
        self.assertIn('"%s/nx2a-data", a->internalDataPath', source)
        self.assertIn("data_dir_valid(embedded)", source)

    def test_mkapk_manifest_selects_a_real_launcher_activity(self):
        script = ROOT / "nxnative/dm/android/apk/mkapk.py"
        sys.path.insert(0, str(script.parent))
        original_argv = sys.argv[:]
        try:
            sys.argv = [str(script), "libdmhost.so", "game.apk", "key.json",
                        "--dex", "classes.dex", "--activity", "org.nx2a.GameActivity"]
            spec = importlib.util.spec_from_file_location("nx2a_mkapk_manifest_test", script)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            manifest = module.MANIFEST
            app = next(node for node in manifest[2] if node[0] == "application")
            activities = [node for node in app[2] if node[0] == "activity"]
            names = [next(value for _, key, value in node[1] if key == "name") for node in activities]
            self.assertEqual(names, ["org.nx2a.GameActivity"])
            self.assertTrue(any(child[0] == "intent-filter" for child in activities[0][2]))

            sys.argv = [str(script), "libdmhost.so", "game.apk", "key.json",
                        "--dex", "classes.dex", "--activity", "org.nx2a.GameActivity",
                        "--launcher-activity", "com.nx2a.launcher.LauncherActivity"]
            spec = importlib.util.spec_from_file_location("nx2a_mkapk_launcher_manifest_test", script)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            manifest = module.MANIFEST
            app = next(node for node in manifest[2] if node[0] == "application")
            activities = [node for node in app[2] if node[0] == "activity"]
            names = [next(value for _, key, value in node[1] if key == "name") for node in activities]
            self.assertEqual(names, ["com.nx2a.launcher.LauncherActivity", "org.nx2a.GameActivity"])
            self.assertTrue(any(child[0] == "intent-filter" for child in activities[0][2]))
            self.assertFalse(any(child[0] == "intent-filter" for child in activities[1][2]))
        finally:
            sys.argv = original_argv
            try: sys.path.remove(str(script.parent))
            except ValueError: pass

    def test_game_activity_is_launcher_when_optional_launcher_is_absent(self):
        mkapk = (ROOT / "nxnative/dm/android/apk/mkapk.py").read_text(encoding="utf-8")
        pipeline = (ROOT / "porter/pipeline.py").read_text(encoding="utf-8")
        self.assertIn("if ARGS.dex and ARGS.launcher_activity else []", mkapk)
        self.assertIn("[] if ARGS.dex and ARGS.launcher_activity else [", mkapk)
        self.assertIn("if launcher: args += ['--launcher-activity', 'com.nx2a.launcher.LauncherActivity']", pipeline)

    def test_first_launch_extracts_off_ui_thread_then_starts_native_host(self):
        java = (ROOT / "nxnative/dm/android/java/org/nx2a/GameActivity.java").read_text(encoding="utf-8")
        native = (ROOT / "nxnative/dm/android/front_android.c").read_text(encoding="utf-8")
        external = (ROOT / "nxnative/dm/android/java/org/nx2a/ExternalActivity.java").read_text(encoding="utf-8")
        self.assertLess(java.index("super.onCreate(state);"), java.index("prepareEmbeddedPack(this);"))
        self.assertIn('new Thread(() ->', java)
        self.assertIn("nx2a-data.sha256", java)
        self.assertIn("unsafe path in game data archive", java)
        self.assertIn("cannot activate extracted game data", java)
        self.assertIn('AAssetManager_open(a->assetManager, "nx2a-export-mode"', native)
        self.assertIn("Java_org_nx2a_GameActivity_embeddedPackReady", native)
        self.assertIn("GameActivity.prepareEmbeddedPack(this);", external)


if __name__ == "__main__":
    unittest.main()
