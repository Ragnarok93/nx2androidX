import hashlib
import importlib.util
import os
import shutil
import struct
import subprocess
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



def _take_lp(data, offset):
    size = struct.unpack_from("<I", data, offset)[0]
    start = offset + 4
    return data[start:start + size], start + size


def _assert_v2_digest(test, apk):
    eocd_offset = len(apk) - 22
    eocd = apk[eocd_offset:]
    cd_offset = struct.unpack_from("<I", eocd, 16)[0]
    block_size = struct.unpack_from("<Q", apk, cd_offset - 24)[0] + 8
    block_start = cd_offset - block_size
    pair_start = block_start + 8
    pair_size = struct.unpack_from("<Q", apk, pair_start)[0]
    pair_id = struct.unpack_from("<I", apk, pair_start + 8)[0]
    test.assertEqual(pair_id, 0x7109871a)
    value = apk[pair_start + 12:pair_start + 8 + pair_size]
    outer, _ = _take_lp(value, 0)
    signer, _ = _take_lp(outer, 0)
    signed, _ = _take_lp(signer, 0)
    digest_list, _ = _take_lp(signed, 0)
    digest_record, _ = _take_lp(digest_list, 0)
    algorithm = struct.unpack_from("<I", digest_record, 0)[0]
    expected_digest, _ = _take_lp(digest_record, 4)
    test.assertEqual(algorithm, 0x0103)
    chunks = []
    for section in (apk[:block_start], apk[cd_offset:eocd_offset], eocd):
        for start in range(0, len(section), 1 << 20):
            chunk = section[start:start + (1 << 20)]
            chunks.append(hashlib.sha256(b"\xa5" + struct.pack("<I", len(chunk)) + chunk).digest())
    actual_digest = hashlib.sha256(b"\x5a" + struct.pack("<I", len(chunks)) + b"".join(chunks)).digest()
    test.assertEqual(expected_digest, actual_digest)

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
            signed_apk = output.read_bytes()
            self.assertIn(b"APK Sig Block 42", signed_apk)
            _assert_v2_digest(self, signed_apk)
            sdk = os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT")
            candidates = [Path(shutil.which("apksigner"))] if shutil.which("apksigner") else []
            if sdk:
                candidates.extend(Path(sdk).glob("build-tools/*/apksigner"))
            apksigner = next((candidate for candidate in candidates if candidate and candidate.is_file()), None)
            if apksigner:
                verified = subprocess.run([str(apksigner), "verify", "--verbose", "--min-sdk-version", "29", str(output)],
                                          capture_output=True, text=True)
                self.assertEqual(verified.returncode, 0, verified.stdout + verified.stderr)

    def test_legacy_v2_signer_hashes_the_adjusted_eocd(self):
        import io
        entries = [("AndroidManifest.xml", b"manifest", 4)]
        zip_entries, central, eocd = apksign.build_zip(entries)
        key = apksign.gen_key()
        certificate = apksign.cert(key)
        signed = apksign.sign_v2(zip_entries, central, eocd, key, certificate)
        with zipfile.ZipFile(io.BytesIO(signed)) as archive:
            self.assertEqual(archive.read("AndroidManifest.xml"), b"manifest")
        self.assertIn(b"APK Sig Block 42", signed)
        _assert_v2_digest(self, signed)

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
