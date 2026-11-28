"""Run the actual desired-presentation-time scheduler with a deterministic clock."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class PaceDependencies(unittest.TestCase):
    def test_completed_dependencies_and_future_horizon(self):
        android = ROOT / 'nxnative/dm/android'
        src = (android / 'gfx_vk_present.c').read_text()
        start = src.index('static uint64_t pace_desired(')
        body = src[start:src.index('\n}', start)+2]
        modern = 'int dependencies_retired' in body
        code = r'''
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include "gfx_vk_pace_policy.h"
#define VKLOG(...) do { if (0) printf(__VA_ARGS__); } while (0)
static struct {int has_dtiming;} V={1};
static int g_swap=1,g_pace_on=1;
static uint64_t g_refresh_ns=8333333,g_pace_anchor=1000000000,g_pace_target,g_pace_ideal;
static uint64_t g_pace_n,g_pace_resync,g_pace_slip,g_pace_horizon,g_pace_ready;
static uint64_t now=1000000000;
static uint64_t mono_ns(void){return now;}
double vk_gpu_ms_recent(void){return 12.0;}
static uint64_t pace_snap(uint64_t t){return vk_pace_snap(t,g_refresh_ns,g_pace_anchor);}
''' + body + r'''
static uint64_t step(double period,int ready){
#ifdef NEW_PACE
    return pace_desired(period,ready);
#else
    (void)ready;return pace_desired(period);
#endif
}
int main(void){
    (void)g_pace_ready;(void)g_pace_horizon;(void)pace_snap;
    uint64_t pending=step(1./120.,0);
    g_pace_target=g_pace_ideal=0;
    uint64_t ready=step(1./120.,1);
    if(!ready || ready>=pending){fprintf(stderr,"retired dependencies still charged full scene: %llu >= %llu\n",(unsigned long long)ready,(unsigned long long)pending);return 1;}
    assert(pending-ready>=g_refresh_ns);
    g_pace_target=g_pace_ideal=now+500000000;
    assert(step(1./120.,1)==0&&!g_pace_target&&!g_pace_ideal);
    assert(g_pace_horizon==1);
    assert(step(1./120.,1)>now);
    // Normal 120 Hz cadence with steady CPU clock and completed dependencies.
    uint64_t prev=g_pace_target;
    for(unsigned i=0;i<200;i++){
        now+=g_refresh_ns;
        assert(step(1./120.,1)>now);
        assert(g_pace_target-prev==g_refresh_ns);
        prev=g_pace_target;
    }
    // A missed refresh rebases; a disabled/unlocked schedule clears its phase.
    now+=200000000;
    assert(step(1./120.,1)>now&&g_pace_resync==1);
    assert(!step(0,1)&&!g_pace_target&&!g_pace_ideal);
    // Preserve fractional 40 fps scheduling on 60 Hz when the source arrives on time.
    g_refresh_ns=16666667;g_pace_anchor=now;g_pace_target=g_pace_ideal=0;
    uint64_t initial=step(1./40.,1);assert(initial>now);
    prev=g_pace_target;
    for(unsigned i=0;i<40;i++){
        now+=25000000;
        assert(step(1./40.,1)>now);
        assert(g_pace_target-prev==g_refresh_ns||g_pace_target-prev==2*g_refresh_ns);
        prev=g_pace_target;
    }
}
'''
        with tempfile.TemporaryDirectory() as d:
            c=Path(d)/'pace.c';c.write_text(code);exe=c.with_suffix('')
            cmd=['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(android),str(c),'-lm','-o',str(exe)]
            if modern:cmd.append('-DNEW_PACE')
            subprocess.run(cmd,check=True)
            run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=5)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
