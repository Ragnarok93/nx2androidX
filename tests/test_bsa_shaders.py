"""Bethesda BSA reader: embedded names, compression, selection and bounds."""
import os
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'nxnative/dm/gfx/tools'))
import bsa

BODY = b'BNSH' + b'X' * 150


def archive(ver=105, compressed=False, default_compression=False, embedded=False):
    folder = b'shaders\0'
    name = b'example.bin\0'
    flags = 3 | (4 if default_compression else 0) | (0x100 if embedded else 0)
    record_size = 24 if ver == 105 else 16
    payload = BODY
    if compressed:
        if ver == 105:
            import lz4.frame
            payload = struct.pack('<I', len(BODY)) + lz4.frame.compress(BODY)
        else:
            payload = struct.pack('<I', len(BODY)) + zlib.compress(BODY)
    if embedded:
        payload = bytes([len(name)]) + name + payload
    toggle = 0x40000000 if compressed != default_compression else 0
    data_off = 36 + record_size + 1 + len(folder) + 16 + len(name)
    header = struct.pack('<4s8I', b'BSA\0', ver, 36, flags, 1, 1, len(folder), len(name), 0)
    if ver == 105:
        directory = struct.pack('<QIIQ', 0, 1, 0, 36 + record_size)
    else:
        directory = struct.pack('<QII', 0, 1, 36 + record_size)
    return header + directory + bytes([len(folder)]) + folder + struct.pack('<QII', 0, toggle | len(payload), data_off) + name + payload


class BsaTests(unittest.TestCase):
    def scan(self, data, select=lambda name: True):
        with tempfile.NamedTemporaryFile() as f:
            f.write(data)
            f.flush()
            return list(bsa.members(f.name, select))

    def test_v105_uncompressed(self):
        self.assertEqual(self.scan(archive()), [('shaders/example.bin', BODY)])

    def test_v105_compressed_with_embedded_filename(self):
        try:
            import lz4.frame
        except ImportError:
            self.skipTest('lz4.frame is not installed')
        self.assertEqual(self.scan(archive(compressed=True, embedded=True)), [('shaders/example.bin', BODY)])

    def test_v104_zlib(self):
        self.assertEqual(self.scan(archive(ver=104, compressed=True)), [('shaders/example.bin', BODY)])

    def test_compression_toggle(self):
        self.assertEqual(self.scan(archive(default_compression=True, compressed=False)), [('shaders/example.bin', BODY)])

    def test_member_filter(self):
        self.assertEqual(self.scan(archive(), lambda name: False), [])

    def test_truncated_member_fails(self):
        with self.assertRaises(ValueError):
            self.scan(archive()[:-3])


if __name__ == '__main__':
    unittest.main()
