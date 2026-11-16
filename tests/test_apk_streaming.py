import importlib.util
import tempfile
import unittest
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "nx2a_apksign", ROOT / "nxnative/dm/android/apk/apksign.py"
)
apksign = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(apksign)


class StreamingApkTests(unittest.TestCase):
    def test_v2_signed_apk_keeps_zip_entries_readable(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            payload = root / "asset.bin"
            payload.write_bytes(b"embedded game data" * 4096)
            entries = [
                ("AndroidManifest.xml", b"manifest", 4),
                ("assets/nx2a-data.zip", payload, 4),
            ]
            key = apksign.gen_key()
            certificate = apksign.cert(key)
            staged = root / "staged.zip"
            output = root / "signed.apk"
            cd_offset, cd_size, eocd_offset, eocd = apksign.build_zip_file(entries, staged)
            apksign.sign_v2_file(staged, output, cd_offset, cd_size, eocd_offset, eocd, key, certificate)
            with zipfile.ZipFile(output) as archive:
                self.assertIsNone(archive.testzip())
                self.assertEqual(archive.read("assets/nx2a-data.zip"), payload.read_bytes())
            self.assertIn(b"APK Sig Block 42", output.read_bytes()[-64:])

    def test_legacy_v2_signer_hashes_the_adjusted_eocd(self):
        import io
        entries = [("AndroidManifest.xml", b"manifest", 4)]
        zip_entries, central, eocd = apksign.build_zip(entries)
        key = apksign.gen_key()
        certificate = apksign.cert(key)
        signed = apksign.sign_v2(zip_entries, central, eocd, key, certificate)
        with zipfile.ZipFile(io.BytesIO(signed)) as archive:
            self.assertEqual(archive.read("AndroidManifest.xml"), b"manifest")
        self.assertIn(b"APK Sig Block 42", signed[-64:])

    def test_file_backed_zip_entries_round_trip(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            payload = root / "large.bin"
            expected = (b"nx2androidX-streaming-test-" * 65536) + b"end"
            payload.write_bytes(expected)
            output = root / "staged.zip"
            entries = [
                ("AndroidManifest.xml", b"manifest", 4),
                ("lib/arm64-v8a/libdmhost.so", payload, 16384),
                ("assets/nx2a-data.sha256", b"0" * 64 + b"\n", 4),
            ]

            cd_offset, cd_size, eocd_offset, eocd = apksign.build_zip_file(entries, output)

            self.assertGreater(cd_offset, len(expected))
            self.assertGreater(cd_size, 0)
            self.assertGreater(eocd_offset, cd_offset)
            self.assertEqual(output.stat().st_size, eocd_offset + len(eocd))
            with zipfile.ZipFile(output) as archive:
                self.assertIsNone(archive.testzip())
                self.assertEqual(archive.read("AndroidManifest.xml"), b"manifest")
                self.assertEqual(archive.read("lib/arm64-v8a/libdmhost.so"), expected)
                self.assertEqual(archive.read("assets/nx2a-data.sha256"), b"0" * 64 + b"\n")


if __name__ == "__main__":
    unittest.main()
