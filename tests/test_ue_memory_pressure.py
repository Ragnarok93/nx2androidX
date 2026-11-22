"""Regression tests for bounded-memory Unreal IoStore shader decoding."""
import struct
import tempfile
import unittest
from pathlib import Path
from porter.upstream import uepak


def sample_toc():
    g0, g1 = b'ABCDE', b'123456'
    gid0, gid1 = b'group-zero!!', b'group-one!!!'
    indices = (2, 0, 3, 1)
    entries = (2 << 34 | 0 << 4 | 0, 4 << 34 | 1 << 4 | 3,
               0 << 34 | 0 << 4 | 5, 0 << 34 | 1 << 4 | 5)
    data = (struct.pack('<II', 1, 0) + struct.pack('<I', 4) + b'H' * 80 +
            struct.pack('<I', 2) + gid0 + gid1 +
            struct.pack('<I', 1) + struct.pack('<II', 0, 4) +
            struct.pack('<I', 4) + struct.pack('<4Q', *entries) +
            struct.pack('<I', 2) + struct.pack('<4I', 0, 2, 5, 6) +
            struct.pack('<I', 4) + struct.pack('<4I', *indices))
    class Toc:
        path = 'fixture.utoc'
        index = {gid0: 1, gid1: 2}
        def read(self, i, decode):
            if i == 0: return data
            if i == 1: return b'!'
            if i == 2: return g1
            raise AssertionError('unknown chunk')
    return Toc(), lambda compressed, raw: g0 if compressed == b'!' and raw == 5 else None


class IoStoreStreamingTests(unittest.TestCase):
    def test_streamed_archive_byte_identical_to_original(self):
        toc, decode = sample_toc()
        expected = uepak.io_archive(toc, 0, decode)
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp) / 'test.ushaderbytecode'
            count = uepak.io_archive_file(toc, 0, decode, out)
            self.assertEqual(count, 4)
            self.assertEqual(out.read_bytes(), expected)
            self.assertFalse(Path(str(out) + '.partial').exists())
            self.assertEqual(len(uepak.archive(out.read_bytes())['shaders']), 4)

    def test_failed_decode_preserves_prior_output(self):
        toc, _ = sample_toc()
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp) / 'test.ushaderbytecode'
            out.write_bytes(b'old valid output')
            with self.assertRaisesRegex(ValueError, 'decoded to'):
                uepak.io_archive_file(toc, 0, lambda c, n: b'bad', out)
            self.assertEqual(out.read_bytes(), b'old valid output')
            self.assertFalse(Path(str(out) + '.partial').exists())

    def test_android_round_spools_and_bounds_results(self):
        root = Path(__file__).resolve().parents[1]
        text = (root / 'porter/pipeline.py').read_text()
        section = text[text.index('    UE_ROUNDS = 4'):text.index('    def device_oodle_end(self):')]
        self.assertIn("pending_dir / name", section)
        self.assertIn("(64 << 20)", section)
        self.assertIn("with out.open('rb') as inp:", section)
        self.assertNotIn("b = out.read_bytes()", section)
        self.assertNotIn("want.setdefault(k, bytes(comp))", section)

    def test_unencrypted_iostore_defers_nso_decompression(self):
        root = Path(__file__).resolve().parents[1]
        text = (root / 'nxnative/dm/gfx/tools/uepak.py').read_text()
        section = text[text.index('def job(romfs, main_nso, work, decode=None):'):text.index('def finish(work, out):')]
        self.assertIn('if img is None: img, lo, hi = nso_image(main_nso)', section)
        self.assertIn('io_archive_file(t, i, decode', section)


if __name__ == '__main__':
    unittest.main()
