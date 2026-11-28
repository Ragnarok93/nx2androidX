"""Compile the real freeze and active-time sampling functions with a fake wall clock."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class PauseAccounting(unittest.TestCase):
    def test_clock_freeze_overlap_and_deterministic_independence(self):
        text=(ROOT/'nxnative/dm/host/hle_os.c').read_text()
        freeze=text[text.index('static void guest_clock_freeze_apply('):text.index('// Entropy source:')]
        code=r'''
#define __ANDROID__ 1
#include <assert.h>
#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>
static _Atomic uint64_t g_gfrozen,g_glag;
static _Atomic int g_gfz;
static _Atomic int dm_host_work;
static uint64_t clock_ns=1000000000ull;
static uint64_t now_ns(void){return clock_ns;}
'''+freeze+r'''
int main(void){
    uint64_t wall,active=dm_guest_active_time(&wall);
    assert(active==wall);
    clock_ns+=10000000;
    guest_clock_freeze_for(1,1);
    clock_ns+=2000000000;
    guest_clock_freeze_for(2,1);
    clock_ns+=1000000000;
    guest_clock_freeze_for(1,0);
    assert(dm_guest_active_time(&wall)==active+10000000);
    clock_ns+=2000000000;
    guest_clock_freeze_for(2,0);
    clock_ns+=7000000;
    uint64_t resumed=dm_guest_active_time(&wall);
    assert(resumed-active==17000000);
    assert(wall-active==5017000000ull);
    clock_ns+=100000000; // genuine unpaused stall remains visible
    assert(dm_guest_active_time(&wall)-resumed==100000000);
    return 0;
}'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'check.c';p.write_text(code);exe=p.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-pthread',str(p),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
