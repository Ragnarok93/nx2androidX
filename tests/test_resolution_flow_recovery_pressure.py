"""Overload recovery must not restart GSR2 into a busy source stream."""
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class FlowRecoveryPressure(unittest.TestCase):
    def test_busy_cooldown_and_quiet_recovery(self):
        source=r'''
#include <assert.h>
#include "gfx_vk_flow_budget.h"
int main(void){
    dm_flow_budget p={0}; dm_flow_workload k={.iw=1280,.ih=720,.ow=2560,.oh=1440,.cadence_us=17000};
    dm_flow_budget_set_workload(&p,&k);
    for(uint64_t t=1;t<=4;t++)dm_flow_budget_pressure(&p,t,1);
    assert(p.retry_after==124);
    for(uint64_t t=5;t<=150;t++){
        dm_flow_budget_pressure(&p,t,1);
        assert(!dm_flow_budget_allow(&p,t));
    }
    for(uint64_t t=151;t<180;t++){
        dm_flow_budget_pressure(&p,t,0);
        assert(!dm_flow_budget_allow(&p,t));
    }
    dm_flow_budget_pressure(&p,180,0);
    assert(dm_flow_budget_allow(&p,180));
    for(uint64_t t=181;t<=184;t++)dm_flow_budget_pressure(&p,t,1);
    assert(p.retry_after==424);
    for(uint64_t t=400;t<429;t++)dm_flow_budget_pressure(&p,t,0);
    // Duplicates and a source gap cannot earn recovery credit.
    for(int i=0;i<50;i++)dm_flow_budget_pressure(&p,428,0);
    assert(!dm_flow_budget_allow(&p,428));
    dm_flow_budget_pressure(&p,430,0);
    assert(!dm_flow_budget_allow(&p,430));
    for(uint64_t t=431;t<459;t++)dm_flow_budget_pressure(&p,t,0);
    assert(!dm_flow_budget_allow(&p,458));
    dm_flow_budget_pressure(&p,459,0);
    assert(dm_flow_budget_allow(&p,459));
    return 0;
}'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'case.c';p.write_text(source);exe=p.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(ROOT/'nxnative/dm/android'),str(p),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
