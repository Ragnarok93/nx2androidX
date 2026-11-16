import ast
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class SelfContainedPipelineTests(unittest.TestCase):
    def test_pack_is_built_before_apk_assembly(self):
        tree = ast.parse((ROOT / "porter/pipeline.py").read_text(encoding="utf-8"))
        port = next(node for node in tree.body if isinstance(node, ast.ClassDef) and node.name == "Port")
        run = next(node for node in port.body if isinstance(node, ast.FunctionDef) and node.name == "_run")
        calls = [
            node.func.attr for node in ast.walk(run)
            if isinstance(node, ast.Call)
            and isinstance(node.func, ast.Attribute)
            and isinstance(node.func.value, ast.Name)
            and node.func.value.id == "self"
            and node.func.attr in {"host", "data", "apk", "install"}
        ]
        self.assertLess(calls.index("host"), calls.index("data"))
        self.assertLess(calls.index("data"), calls.index("apk"))
        self.assertLess(calls.index("apk"), calls.index("install"))

    def test_phone_build_requests_self_contained_apk(self):
        source = (ROOT / "android/app/src/main/java/com/nx/android/data/PortRunner.kt").read_text(encoding="utf-8")
        self.assertIn('"--self-contained-apk"', source)
        self.assertNotIn("NX2A_PACK_DIR", source)
        self.assertNotIn("packDir:", source)

    def test_native_activity_prefers_extracted_private_pack(self):
        source = (ROOT / "nxnative/dm/android/front_android.c").read_text(encoding="utf-8")
        self.assertIn('"%s/nx2a-data", a->internalDataPath', source)
        self.assertIn("data_dir_valid(embedded)", source)

    def test_first_launch_extracts_before_native_activity_startup(self):
        source = (ROOT / "nxnative/dm/android/java/org/nx2a/GameActivity.java").read_text(encoding="utf-8")
        self.assertLess(source.index("extractEmbeddedPack(this);"), source.index("super.onCreate(state);"))
        self.assertIn("nx2a-data.sha256", source)
        self.assertIn("unsafe path in game data archive", source)
        self.assertIn("cannot activate extracted game data", source)


if __name__ == "__main__":
    unittest.main()
