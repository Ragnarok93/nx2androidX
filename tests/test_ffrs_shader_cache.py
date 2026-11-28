"""Regression for the FFRS 1.0.1 lazy shader factory's failed-create exit.

Model only the four instructions before the shared ABI epilogue, using the
actual registered CUT bytes. No game image, emulator or GPU is required.
"""
from pathlib import Path
import runpy
import struct
import unittest

PROFILE = Path(__file__).resolve().parents[1] / 'nxnative/dm/games/0100606026F12000/patches.py'
BUILD = '84e182f72ee1da4f477804bd05b54c15'
START = 0x15686fc
EPILOGUE = 0x15686e8
ORIGINAL = bytes.fromhex('f5031faa e00315aa f44f43a9 f65742a9')


def failed_create(cache, index):
    code = bytearray(ORIGINAL)
    for offset, old, new, _ in runpy.run_path(str(PROFILE))['CUTS'][BUILD]:
        if START <= offset < START + len(code):
            pos = offset - START
            old, new = bytes.fromhex(old), bytes.fromhex(new)
            assert len(old) == len(new) and code[pos:pos + len(old)] == old
            code[pos:pos + len(new)] = new
    regs = {0: 0, 8: 0, 21: 0xdead, 22: index, 23: 0x1000}
    memory = {0x1038: 0x2000, **{0x2000 + 8*i: p for i, p in enumerate(cache)}}
    acquired = False
    for i, (w,) in enumerate(struct.iter_unpack('<I', code)):
        if w == 0xaa1f03f5:  # mov x21, xzr
            regs[21] = 0
        elif w == 0xaa1503e0:  # mov x0, x21
            regs[0] = regs[21]
        elif w == 0xa9434ff4:  # original duplicated epilogue begins
            return regs[0], acquired
        elif w == 0xf9401ee8:  # ldr x8, [x23, #0x38]
            regs[8] = memory[regs[23] + 0x38]
        elif w == 0x8b36cd08:  # add x8, x8, w22, sxtw #3
            regs[8] += 8 * regs[22]
        elif w == 0xc8dffd00:  # ldar x0, [x8]
            regs[0] = memory[regs[8]]
            acquired = True
        elif w >> 26 == 0b000101:  # b to the common register-restoring epilogue
            imm = w & 0x3ffffff
            if imm & 0x2000000:
                imm -= 0x4000000
            assert START + 4*i + 4*imm == EPILOGUE
            return regs[0], acquired
        else:
            raise AssertionError(f'unexpected instruction {w:#x}')
    raise AssertionError('missing epilogue')


class FailedShaderCreate(unittest.TestCase):
    def test_competing_success_is_not_lost(self):
        self.assertEqual(failed_create([0xabc0, 0xdef0], 1), (0xdef0, True))

    def test_genuine_miss_is_not_fabricated(self):
        self.assertEqual(failed_create([0xabc0, 0], 1)[0], 0)

    def test_no_neighboring_shader_substitution(self):
        self.assertEqual(failed_create([0, 0xdef0], 0)[0], 0)


if __name__ == '__main__':
    unittest.main()
