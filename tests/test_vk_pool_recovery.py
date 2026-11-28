"""Exercise idle-pool hysteresis with the production C policy."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class PoolRecovery(unittest.TestCase):
    def test_immediate_trim_and_delayed_recovery(self):
        code = r'''
#include <assert.h>
#include "gfx_vk_pool_pressure.h"
int main(void) {
    vk_pool_recovery s={0};
    uint64_t total=7204ull*1024, full=384ull<<20, low=64ull<<20;
    uint64_t b=vk_pool_reuse_budget_update(&s,full,822ull*1024,total);
    assert(b==0);
    b=vk_pool_reuse_budget_update(&s,b,1100ull*1024,total);assert(b==0);
    b=vk_pool_reuse_budget_update(&s,b,822ull*1024,total);assert(b==0);
    for(int i=0;i<3;i++){b=vk_pool_reuse_budget_update(&s,b,1100ull*1024,total);assert(b==0);}
    for(int i=0;i<2;i++){b=vk_pool_reuse_budget_update(&s,b,1300ull*1024,total);assert(b==0);}
    b=vk_pool_reuse_budget_update(&s,b,1300ull*1024,total);assert(b==low);
    for(int i=0;i<2;i++){b=vk_pool_reuse_budget_update(&s,b,2000ull*1024,total);assert(b==low);}
    b=vk_pool_reuse_budget_update(&s,b,2000ull*1024,total);assert(b==full);
    b=vk_pool_reuse_budget_update(&s,b,822ull*1024,total);assert(b==0);
    return 0;
}'''
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'check.c';p.write_text(code)
            exe=p.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(ROOT/'nxnative/dm/android'),str(p),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
