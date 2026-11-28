"""Repeated refused LSFG graph transitions must not keep probing at 2 s."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class FGTransitionBackoff(unittest.TestCase):
    def test_failure_backoff_and_success_reset(self):
        source = r'''
#include <assert.h>
#include "gfx_vk_fg_governor.h"
int main(void) {
    dm_fg_governor g;
    dm_fg_governor_reset(&g,50,1);
    const double expected[] = {2,4,8,16,32,32};
    for (unsigned i=0;i<sizeof(expected)/sizeof(expected[0]);++i) {
        dm_fg_governor_request(&g,45,FG_FLOW_DOWN);
        dm_fg_governor_ack(&g,0,1);
        assert(!g.pending && g.applied==50 && g.backoff==expected[i]);
    }
    dm_fg_timing s={.epoch=1,.scale=50,.generated=1,.valid=1,.total_ms=10,
        .copy_ms=1,.mip_ms=1,.shared_ms=2,.refine_ms=5,.output_ms=1};
    for (unsigned i=0;i<100;++i) {
        ++s.serial;
        assert(dm_fg_governor_observe(&g,&s,8,.1,1,0).reason==FG_FLOW_HOLD);
    }
    dm_fg_governor_request(&g,45,FG_FLOW_DOWN);
    dm_fg_governor_ack(&g,1,2);
    assert(g.applied==45 && g.backoff==2);
    dm_fg_governor_request(&g,40,FG_FLOW_DOWN);
    dm_fg_governor_ack(&g,0,2);
    assert(g.applied==45 && g.backoff==2);
    dm_fg_governor_reset(&g,50,3);
    assert(g.backoff==0);
    return 0;
}'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'case.c'
            path.write_text(source)
            exe = path.with_suffix('')
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-I', str(ROOT / 'nxnative/dm/android'), str(path), '-lm', '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
