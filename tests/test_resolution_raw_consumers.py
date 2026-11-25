"""Run the actual draw range preflight with a mocked guest pool and active owner."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPLAY = ROOT / 'nxnative/dm/android/gfx_vk_exec.c'
PREFIX = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define NVN_MEMORY_POOL_FLAGS_VIRTUAL 0x200
static struct { struct { uint64_t address,size; } vbuf[16]; } S;
static int g_tf_on;
typedef struct { uint64_t lo,hi; int buf,flags; } vkpool_t;
static vkpool_t p={0x1000,0x2000,1,0};
static uint64_t owner=0x1800, owner_size=0x100;
static vkpool_t *vk_pool_of(uint64_t a,uint64_t n) { return a>=p.lo && a<p.hi && n<=p.hi-a?&p:0; }
static int vk_rs_preflight_buffer(uint64_t a,uint64_t n) { return a<=owner ? owner-a>=n : a-owner>=owner_size; }
"""
SUFFIX = r"""
int main(void) {
    assert(resolution_draw_inputs(0,0,3,0,0,0,0,0));
    S.vbuf[0].address=0x1000; S.vbuf[0].size=4;
    assert(!resolution_draw_inputs(0,0,3,0,0,0,0,0));
    S.vbuf[0].address=0x1900;
    assert(resolution_draw_inputs(0,0,3,0,0,0,0,0));
    S.vbuf[0].address=0;
    assert(resolution_draw_inputs(0x17f0,1,8,1,0,0,0,0));
    assert(!resolution_draw_inputs(0x17f0,1,9,1,0,0,0,0));
    assert(!resolution_draw_inputs(0x1700,1,0,1,0x1900,0,0,0));
    assert(resolution_draw_inputs(0x1900,2,0,1,0x1920,0,0,0));
    assert(!resolution_draw_inputs(0,0,0,0,0x17f8,0,0,0));
    assert(!resolution_draw_inputs(0,0,0,0,0x1900,0x1800,2,16));
    assert(!resolution_draw_inputs(0,0,0,0,0x17e0,0x1900,3,16));
    assert(resolution_draw_inputs(0,0,0,0,0x1900,0x1950,2,16));
    assert(!resolution_draw_inputs(0,0,0,0,0x1900,0x1950,2,15));
    assert(!resolution_draw_inputs(0,0,0,0,0x1ff8,0,0,0));
    assert(!resolution_draw_inputs(0x1900,3,3,1,0,0,0,0));
    g_tf_on=1; assert(!resolution_draw_inputs(0,0,0,0,0,0,0,0)); g_tf_on=0;
    p.flags=NVN_MEMORY_POOL_FLAGS_VIRTUAL; assert(!resolution_draw_inputs(0,0,0,0,0x1900,0,0,0));
    puts("raw consumer ranges PASS");
}
"""

class ResolutionRawConsumers(unittest.TestCase):
    def test_draw_ranges(self):
        cc = shutil.which('cc') or shutil.which('clang')
        if not cc:
            self.skipTest('C compiler required')
        source = REPLAY.read_text()
        start = source.index('static int resolution_raw_span(')
        end = source.index('static void resolution_draw_preflight(', start)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            c = path / 'check.c'
            exe = path / 'check'
            c.write_text(PREFIX + source[start:end] + SUFFIX)
            subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror', str(c), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_preflight_precedes_cpu_and_snapshot_access(self):
        source = REPLAY.read_text()
        for signature, preflight, ready in (
            ('static void draw(const p_draw *d) {', 'resolution_draw_preflight(', 'graphics_cpu_ready();'),
            ('static void draw_idx(const p_draw_idx *d) {', 'resolution_draw_preflight(', 'graphics_cpu_ready();'),
            ('static void dispatch(const p_dispatch *d) {', 'resolution_preflight_compute(cs,d)', 'shader_cpu_ready(cs, 5);'),
        ):
            start = source.index(signature)
            end = source.index('\n}', start)
            body = source[start:end]
            self.assertLess(body.index(preflight), body.index(ready))
            self.assertLess(body.index(preflight), body.index('texel_snapshots('))
