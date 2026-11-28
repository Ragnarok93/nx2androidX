"""Distinguish unknown timing from real exhaustion of the LSFG GPU budget."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

class ExhaustedBudget(unittest.TestCase):
    def test_known_deficit_can_reduce_flow_but_unknown_cannot(self):
        source = r'''
#include <assert.h>
#include "gfx_vk_fg_governor.h"
int main(void){
    dm_fg_governor g;
    dm_fg_governor_reset(&g,50,1);
    dm_fg_timing s={.epoch=1,.scale=50,.generated=1,.valid=1,.total_ms=4,
        .copy_ms=.25,.mip_ms=.25,.shared_ms=1,.refine_ms=2,.output_ms=.5};
    for(unsigned i=0;i<20;i++){
        ++s.serial;assert(dm_fg_governor_observe(&g,&s,NAN,.1,1,1).reason==FG_FLOW_HOLD);
    }
    for(unsigned i=0;i<3;i++){
        ++s.serial;assert(dm_fg_governor_observe(&g,&s,-1,.1,1,1).reason==FG_FLOW_HOLD);
    }
    ++s.serial;dm_fg_flow_request r=dm_fg_governor_observe(&g,&s,-1,.1,1,1);
    assert(r.reason==FG_FLOW_DOWN&&r.pct==40);
    dm_fg_governor_ack(&g,1,2);
    s.epoch=2;s.scale=40;
    for(unsigned i=0;i<20;i++){
        ++s.serial;assert(dm_fg_governor_observe(&g,&s,0,.1,1,1).reason==FG_FLOW_HOLD);
    }
    // A failed/comparable-cost probe must not roll back into a known deficit.
    assert(!g.probe);
    dm_fg_governor_reset(&g,25,3);s.epoch=3;s.scale=25;
    for(unsigned i=0;i<60;i++){
        ++s.serial;assert(dm_fg_governor_observe(&g,&s,0,.1,1,1).reason==FG_FLOW_HOLD);
    }
    return 0;
}'''
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / 'case.c'
            p.write_text(source)
            exe = p.with_suffix('')
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-I', str(ROOT / 'nxnative/dm/android'), str(p), '-lm', '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
