"""Live settings and source-frame mode transitions; no device dependency."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
CODE=r"""
#include <assert.h>
#include "gfx_vk_resolution_live.h"
int main(void) {
    nx_rs_live_config c=nx_rs_live_unpack(0);
    assert(c.mode==NX_RS_OFF && c.min==50 && c.max==100);
    assert(!nx_rs_live_pack((nx_rs_live_config){2,95,70}));
    c=nx_rs_live_max(c,75);c=nx_rs_live_min(c,80);assert(c.min==80&&c.max==80);
    c=nx_rs_live_max(c,60);assert(c.min==60&&c.max==60);
    c=(nx_rs_live_config){NX_RS_FIXED,50,75};
    unsigned packed=nx_rs_live_pack(c),out=0;
    nx_rs_live_policy p={0};
    assert(nx_rs_live_update(&p,packed,60,1,0,0,0,&out)&&out==75);
    for(unsigned i=2;i<20;i++)assert(!nx_rs_live_update(&p,packed,60,i,i,40000,5000,&out));
    c.mode=NX_RS_ADAPTIVE;c.max=90;packed=nx_rs_live_pack(c);
    assert(nx_rs_live_update(&p,packed,60,20,12,40000,5000,&out)&&out==90);
    for(unsigned i=21;i<=31;i++)assert(!nx_rs_live_update(&p,packed,60,i+8,i,40000,5000,&out));
    assert(nx_rs_live_update(&p,packed,60,40,32,40000,5000,&out)&&out==85);
    c.mode=NX_RS_OFF;packed=nx_rs_live_pack(c);
    assert(nx_rs_live_update(&p,packed,60,41,33,40000,5000,&out)&&out==100);
    assert(!nx_rs_live_update(&p,packed,60,42,34,40000,5000,&out));
    c.mode=NX_RS_ADAPTIVE;c.min=70;packed=nx_rs_live_pack(c);
    assert(nx_rs_live_update(&p,packed,(unsigned)-1,43,35,40000,5000,&out)&&out==90);
    for(unsigned i=44;i<150;i++)assert(!nx_rs_live_update(&p,packed,(unsigned)-1,i,i,40000,5000,&out));
}
"""
class LiveResolutionTests(unittest.TestCase):
    def test_live_modes_and_bounds(self):
        cc=shutil.which("cc") or shutil.which("clang")
        if not cc:self.skipTest("C compiler required")
        with tempfile.TemporaryDirectory() as d:
            c=Path(d)/"live.c";exe=Path(d)/"live"
            c.write_text(CODE)
            subprocess.run([cc,"-std=c11","-Wall","-Wextra","-Werror","-I",str(ROOT/"nxnative/dm/gfx/nvn"),str(c),"-o",str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
