"""Verify replay sharpening metadata survives delayed presentation."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class ReplaySharpness(unittest.TestCase):
    def test_submitted_value_and_legacy_flag_bits(self):
        source = r'''
#include <assert.h>
#include "spatial_upscaler.h"
int main(void) {
    for (unsigned i=0; i<=100; ++i) {
        for (int temporal=0; temporal<2; ++temporal) {
            unsigned queued=spatial_replay_pack(i,temporal);
            assert(spatial_replay_sharpness(queued)==i);
            assert(spatial_replay_bits(queued)==((i?1u:0u)|(temporal?2u:0u)));
        }
    }
    unsigned old_frame=spatial_replay_pack(37,0);
    unsigned next_frame=spatial_replay_pack(100,1);
    assert(spatial_replay_sharpness(old_frame)==37);
    assert(spatial_replay_sharpness(next_frame)==100);
    assert(spatial_replay_sharpness(spatial_replay_pack(101,0))==100);
    return 0;
}'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'case.c'
            path.write_text(source)
            exe = path.with_suffix('')
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-I', str(ROOT / 'nxnative/dm/android'), str(path), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
