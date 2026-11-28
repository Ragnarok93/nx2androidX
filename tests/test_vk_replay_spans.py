"""Separate replay submission groups exclude inter-group gaps and overlapping spans."""
import subprocess,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class ReplaySpans(unittest.TestCase):
    def test_disjoint_overlap_and_wrap(self):
        android=ROOT/'nxnative/dm/android'
        modern=(android/'gfx_vk_replay_spans.h').exists()
        code=r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#ifdef REPLAY_SPANS
#include "gfx_vk_replay_spans.h"
#endif
int main(void){
#ifndef REPLAY_SPANS
    fprintf(stderr,"no independent replay submission spans\n");return 1;
#else
    uint64_t stamp[]={100,110,140,152,160,170};
    vk_replay_span spans[]={{0,1},{2,3},{4,5}};
    uint64_t ns=0;
    assert(vk_replay_spans_ns(stamp,6,spans,3,64,1000000.,&ns)&&ns==32000000);
    // GPU pipelining can overlap consecutive submit groups.
    stamp[2]=105;stamp[3]=120;
    assert(vk_replay_spans_ns(stamp,6,spans,3,64,1000000.,&ns)&&ns==30000000);
    uint64_t wrap[]={250,4,2,8};
    assert(vk_replay_spans_ns(wrap,4,spans,2,8,1.,&ns)&&ns==14);
    // Reject impossible backwards starts, missing endpoints and invalid tick periods.
    wrap[2]=249;
    assert(!vk_replay_spans_ns(wrap,4,spans,2,8,1.,&ns));
    assert(!vk_replay_spans_ns(stamp,3,spans,2,64,1.,&ns));
    assert(!vk_replay_spans_ns(stamp,6,spans,3,0,1.,&ns));
    assert(!vk_replay_spans_ns(stamp,6,spans,3,64,0.,&ns));
    stamp[1]=99;assert(!vk_replay_spans_ns(stamp,6,spans,3,64,1.,&ns));
    // A valid zero-duration group is distinct from an absent query.
    stamp[1]=100;assert(vk_replay_spans_ns(stamp,6,spans,1,64,1.,&ns)&&ns==0);
#endif
}
'''
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'case.c';p.write_text(code);exe=p.with_suffix('')
            cmd=['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(android),str(p),'-lm','-o',str(exe)]
            if modern:cmd.append('-DREPLAY_SPANS')
            subprocess.run(cmd,check=True)
            r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=5)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
