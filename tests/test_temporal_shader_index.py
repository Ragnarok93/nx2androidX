"""Check that recorded shader keys resolve to actual SPIR-V accesses."""
import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'nxnative/dm/android/tools'))
import dmsv
from resolution_meta import encode_resolution_meta
import temporal_shader_index as temporal


def module():
    words = [0x07230203, 0x10300, 0, 50, 0]

    def instruction(op, *args):
        words.extend(((1 + len(args)) << 16 | op, *args))

    # Static uniform load through an array of vectors:
    # struct member 0 at 32, array index 2 at stride 16, component 1 at 4.
    for var, binding in ((20, 0), (21, 2), (22, 4), (23, 7)):
        instruction(71, var, 33, binding)
        instruction(71, var, 34, 3)
    instruction(72, 5, 0, 35, 32)
    instruction(71, 4, 6, 16)
    instruction(21, 1, 32, 0)
    instruction(22, 2, 32)
    instruction(23, 3, 2, 4)
    instruction(28, 4, 3, 13)
    instruction(30, 5, 4)
    instruction(32, 6, 2, 5)
    instruction(32, 7, 2, 2)
    for constant, value in ((10, 0), (11, 2), (12, 1), (13, 16)):
        instruction(43, 1, constant, value)
    instruction(59, 6, 20, 2)
    for var in (21, 22, 23):
        instruction(59, 8, var, 0)
    instruction(65, 7, 24, 20, 10, 11, 12)
    instruction(61, 2, 25, 24)
    instruction(65, 7, 26, 20, 10, 45, 12)  # dynamic index
    instruction(61, 2, 27, 26)
    instruction(61, 9, 31, 21)
    instruction(61, 9, 32, 22)
    instruction(61, 9, 33, 23)
    instruction(95, 3, 34, 31, 46)
    instruction(80, 3, 35, 25, 25, 25, 25)
    instruction(129, 3, 36, 34, 35)  # reprojected sample coordinate comes from slot 2 and the UBO
    instruction(88, 3, 37, 32, 36)
    instruction(99, 33, 46, 37)
    return struct.pack(f'<{len(words)}I', *words)


def reflection(c1=b""):
    rows = [
        (1, 0, 255, 0, 0, 1, 3, 0, 0, -1, 0),
        (7, 2, 1, 0, 2, 1, 5, 0, 0, -1, 0),
        (7, 2, 1, 0, 4, 1, 9, 0, 0, -1, 0),
        (9, 3, 1, 0, 7, 1, 0, 0, 4, -1, 0),
    ]
    return struct.pack('<5I', len(rows), len(c1), 8, 8, 1) + b''.join(
        temporal.REFLECTION.pack(*row) for row in rows) + c1


class TemporalShaderIndex(unittest.TestCase):
    def test_bundle_evidence_preserves_keys_bindings_and_fixed_offsets(self):
        with tempfile.TemporaryDirectory() as tmp:
            bundle = Path(tmp) / 'shaders.dmsv'
            code = module()
            dmsv.write(bundle, [(0x17ab, 5, code, reflection()), (2, 0, code, reflection())], level=0)
            digest = hashlib.sha256(bundle.read_bytes()).hexdigest()
            index = temporal.build_index(bundle, digest)
        self.assertEqual(index['bundle_sha256'], digest)
        shader, = index['candidates']
        self.assertEqual((shader['key'], shader['stage']), ('00000000000017ab', 5))
        self.assertEqual(shader['spv_sha256'], hashlib.sha256(code).hexdigest())
        bindings = {x['binding']: x for x in shader['bindings']}
        self.assertEqual(bindings[0]['byte_offsets'], [68])
        self.assertTrue(bindings[0]['unresolved_access'])
        self.assertEqual(bindings[2]['read_ops'], ['fetch'])
        self.assertEqual(bindings[4]['read_ops'], ['sample-lod'])
        self.assertEqual(bindings[4]['coordinate_image_sources'], [2])
        self.assertEqual(bindings[4]['coordinate_uniform_sources'], [(0, 68)])
        self.assertTrue(bindings[7]['writes'])
        self.assertNotIn('role', bindings[4])

    def test_production_scaling_trailer_preserves_temporal_candidates(self):
        bindings = [{'binding': binding, 'type': kind, 'count': 1, 'dim': 1}
                    for binding, kind in ((2, 'sampled'), (4, 'sampled'), (7, 'storage'))]
        resources = {
            'textures': [{'binding': binding, 'flags': 2, 'array': 1} for binding in (2, 4)],
            'images': [{'binding': 7, 'flags': 4, 'array': 1}],
        }
        base = reflection(b'RSCL in C1 must not be parsed')
        trailer = encode_resolution_meta('cs', bindings, resources)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'shaders.dmsv'
            code = module()
            dmsv.write(path, [(1, 5, code, base), (2, 5, code, base + trailer),
                              (3, 5, code, base + trailer[:-1])], level=0)
            report = temporal.build_index(path, '')
        legacy, scaled = report['candidates']
        self.assertEqual((legacy['key'], scaled['key']), ('0000000000000001', '0000000000000002'))
        self.assertEqual(legacy['bindings'], scaled['bindings'])
        self.assertEqual(temporal.reflected_bindings(base + b'UNKNOWN!'), {})

    def test_invalid_module_is_reported_and_ignored(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'shaders.dmsv'
            dmsv.write(path, [(5, 5, b'bad module', reflection())], level=0)
            report = temporal.build_index(path, '')
        self.assertEqual(report['candidates'], [])
        self.assertEqual(report['skipped'], {'invalid-module': 1})
