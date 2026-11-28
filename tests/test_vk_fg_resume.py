"""Check actual LSFG pacer resume without treating paused time as work."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class FGPacerResume(unittest.TestCase):
    def test_resume_preserves_overload_evidence_and_excludes_pause(self):
        android = ROOT / 'nxnative/dm/android'
        modern = 'void lsfg_pacer_resume(' in (android / 'lsfg/lsfg.h').read_text()
        code = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "lsfg/lsfg.h"
static void resume(lsfg_pacer *p,double now){
#ifdef EXPLICIT_RESUME
    lsfg_pacer_resume(p,now);
#else
    (void)p;(void)now;
#endif
}
int main(void){
    lsfg_pacer p={.last_frame=100.,.smoothed_interval=1.f/60,.issued=1,
        .output_credit=.5,.unloaded_base_rate=60.,.next_probe=105.,
        .stable_until=101.,.fixed_base=60,.min_base=54.,.base_blocked=1,
        .base_healthy_since=99.8,.probe_failures=2};
    resume(&p,110.);
    if(p.last_frame || p.smoothed_interval || p.issued || p.output_credit ||
        p.next_probe!=115. || p.stable_until!=111.){
        fprintf(stderr,"resume lost cadence/cooldown separation\n");return 1;
    }
    assert(p.unloaded_base_rate==60.&&p.probe_failures==2&&p.base_blocked);
    assert(!p.base_healthy_since&&!p.base_deficit_since);
    assert(!lsfg_pacer_plan(&p,3,2,0,110.).generations);
    for(unsigned i=1;i<300;i++)assert(!lsfg_pacer_plan(&p,3,2,0,110.+i/60.).generations);
    assert(lsfg_pacer_plan(&p,3,2,0,115.).generations==1);
    // An interrupted legacy adaptive-output probe cannot be judged by a pause.
    p=(lsfg_pacer){.last_frame=200.,.probe_until=200.5,.limit=2,
        .probe_previous_limit=1,.probe_failures=1,.mode_target=120};
    resume(&p,220.);
    assert(p.limit==1&&!p.probe_until&&p.probe_failures==1&&p.next_probe>=220.);
    // Short hold is still explicit; repeated call before another source is harmless.
    p=(lsfg_pacer){.last_frame=300.,.smoothed_interval=1.f/60,.issued=3,.fixed_base=60};
    resume(&p,300.02);resume(&p,300.03);
    assert(!lsfg_pacer_plan(&p,3,4,0,300.03).generations);
    assert(lsfg_pacer_plan(&p,3,4,0,300.03+1./60.).generations==3);
}
'''
        with tempfile.TemporaryDirectory() as d:
            c=Path(d)/'resume.c';c.write_text(code);exe=c.with_suffix('')
            cmd=['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(android),str(c),
                 str(android/'lsfg/lsfg_pacer.c'),'-lm','-o',str(exe)]
            if modern:cmd.append('-DEXPLICIT_RESUME')
            subprocess.run(cmd,check=True)
            run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=5)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
