import unittest
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'nxnative/dm/android/tools'))
from resolution_meta import encode_resolution_meta, HEADER, RECORD, UNKNOWN, UNSUPPORTED

class ResolutionMetaTests(unittest.TestCase):
    def test_descriptor_order_and_unsupported(self):
        res = {'textures': [{'binding': 19, 'array': 1, 'flags': 2}, {'binding': 3, 'array': 1, 'flags': 0}, {'binding': 7, 'array': 4, 'flags': 3}], 'images': [{'binding': 2, 'array': 1, 'flags': 4}]}
        binds = [{'binding': n, 'type': t, 'count': c, 'dim': 1} for n,t,c in [(2,'storage',1),(3,'sampled',1),(7,'sampled',4),(19,'sampled',1),(99,'image',1)]]
        b = encode_resolution_meta('ps', binds, res)
        self.assertEqual(HEADER.unpack_from(b), (b'RSCL', 1, 5))
        self.assertEqual(list(RECORD.iter_unpack(b[HEADER.size:])), [(2, UNKNOWN, 5), (3, 2, 0), (7, UNKNOWN, 3), (19, 1, 2), (99, UNKNOWN, UNSUPPORTED)])
        self.assertEqual(encode_resolution_meta('gs', binds, res), b'')
        self.assertEqual(encode_resolution_meta('ps', binds, {}), b'')
    def test_duplicate_fails_closed(self):
        r = {'textures': [{'binding': 5, 'array': 1, 'flags': 0}]*2, 'images': []}
        self.assertEqual(encode_resolution_meta('vs', [], r), b'')

if __name__ == '__main__': unittest.main()
