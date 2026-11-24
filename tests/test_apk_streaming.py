import hashlib
import importlib.util
import json
import sys
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
    normalized_eocd = eocd[:16] + struct.pack('<I', block_start) + eocd[20:]
    for section in (apk[:block_start], apk[cd_offset:eocd_offset], normalized_eocd):
        for start in range(0, len(section), 1 << 20):
            chunk = section[start:start + (1 << 20)]
            chunks.append(hashlib.sha256(b"\xa5" + struct.pack("<I", len(chunk)) + chunk).digest())
    actual_digest = hashlib.sha256(b"\x5a" + struct.pack("<I", len(chunks)) + b"".join(chunks)).digest()
    test.assertEqual(expected_digest, actual_digest)

class StreamingApkTests(unittest.TestCase):
    def test_standalone_update_preserves_package_version_and_certificate(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            library = root / "fixture.so"
            library.write_bytes(b"fixture native host")
            key = root / "game-key.json"
            package = "org.nx2a.upgrade.fixture"
            first, second = root / "first.apk", root / "second.apk"
            tool = ROOT / "nxnative/dm/android/apk/mkapk.py"
            for apk, version in ((first, 42), (second, 7)):
                subprocess.run([sys.executable, str(tool), str(library), str(apk), str(key),
                                "--package", package, "--version-code", str(version),
                                "--preserve-version-floor"], capture_output=True, check=True)
                _assert_v2_digest(self, apk.read_bytes())
                cert = bytes.fromhex(json.loads(key.read_text())["cert"])
                self.assertIn(cert, apk.read_bytes())
            with zipfile.ZipFile(first) as before, zipfile.ZipFile(second) as after:
                self.assertEqual(before.read("AndroidManifest.xml"), after.read("AndroidManifest.xml"))
                self.assertIn(package.encode("utf-16le"), after.read("AndroidManifest.xml"))
            with apksign.upgrade_version(key, "org.nx2a.other", 7) as version:
                self.assertEqual(version, 7)

    def test_runtime_upgrade_replaces_code_only(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            apkdir = ROOT / "nxnative/dm/android/apk"
            old, new = root / "old.apk", root / "new.apk"
            host, dex = root / "host.so", root / "classes.dex"
            host.write_bytes(b"old host")
            dex.write_bytes(b"old dex")
            payload = root / "data.zip"
            payload.write_bytes(b"game assets" * 200000)
            key = root / "key.json"
            subprocess.run([sys.executable, str(apkdir / "mkapk.py"), str(host), str(old), str(key),
                            "--package", "org.nx2a.runtimefixture", "--version-code", "42", "--lib", "dmhost",
                            "--dex", str(dex), "--activity", "org.nx2a.GameActivity",
                            "--asset", f"nx2a-data.zip={payload}"], check=True, capture_output=True)
            host.write_bytes(b"new host")
            dex.write_bytes(b"new dex")
            args = [sys.executable, str(apkdir / "apkdriver.py"), str(old), str(new), str(key),
                    "--host", str(host), "--dex", str(dex)]
            subprocess.run(args, check=True, capture_output=True)
            with zipfile.ZipFile(old) as before, zipfile.ZipFile(new) as after:
                self.assertEqual(before.namelist(), after.namelist())
                self.assertEqual(after.read("classes.dex"), b"new dex")
                self.assertEqual(after.read("lib/arm64-v8a/libdmhost.so"), b"new host")
                for name in before.namelist():
                    if name not in ("classes.dex", "lib/arm64-v8a/libdmhost.so"):
                        self.assertEqual(before.read(name), after.read(name))
            _assert_v2_digest(self, new.read_bytes())
            self.assertIn(bytes.fromhex(json.loads(key.read_text())["cert"]), new.read_bytes())
            other = root / "other.json"
            subprocess.run([sys.executable, str(apkdir / "mkapk.py"), str(host), str(root / "other.apk"), str(other),
                            "--package", "org.nx2a.other"], check=True, capture_output=True)
            args[4] = str(other)
            before = new.read_bytes()
            self.assertNotEqual(subprocess.run(args, capture_output=True).returncode, 0)
            self.assertEqual(new.read_bytes(), before)

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
