"""Host test of the real backend source-frame adaptive policy."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r"""
#include <assert.h>
#include "gfx_vk_resolution_adaptive.h"
int main(void) {
    nx_rs_adaptive s={0};
    assert(nx_rs_budget(30)==33333 && nx_rs_budget(40)==25000 && nx_rs_budget(60)==16667 && !nx_rs_budget((unsigned)-1));
    assert(!nx_rs_adaptive_reset(&s,51,100,60,0));
    assert(!nx_rs_adaptive_reset(&s,90,80,60,0));
    assert(nx_rs_adaptive_reset(&s,65,80,60,0) && s.scale==80);
    for (unsigned f=1;f<=11;f++) assert(!nx_rs_adaptive_sample(&s,f,22000,8000));
    assert(!nx_rs_adaptive_sample(&s,11,22000,8000));
    assert(nx_rs_adaptive_sample(&s,12,22000,8000) && s.scale==75);
    nx_rs_adaptive_requested(&s,20);
    for (unsigned f=13;f<=20;f++) assert(!nx_rs_adaptive_sample(&s,f,22000,8000));
    for (unsigned f=21;f<=100;f++) assert(!nx_rs_adaptive_sample(&s,f,22000,18000));
    for (unsigned f=101;f<=112;f++) nx_rs_adaptive_sample(&s,f,22000,8000);
    assert(s.scale==70);
    for (unsigned f=113;f<=124;f++) nx_rs_adaptive_sample(&s,f,22000,8000);
    assert(s.scale==65);
    for (unsigned f=125;f<=150;f++) assert(!nx_rs_adaptive_sample(&s,f,22000,8000));
    for (unsigned f=151;f<=209;f++) assert(!nx_rs_adaptive_sample(&s,f,10000,5000));
    assert(nx_rs_adaptive_sample(&s,210,10000,5000) && s.scale==70);
    assert(nx_rs_adaptive_reset(&s,50,90,(unsigned)-1,210));
    for (unsigned f=211;f<300;f++) assert(!nx_rs_adaptive_sample(&s,f,40000,5000));
    assert(s.scale==90);
    assert(nx_rs_adaptive_reset(&s,50,100,40,300));
    for (unsigned f=301;f<312;f++) assert(!nx_rs_adaptive_sample(&s,f,30000,5000));
    assert(!nx_rs_adaptive_sample(&s,312,30000,0));
    for (unsigned f=313;f<324;f++) assert(!nx_rs_adaptive_sample(&s,f,30000,5000));
    assert(!nx_rs_adaptive_sample(&s,325,30000,5000));
    assert(s.scale==100);
}
"""
class AdaptiveResolutionTests(unittest.TestCase):
    def test_source_frame_policy(self):
        cc = shutil.which("cc") or shutil.which("clang")
        if not cc:
            self.skipTest("C compiler required")
        with tempfile.TemporaryDirectory() as tmp:
            c = Path(tmp)/"adaptive.c"
            exe = Path(tmp)/"adaptive"
            c.write_text(SOURCE)
            subprocess.run([cc,"-std=c11","-Wall","-Wextra","-Werror","-I",str(ROOT/"nxnative/dm/gfx/nvn"),str(c),"-o",str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
