import hashlib
import importlib.util
import io
import json
import sys
import os
import shutil
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch
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

    def test_title_signing_is_reproducible_and_preserves_legacy_exports(self):
        from porter import signing
        from porter.upstream import BuildError

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            legacy = root / "signing-key.json"
            seed = bytes(range(32)).hex()
            seed_path = legacy.with_suffix(".seed-v1")
            seed_path.write_text(seed + "\n")
            library = root / "host.so"
            library.write_bytes(b"runtime fixture")
            title, other = "0100000000010000", "01000000000AB000"
            package = "org.nx2a.signing.fixture"
            apk_tools = signing.paths.DM / "android" / "apk"
            dex = root / "classes.dex"
            dex.write_bytes(b"dex fixture")

            def build(name, key, version):
                apk = root / (name + ".apk")
                subprocess.run([sys.executable, str(apk_tools / "mkapk.py"), str(library), str(apk),
                                str(key), "--lib", "dmhost", "--dex", str(dex), "--package", package,
                                "--version-code", str(version), "--preserve-version-floor"],
                               check=True, capture_output=True)
                _assert_v2_digest(self, apk.read_bytes())
                with zipfile.ZipFile(apk) as z:
                    return apk, signing.source_certificate(apk, z.start_dir)

            with patch.object(signing.paths, "signing_key", return_value=legacy):
                key = signing.export_key(title)
                first, identity = build("first", key, 42)
                self.assertEqual(hashlib.sha256(identity[0]).hexdigest(),
                                 "1551c62c47add16a6ecd4017fbc9e91c3c92c938a1d4503ec32a78d0a5c93ae9")
                key.unlink()
                second, second_identity = build("second", signing.export_key(title), 7)
                self.assertEqual(identity, second_identity)
                with zipfile.ZipFile(first) as before, zipfile.ZipFile(second) as after:
                    self.assertEqual(before.read("AndroidManifest.xml"), after.read("AndroidManifest.xml"))
                other_apk, other_identity = build("another-title", signing.export_key(other.lower()), 42)
                self.assertNotEqual(identity, other_identity)
                self.assertEqual(signing.original_key(second), key)
                upgraded = root / "runtime-upgrade.apk"
                original_key = signing.original_key(first, title=title)
                subprocess.run([sys.executable, str(apk_tools / "apkdriver.py"), str(first),
                                str(upgraded), str(original_key), "--host", str(library), "--dex", str(dex)],
                               check=True, capture_output=True)
                _assert_v2_digest(self, upgraded.read_bytes())
                with zipfile.ZipFile(upgraded) as z:
                    self.assertEqual(identity, signing.source_certificate(upgraded, z.start_dir))
                with self.assertRaisesRegex(BuildError, "original key"):
                    signing.original_key(other_apk, title=title)

                old, old_identity = build("legacy", legacy, 12)
                legacy_title = "01000000000CD000"
                self.assertEqual(signing.export_key(legacy_title, package=package), legacy)
                self.assertEqual(signing.export_key(legacy_title, package=package, previous_apk=old), legacy)
                fresh_title = "01000000000EF000"
                fresh_key = signing.export_key(fresh_title, package="org.nx2a.newgame")
                self.assertNotEqual(fresh_key, legacy)
                _, fresh_identity = build("new-title-after-legacy", fresh_key, 1)
                self.assertNotEqual(old_identity, fresh_identity)
                self.assertEqual(signing.original_key(old, title=other), legacy)
                recovered = signing.original_key(old, title=legacy_title, explicit=legacy)
                self.assertNotEqual(recovered, legacy)
                self.assertEqual(signing.read_signer(recovered), signing.read_signer(legacy))
                self.assertEqual(signing.original_key(old, title=legacy_title), recovered)
                with patch.dict(os.environ, {"NX2A_INSTALLED_GAME_APKS": json.dumps({package: str(old)})}):
                    self.assertEqual(signing.export_key(legacy_title, package=package, previous_apk=first), recovered)
                self.assertEqual(signing.export_key(title), key)
                # A compiler reset can archive a conflicting older signing family.
                archived = legacy.with_suffix(".recovery") / "older" / "signing-key.json"
                archived.parent.mkdir(parents=True)
                old_title_key = signing.title_key(archived, other)
                old_title_key.parent.mkdir()
                old_title_key.write_bytes(signing.title_key(legacy, other).read_bytes())
                signing.title_key(legacy, other).unlink()
                self.assertEqual(signing.original_key(other_apk, title=other), old_title_key)

                key.unlink()
                self.assertEqual(signing.original_key(first, title=title), key)
                key.unlink()
                seed_path.unlink()
                with self.assertRaisesRegex(BuildError, "signing seed is missing"):
                    signing.export_key(title)
                self.assertFalse(seed_path.exists())
                seed_path.write_text("ff" * 32)
                with self.assertRaisesRegex(BuildError, "seed does not match"):
                    signing.export_key(title)
                self.assertFalse(key.exists())

            restored = root / "restored" / legacy.name
            restored.parent.mkdir()
            restored.with_suffix(".seed-v1").write_text(seed)
            with patch.object(signing.paths, "signing_key", return_value=restored):
                restored_key = signing.original_key(first, title=title)
                _, restored_identity = build("restored", restored_key, 42)
                self.assertEqual(identity, restored_identity)

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
            gsr_license = ROOT / "nxnative/dm/android/shaders/licenses/sgsr1.txt"
            args = [sys.executable, str(apkdir / "apkdriver.py"), str(old), str(new), str(key),
                    "--host", str(host), "--dex", str(dex),
                    "--asset", f"licenses/sgsr1.txt={gsr_license}"]
            subprocess.run(args, check=True, capture_output=True)
            with zipfile.ZipFile(old) as before, zipfile.ZipFile(new) as after:
                self.assertEqual(set(after.namelist()) - set(before.namelist()), {"assets/licenses/sgsr1.txt"})
                self.assertEqual(after.read("assets/licenses/sgsr1.txt"), gsr_license.read_bytes())
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

    def test_runtime_upgrade_checks_elf_shader_and_interface_compatibility(self):
        from porter.revision import PLAYER_API_REVISION
        from porter.upgrade_apk import metadata
        from porter.upstream import BuildError

        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "game.apk"

            def build(machine=183, shader_version=2, revision=PLAYER_API_REVISION):
                payload = io.BytesIO()
                with zipfile.ZipFile(payload, "w") as pack:
                    manifest = {"title_id": "0100000000010000"}
                    if revision is not None:
                        manifest["player_api_revision"] = revision
                    pack.writestr("nx2a-data.json", json.dumps(manifest))
                    pack.writestr("mods/build_src.sha256", "test fixture")
                    pack.writestr("shaders.dmsv", struct.pack("<4sII", b"DMSV", shader_version, 0))
                with zipfile.ZipFile(path, "w") as apk:
                    ident = b"\x7fELF\x02\x01\x01".ljust(16, b"\0")
                    apk.writestr("lib/arm64-v8a/libdmhost.so", ident + struct.pack("<HH", 3, machine))
                    apk.writestr("assets/nx2a-data.zip", payload.getvalue())

            build()
            self.assertEqual(metadata(path, None)[0]["player_api_revision"], PLAYER_API_REVISION)
            build(shader_version=1, revision=None)
            self.assertNotIn("player_api_revision", metadata(path, None)[0])
            for args, error in [
                ({"machine": 62}, "arm64-v8a"),
                ({"shader_version": 3}, "shader bundle is incompatible"),
                ({"revision": PLAYER_API_REVISION + 1}, "runtime interface revision"),
            ]:
                with self.subTest(args=args):
                    build(**args)
                    with self.assertRaisesRegex(BuildError, error):
                        metadata(path, None)

    def test_runtime_upgrade_builds_at_the_fingerprinted_profile_location(self):
        from porter import host, upgrade_apk
        from porter.paths import DM

        title = "0100606026F12000"
        registered = DM / "games" / title
        fingerprint = host.profile_hash(title)
        manifest = {"title_id": title, "profile_h": fingerprint}

        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            key = root / "key.json"
            key.touch()
            run = subprocess.run

            def build_or_package(args, **kwargs):
                if "--out" not in args:
                    return
                env = kwargs["env"]
                output = Path(args[args.index("--out") + 1])
                header_dir = output / "gen" / title
                header_dir.mkdir(parents=True)
                run([sys.executable, str(DM / "games/game.py"),
                     "header", title, str(header_dir / "game_profile.h")], env=env, check=True)
                ident = b"\x7fELF\x02\x01\x01".ljust(16, b"\0")
                (output / "libdmhost.so").write_bytes(ident + struct.pack("<HH", 3, 183))

            with (patch.object(upgrade_apk, "metadata", return_value=(manifest, "record", [])),
                  patch.object(upgrade_apk.signing, "original_key", return_value=key) as original_key,
                  patch.object(upgrade_apk, "activity_dex", return_value=root / "classes.dex"),
                  patch.object(upgrade_apk.runtime_update, "find_profile", return_value=(registered, fingerprint, True)),
                  patch.object(upgrade_apk.host, "stale_sources", return_value=[]),
                  patch.object(upgrade_apk.toolchain, "find_ndk", return_value=(root, "fixture")),
                  patch.object(upgrade_apk.subprocess, "run", side_effect=build_or_package)):
                result = upgrade_apk.upgrade(root / "old.apk", root / "updated.apk", key=key)
                self.assertEqual(result, root / "updated.apk")
                original_key.assert_called_once_with(root / "old.apk", title=title, explicit=key)

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
