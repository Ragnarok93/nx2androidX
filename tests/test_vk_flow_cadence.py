"""Host checks for the production reconstruction cadence estimator."""
import subprocess, tempfile, unittest
from pathlib import Path
class Cadence(unittest.TestCase):
    def test_effective_period(self):
        source=r'''
#include <assert.h>
#include "gfx_vk_flow_budget.h"
int main(void) {
    // Guest asks for 60, host caps to 30 or 40: the temporal budget uses
    // the effective source period, not the nominal guest present interval.
    dm_flow_cadence c=dm_flow_cadence_estimate(1.0/60,30);
    assert(c.period_us==33333 && c.budget_ms==2.5f);
    c=dm_flow_cadence_estimate(1.0/60,40);
    assert(c.period_us==25000 && c.budget_ms==2.5f);
    c=dm_flow_cadence_estimate(1.0/60,60);
    assert(c.period_us==16667 && c.budget_ms>1.66f && c.budget_ms<1.67f);
    c=dm_flow_cadence_estimate(1.0/30,60);
    assert(c.period_us==33333 && c.budget_ms==2.5f);
    c=dm_flow_cadence_estimate(0,30);
    assert(c.period_us==33333 && c.budget_ms==2.5f);
    c=dm_flow_cadence_estimate(0,0);
    assert(c.period_us==0 && c.budget_ms==2.0f);
    c=dm_flow_cadence_estimate(1.0/60,0);
    assert(c.period_us==16667);
    c=dm_flow_cadence_estimate(-1,30);
    assert(c.period_us==0 && c.budget_ms==0);
    return 0;
}'''
        root=Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);(p/'case.c').write_text(source)
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(root/'nxnative/dm/android'),str(p/'case.c'),'-o',str(p/'case')],check=True)
            subprocess.run([str(p/'case')],check=True)
if __name__=='__main__':unittest.main()
