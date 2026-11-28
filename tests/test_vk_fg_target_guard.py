"""Run actual LSFG pacer under deterministic explicit/variable source traces."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class FGTargetGuard(unittest.TestCase):
    def test_source_target_survives_overload_and_recovers(self):
        android = ROOT / 'nxnative/dm/android'
        # Allows this exact regression to execute against the audited ABI as RED.
        fixed = 'int fixed_base;' in (android / 'lsfg/lsfg.h').read_text()
        code = r'''
#include <assert.h>
#include <stdio.h>
#include "lsfg/lsfg.h"
static void init(lsfg_pacer *p,int rate) {
    lsfg_pacer_reset(p);
    p->min_base = rate * .9f;
#ifdef EXPLICIT_BASE
    p->fixed_base = rate;
#endif
}
static size_t tick(lsfg_pacer *p,double *t,int mult,int target,double dt) {
    *t += dt;
    return lsfg_pacer_plan(p,3,mult,target,*t).generations;
}
int main(void) {
    lsfg_pacer p; double t=100.; size_t n=0;
    // A 60 FPS base targeting 60 displayed FPS must never ask for FG merely
    // because the source fell behind. Includes start already overloaded.
    init(&p,60);
    for (int i=0;i<240;i++) {
        n=tick(&p,&t,2,60,1./40.);
        if (n) {fprintf(stderr,"slow base added optional frames at output=base target\n");return 1;}
    }
    // Adaptive 120 output learns one extra at 60 base, no additional demand
    // at 40, removes optional work and automatically recovers at 60.
    init(&p,60);
    for(int i=0;i<300;i++)n=tick(&p,&t,2,120,1./60.);
    assert(n==1);
    for(int i=0;i<120;i++){
        n=tick(&p,&t,2,120,1./40.);
        assert(n<=1);
        if(i>=60)assert(!n);
    }
    for(int i=0;i<300;i++)n=tick(&p,&t,2,120,1./60.);
    assert(n==1);
    // Fixed mode never rewrites the requested multiplier. Long overload
    // suppresses optional generation; a few jitter frames do not latch off.
    for(int rate=30;rate<=60;rate+=10){
        if(rate==50)continue;
        for(int mult=2;mult<=4;mult++){
            init(&p,rate);
            for(int i=0;i<120;i++)n=tick(&p,&t,mult,0,1./rate);
            assert(n==(size_t)(mult-1));
            for(int i=0;i<3;i++)n=tick(&p,&t,mult,0,1./(rate*.8));
            for(int i=0;i<60;i++)n=tick(&p,&t,mult,0,1./rate);
            assert(n==(size_t)(mult-1));
            for(int i=0;i<120;i++)n=tick(&p,&t,mult,0,1./(rate*2./3.));
            assert(!n);
            for(int i=0;i<120;i++)n=tick(&p,&t,mult,0,1./rate);
            assert(n==(size_t)(mult-1));
        }
    }
    // Legacy variable source, no explicit target: still interpolate at 30
    // to reach 60 and preserve the existing pacer behavior.
    lsfg_pacer_reset(&p);
    for(int i=0;i<240;i++)n=tick(&p,&t,2,60,1./30.);
    assert(n==1);
    // Source interruption is not a new requested cadence.
    init(&p,60);
    for(int i=0;i<60;i++)n=tick(&p,&t,2,0,1./60.);
    assert(n==1 && !tick(&p,&t,2,0,1.2));
    for(int i=0;i<120;i++)n=tick(&p,&t,2,0,1./60.);
    assert(n==1);
    puts("explicit base guard, adaptive output demand, fixed multiplier recovery, jitter and stall: PASS");
}
'''
        with tempfile.TemporaryDirectory() as d:
            c = Path(d) / 'pacer.c'
            c.write_text(code)
            exe = c.with_suffix('')
            cmd = ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(android), str(c),
                   str(android / 'lsfg/lsfg_pacer.c'), '-lm', '-o', str(exe)]
            if fixed:
                cmd.append('-DEXPLICIT_BASE')
            subprocess.run(cmd, check=True)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
