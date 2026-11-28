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
