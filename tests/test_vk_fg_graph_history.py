"""Validate chronological warmup and the production seed-pass boundary."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class FGGraphHistory(unittest.TestCase):
    def test_three_consecutive_submitted_sources(self):
        code=r'''
#include <assert.h>
#include "lsfg/lsfg_graph_history.h"
int main(void){
    lsfg_graph_history h={0};
    lsfg_graph_history_reset(&h,7);
    assert(!h.initialized&&!lsfg_graph_history_ready(&h,7,10));
    assert(lsfg_graph_history_index(&h,10)==0);
    assert(!lsfg_graph_history_submit(&h,7,10,0)&&!h.initialized);
    assert(lsfg_graph_history_submit(&h,7,10,1)&&h.initialized&&h.filled==1);
    assert(!lsfg_graph_history_submit(&h,7,10,1));
    assert(!lsfg_graph_history_submit(&h,8,11,1));
    assert(lsfg_graph_history_index(&h,11)==1);
    assert(lsfg_graph_history_submit(&h,7,11,1)&&h.filled==2);
    assert(!lsfg_graph_history_ready(&h,7,12));
    assert(!lsfg_graph_history_submit(&h,7,12,0));
    assert(lsfg_graph_history_index(&h,13)==0);
    assert(lsfg_graph_history_submit(&h,7,13,1)&&h.filled==1);
    assert(lsfg_graph_history_submit(&h,7,14,1)&&h.filled==2);
    assert(lsfg_graph_history_index(&h,15)==2);
    assert(lsfg_graph_history_submit(&h,7,15,1)&&h.filled==3);
    assert(lsfg_graph_history_ready(&h,7,16));
    assert(!lsfg_graph_history_ready(&h,7,17));
    assert(!lsfg_graph_history_ready(&h,8,16));
    // If activation is deferred, continue rotating both input and alpha rings.
    assert(lsfg_graph_history_index(&h,16)==3);
    assert(lsfg_graph_history_submit(&h,7,16,1));
    assert(lsfg_graph_history_index(&h,17)==4);
    assert(lsfg_graph_history_submit(&h,7,17,1));
    assert(h.filled==3&&lsfg_graph_history_ready(&h,7,18)&&h.next_index==5);
    lsfg_graph_history_reset(&h,8);
    assert(h.initialized&&!h.filled&&!lsfg_graph_history_ready(&h,8,16));
    assert(lsfg_graph_history_index(&h,16)==0);
    assert(lsfg_graph_history_submit(&h,8,16,1));
    assert(!lsfg_graph_history_submit(&h,8,15,1));
    return 0;
}'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'case.c';p.write_text(code);exe=p.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',
                            '-I',str(ROOT/'nxnative/dm/android'),str(p),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
    def test_seed_pass_has_no_output_or_beta_access(self):
        source=(ROOT/'nxnative/dm/android/lsfg/lsfg_chain.c').read_text()
        seed=source[source.index('static void lsfg_chain_seed_timed('):source.index('void lsfg_chain_seed(')]
        self.assertIn('c->mip_pass',seed)
        self.assertIn('c->alpha_pass',seed)
        for forbidden in ('c->beta','c->gamma','c->delta','c->gen_pass','c->gen_sets'):
            self.assertNotIn(forbidden,seed)
        shared=source[source.index('void lsfg_chain_shared_timed('):source.index('void lsfg_chain_shared(')]
        self.assertLess(shared.index('lsfg_chain_seed_timed('),shared.index('c->beta_pass'))

    def test_active_generation_requires_three_fresh_alpha_slots(self):
        source=(ROOT/'nxnative/dm/android/dm_framegen.c').read_text()
        start=source.index('    uint64_t count = F.count++')
        end=source.index('    if (ng > LSFG_MAX_GENERATIONS)',start)
        end=source.index('\n',end)
        code=r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include "lsfg/lsfg_graph_history.h"
enum { LSFG_MAX_GENERATIONS=3 };
static struct { uint64_t count; unsigned warm_streak; } F;
static unsigned alpha_mask;
static size_t frame(int ready, size_t generations){
    struct {int warm;size_t generations;} plan={ready,generations};
'''+source[start:end]+r'''
    if(warm)alpha_mask|=1u<<(count%3);else alpha_mask=0;
    if(ng)assert(alpha_mask==7 && "generation may only read three fresh alpha slots");
    return ng;
}
int main(void){
    assert(!frame(0,0));assert(!frame(1,1));assert(!frame(1,1));assert(frame(1,1)==1);
    assert(!frame(0,0));assert(!frame(1,1));assert(!frame(1,1));assert(frame(1,1)==1);
    assert(!frame(1,0));assert(frame(1,2)==2);
    F.count=F.warm_streak=alpha_mask=0;
    assert(!frame(1,1));assert(!frame(1,1));assert(!frame(1,1));assert(frame(1,1)==1);
    return 0;
}'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'active.c';p.write_text(code);exe=p.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',
                            '-I',str(ROOT/'nxnative/dm/android'),str(p),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
        dispatch=source[source.index('    fg_timing_stamp(cb,qb+1);'):source.index('    if (seeded) {')]
        self.assertIn('if (ng) lsfg_chain_shared_timed',dispatch)
        self.assertIn('if(warm)lsfg_chain_seed',dispatch)
