"""A same-workload GSR2 fallback must retain a finite recovery opportunity."""
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class RecurringRecovery(unittest.TestCase):
    def test_many_failed_probes_then_sustained_recovery(self):
        code=r'''
#include <assert.h>
#include <stdio.h>
#include "gfx_vk_flow_budget.h"
int main(void){
    dm_flow_budget p={.valid=1,.backoff=120,.generation=1};
    uint64_t t=1;
    for(unsigned probe=0;probe<12;probe++){
        for(unsigned i=0;i<4;i++)dm_flow_budget_pressure(&p,t++,1);
        if(p.retry_after==UINT64_MAX){fprintf(stderr,"permanent cooldown after overload %u\n",probe+1);return 1;}
        assert(p.retry_after>=t&&p.retry_after-t<1920);
        while(t<=p.retry_after){dm_flow_budget_pressure(&p,t,1);assert(!dm_flow_budget_allow(&p,t));t++;}
        for(unsigned i=0;i<29;i++){dm_flow_budget_pressure(&p,t,0);assert(!dm_flow_budget_allow(&p,t));t++;}
        dm_flow_budget_pressure(&p,t,0);assert(dm_flow_budget_allow(&p,t));t++;
    }
    assert(p.backoff==1920);
    for(unsigned i=0;i<60;i++,t++)dm_flow_budget_observe(&p,p.generation,t,t+1,3.,2.);
    assert(p.backoff==120&&!p.overload_events);
    p.overload_events=~0u;dm_flow_budget_overload(&p,UINT64_MAX-1);
    assert(p.retry_after==UINT64_MAX&&p.overload_events==~0u);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'case.c';p.write_text(code);exe=p.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(ROOT/'nxnative/dm/android'),str(p),'-o',str(exe)],check=True)
            r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=5)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
