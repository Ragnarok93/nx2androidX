"""Exercise the production LSFG flow-size policy without Vulkan."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class FGFlow(unittest.TestCase):
    def test_auto_bounds_and_explicit_quality(self):
        code=r'''
#include <assert.h>
#include "gfx_vk_fg_flow.h"
int main(void){
    assert(dm_fg_flow_normalize(-1)==DM_FG_FLOW_ADAPTIVE);
    assert(dm_fg_flow_normalize(-2)==DM_FG_FLOW_AUTO);
    assert(dm_fg_flow_is_adaptive(-1,0)&&!dm_fg_flow_is_adaptive(0,0));
    assert(dm_fg_flow_is_adaptive(0,1)&&!dm_fg_flow_is_adaptive(50,1));
    assert(dm_fg_flow_step(0,1)==-1 && dm_fg_flow_step(-1,1)==25);
    assert(dm_fg_flow_step(25,-1)==-1 && dm_fg_flow_step(-1,-1)==0);
    assert(dm_fg_flow_step(100,1)==0 && dm_fg_flow_step(0,-1)==100);
    assert(dm_fg_effective_flow(-1,1920,1080)==dm_fg_effective_flow(0,1920,1080));
    assert(dm_fg_effective_flow(0,1280,720)==100);
    assert(dm_fg_effective_flow(0,1920,1080)==50);
    assert(dm_fg_effective_flow(0,1080,1920)==50);
    assert(dm_fg_effective_flow(0,2560,1440)==50);
    assert(dm_fg_effective_flow(0,3840,2160)==25);
    assert(dm_fg_effective_flow(0,UINT32_MAX,UINT32_MAX)==25);
    for(int pct=25;pct<=100;pct++)
        assert(dm_fg_effective_flow(pct,3840,2160)==pct);
    assert(dm_fg_effective_flow(1,1920,1080)==25);
    assert(dm_fg_effective_flow(101,1920,1080)==100);
    return 0;
}'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'check.c';p.write_text(code);exe=p.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(ROOT/'nxnative/dm/android'),str(p),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
