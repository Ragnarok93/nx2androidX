"""Check refresh jitter and distinct display deadlines under FG cadence."""
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class PacePolicy(unittest.TestCase):
    def test_refresh_jitter_and_grid(self):
        source=r'''
#include <assert.h>
#include "gfx_vk_pace_policy.h"
int main(void){
    uint64_t r=0;
    assert(vk_pace_refresh_update(&r,8331788));
    assert(!vk_pace_refresh_update(&r,8340000)&&r==8331788);
    assert(!vk_pace_refresh_update(&r,8310000)&&r==8331788);
    assert(!vk_pace_refresh_update(&r,0)&&r==8331788);
    assert(vk_pace_refresh_update(&r,16666667)&&r==16666667);
    const uint64_t a=1000000000,r60=16666667;
    uint64_t prev=a,ideal=a;
    // 40 Hz on 60 Hz must preserve the fractional cadence (1,2 or 2,1).
    for(unsigned i=0;i<40;i++){
        ideal+=25000000;
        uint64_t next=vk_pace_next_grid(ideal,prev,r60,a);
        assert(next-prev==r60||next-prev==2*r60);prev=next;
    }
    assert(prev==a+60*r60);
    // An output cadence temporarily above panel refresh cannot reuse the
    // same display deadline (even after a small reported phase shift).
    prev=a;ideal=a+8333333;
    uint64_t next=vk_pace_next_grid(ideal,prev,r60,a);
    assert(next==a+r60);
    next=vk_pace_next_grid(ideal+8333333,next,r60,a+10000);
    assert(next==a+10000+2*r60);
    return 0;
}'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'case.c';p.write_text(source);exe=p.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(ROOT/'nxnative/dm/android'),str(p),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
