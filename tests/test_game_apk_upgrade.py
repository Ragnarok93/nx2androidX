import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from porter import pipeline
from porter.upstream import BuildError


class GameApkUpdateTests(unittest.TestCase):
    title = "0100000000010000"
    patch_title = "0100000000010800"

    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        root = Path(tmp.name)
        base = root / "base.nsp"
        self.patch = root / "externally-selected.nsz"
        self.sibling = root / "sibling-[v65536].nsp"
        for path in (base, self.patch, self.sibling):
            path.write_bytes(b"fixture")
        request = pipeline.GameUpdate(self.patch, self.title, "v0", 65540, root / "signer.json")
        options = pipeline.Options(folder=root, package="org.nx2a.fixture", game_update=request)
        options.game = base
        self.port = pipeline.Port(options)
        self.port.work = root / "build"
        self.port.app = int(self.title, 16)
        self.port.fam = {"base": base, "updates": [(65536, self.sibling)], "dlc": []}
        self.port.env = {}
        self.metadata = f"patch {self.patch_title} 65536 {self.title}\n"

        def extractor_run(cmd, log, **_):
            self.assertEqual(cmd[-2:], ["--patch-meta", self.patch])
            log.parent.mkdir(parents=True, exist_ok=True)
            log.write_text(self.metadata)

        mocks = (
            patch.object(pipeline.nsp, "header_key"),
            patch.object(pipeline.nsp, "meta_titles", return_value=[self.port.app]),
            patch.object(pipeline.nsp, "build_nx_extract", return_value=root / "nx_extract"),
            patch.object(pipeline.nsp, "run", side_effect=extractor_run),
            patch.object(pipeline.profiles, "by_title",
                         return_value=(SimpleNamespace(builds=lambda: {"v65536": {}}), object())),
        )
        for mocked in mocks:
            mocked.start()
            self.addCleanup(mocked.stop)

    def test_uses_the_explicit_update_and_its_metadata_version(self):
        self.port.select_game_update(self.title)
        self.assertEqual(self.port.o.version, "v65536")
        self.assertEqual(self.port.o.vercode_offset, 3)
        self.assertEqual(self.port.fam["updates"], [(65536, self.patch)])

    def test_title_and_version_errors_happen_before_recompile(self):
        cases = (
            ("patch 0100000000020800 65536 0100000000020000\n", "belongs to title"),
            (f"patch {self.patch_title} 0 {self.title}\n", "must be newer"),
            (f"patch {self.patch_title} 131072 {self.title}\n", "no supported Android build"),
        )
        for metadata, reason in cases:
            with self.subTest(reason=reason):
                self.metadata = metadata
                with self.assertRaisesRegex(BuildError, reason):
                    self.port.select_game_update(self.title)
                self.assertEqual(self.port.fam["updates"], [(65536, self.sibling)])
                self.assertEqual(self.port.o.version, "")

    def test_rejects_a_version_that_cannot_meet_the_android_floor(self):
        with patch.object(pipeline.signing, "saved_version_floor", return_value=131073):
            with self.assertRaisesRegex(BuildError, "cannot meet Android version code 131073"):
                self.port.select_game_update(self.title)
        self.assertEqual(self.port.o.version, "")


if __name__ == "__main__":
    unittest.main()
