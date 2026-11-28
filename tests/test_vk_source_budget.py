"""Join GPU stages by source ticket; no cross-frame estimates or double charging."""
import subprocess,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class SourceBudget(unittest.TestCase):
    def test_ticket_join_and_unknown_cost(self):
        android=ROOT/'nxnative/dm/android'
        modern=(android/'gfx_vk_source_budget.h').exists()
        code=r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#ifdef SOURCE_BUDGET
#include "gfx_vk_source_budget.h"
#endif
int main(void){
#ifndef SOURCE_BUDGET
    fprintf(stderr,"no source-keyed residual budget\n");return 1;
#else
    dm_source_budget b={0};
    dm_source_budget_begin(&b,10,123,16.666,1,4,1920,1080);
    assert(isnan(dm_source_budget_residual(&b,10,123,10.)));
    assert(!dm_source_budget_recon(&b,9,4,1920,1080,2.));
    assert(!dm_source_budget_recon(&b,10,3,1920,1080,2.));
    assert(!dm_source_budget_recon(&b,10,4,1280,720,2.));
    assert(!dm_source_budget_recon(&b,10,4,1920,1080,NAN));
    assert(dm_source_budget_recon(&b,10,4,1920,1080,2.));
    assert(fabs(dm_source_budget_residual(&b,10,123,10.)-3.666)<1e-6);
    assert(isnan(dm_source_budget_residual(&b,10,123,-1.)));
    assert(isnan(dm_source_budget_residual(&b,10,321,10.)));
    assert(!dm_source_budget_recon(&b,10,4,1920,1080,10.));
    // Replay includes any replay-integrated GSR2. No separate charge if
    // no presentation reconstruction was submitted.
    dm_source_budget_begin(&b,11,456,16.666,0,0,1920,1080);
    assert(fabs(dm_source_budget_residual(&b,11,456,12.)-3.666)<1e-6);
    assert(dm_source_budget_residual(&b,11,456,18.)<0.);
    dm_source_budget_clear(&b);
    assert(isnan(dm_source_budget_residual(&b,10,123,10.)));
    assert(!dm_source_budget_recon(&b,10,4,1920,1080,2.));
    dm_source_budget_begin(&b,12,123,16.666,1,4,1920,1080);
    dm_source_budget_begin(&b,12+DM_SOURCE_BUDGET_SLOTS,123,16.666,1,4,1920,1080);
    assert(!dm_source_budget_recon(&b,12,4,1920,1080,2.));
    assert(isnan(dm_source_budget_residual(&b,12,123,10.)));
#endif
}
'''
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'budget.c';p.write_text(code);exe=p.with_suffix('')
            cmd=['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(android),str(p),'-lm','-o',str(exe)]
            if modern:cmd.append('-DSOURCE_BUDGET')
            subprocess.run(cmd,check=True)
            r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=5)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
