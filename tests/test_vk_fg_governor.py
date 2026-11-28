"""Exercise the real pure LSFG governor with deterministic completed samples."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]

class FGGovernor(unittest.TestCase):
    def test_dwell_ack_and_rollback(self):
        code=r'''
#include <assert.h>
#include "gfx_vk_fg_governor.h"
static dm_fg_timing s;
static dm_fg_flow_request tick(dm_fg_governor *g,double budget,int accepted,int recover) {
    ++s.serial;
    return dm_fg_governor_observe(g,&s,budget,.1,accepted,recover);
}
int main(void) {
    dm_fg_governor g;
    dm_fg_governor_reset(&g,50,1);
    s=(dm_fg_timing){.epoch=1,.scale=50,.generated=1,.valid=1,
        .copy_ms=1,.mip_ms=1,.shared_ms=2,.refine_ms=4,.output_ms=1,.total_ms=9};
    for(int i=0;i<3;i++)assert(tick(&g,9,1,1).reason==FG_FLOW_HOLD);
    assert(tick(&g,9,0,1).reason==FG_FLOW_HOLD); // rejected present breaks evidence
    for(int i=0;i<3;i++)assert(tick(&g,9,1,1).reason==FG_FLOW_HOLD);
    dm_fg_flow_request r=tick(&g,9,1,1);
    assert(r.pct==40&&r.reason==FG_FLOW_DOWN&&g.applied==50);
    assert(tick(&g,9,1,1).reason==FG_FLOW_HOLD); // pending, no second request
    dm_fg_governor_ack(&g,1,2);
    assert(g.applied==40&&g.epoch==2&&g.probe);
    s.epoch=2;s.scale=40; // no savings in comparable workload
    for(int i=0;i<23;i++)assert(tick(&g,9,1,1).reason==FG_FLOW_HOLD);
    r=tick(&g,9,1,1);
    assert(r.reason==FG_FLOW_ROLLBACK&&r.pct==50);
    dm_fg_governor_ack(&g,0,2);assert(g.applied==40&&!g.pending);
    dm_fg_governor_reset(&g,50,3);s.epoch=3;s.scale=50;s.total_ms=4;
    s.mip_ms=.5;s.shared_ms=.5;s.refine_ms=1;s.copy_ms=1;s.output_ms=1;
    for(int i=0;i<50;i++)assert(tick(&g,12,1,0).reason==FG_FLOW_HOLD);
    for(int i=0;i<39;i++)assert(tick(&g,12,1,1).reason==FG_FLOW_HOLD);
    r=tick(&g,12,1,1);assert(r.pct==60&&r.reason==FG_FLOW_UP&&g.applied==50);
    dm_fg_governor_ack(&g,0,3);
    dm_fg_governor_reset(&g,25,4);s.epoch=4;s.scale=25;s.total_ms=9;
    for(int i=0;i<60;i++)assert(tick(&g,2,1,0).reason==FG_FLOW_HOLD);
    dm_fg_governor_reset(&g,50,5);s.epoch=5;s.scale=50;
    s.total_ms=9;s.generated=0;
    for(int i=0;i<20;i++)assert(tick(&g,2,1,1).reason==FG_FLOW_HOLD);
    s.generated=1;s.epoch=4;
    for(int i=0;i<20;i++)assert(tick(&g,2,1,1).reason==FG_FLOW_HOLD);
    s.epoch=5;s.scale=60;
    for(int i=0;i<20;i++)assert(tick(&g,2,1,1).reason==FG_FLOW_HOLD);
    s.scale=50;
    assert(tick(&g,0,1,1).reason==FG_FLOW_HOLD);
    assert(tick(&g,2,1,1).reason==FG_FLOW_HOLD);
    for(int i=0;i<20;i++)assert(dm_fg_governor_observe(&g,&s,2,.1,1,1).reason==FG_FLOW_HOLD);
    ++s.serial;assert(dm_fg_governor_observe(&g,&s,2,20,1,1).reason==FG_FLOW_HOLD);
    dm_fg_governor_reset(&g,75,6);dm_fg_governor_preset(&g,DM_FG_FLOW_QUALITY);
    s.epoch=6;s.scale=75;s.total_ms=9;s.generated=1;s.valid=1;
    for(int i=0;i<3;i++)assert(tick(&g,5,1,0).reason==FG_FLOW_HOLD);
    r=tick(&g,5,1,0);assert(r.reason==FG_FLOW_DOWN&&r.pct==70);
    dm_fg_governor_reset(&g,70,7);dm_fg_governor_preset(&g,DM_FG_FLOW_QUALITY);
    s.epoch=7;s.scale=70;
    for(int i=0;i<20;i++)assert(tick(&g,5,1,0).reason==FG_FLOW_HOLD);
    dm_fg_governor_reset(&g,75,8);dm_fg_governor_preset(&g,DM_FG_FLOW_BALANCED);
    s.epoch=8;s.scale=75;
    for(int i=0;i<39;i++)assert(tick(&g,20,1,1).reason==FG_FLOW_HOLD);
    r=tick(&g,20,1,1);assert(r.reason==FG_FLOW_UP&&r.pct==80);
    dm_fg_governor_reset(&g,80,9);dm_fg_governor_preset(&g,DM_FG_FLOW_BALANCED);
    s.epoch=9;s.scale=80;
    for(int i=0;i<60;i++)assert(tick(&g,20,1,1).reason==FG_FLOW_HOLD);
    return 0;
}'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'check.c';p.write_text(code);exe=p.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(ROOT/'nxnative/dm/android'),str(p),'-lm','-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
