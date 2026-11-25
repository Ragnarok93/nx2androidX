"""Closed replay lifetime authorization state machine and integration seams."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
SOURCE=r"""
#include <assert.h>
#include "gfx_vk_resolution_scope.h"
int main(void) {
    nx_rs_scope s;
    assert(!nx_rs_scope_init(&s,0) && !nx_rs_scope_init(&s,512));
    assert(nx_rs_scope_init(&s,257));
    assert(!nx_rs_scope_draw(&s,1,0));
    assert(!nx_rs_scope_clear(&s,1,0));
    assert(nx_rs_scope_clear(&s,1,1));
    assert(!nx_rs_scope_switch(&s,0) && !nx_rs_scope_draw(&s,1,0));
    assert(nx_rs_scope_clear(&s,256,1));
    assert(!nx_rs_scope_draw(&s,0,0) && !nx_rs_scope_draw(&s,1,1));
    assert(nx_rs_scope_draw(&s,1,0));
    assert(!nx_rs_scope_switch(&s,1));
    assert(nx_rs_scope_switch(&s,0));
    assert(!nx_rs_scope_discard(&s,1));
    assert(nx_rs_scope_draw(&s,1,0) && !nx_rs_scope_switch(&s,1));
    assert(nx_rs_scope_draw(&s,1,1) && nx_rs_scope_switch(&s,1));
    assert(!nx_rs_scope_draw(&s,1,1) && !nx_rs_scope_discard(&s,2));
    assert(nx_rs_scope_discard(&s,256)==1 && !nx_rs_scope_discard(&s,256));
    assert(nx_rs_scope_discard(&s,1)==2 && s.phase==3);
    assert(!nx_rs_scope_discard(&s,1));
}
"""
class ScopeTests(unittest.TestCase):
    def test_closed_lifetime(self):
        cc=shutil.which("cc") or shutil.which("clang")
        if not cc:self.skipTest("C compiler required")
        with tempfile.TemporaryDirectory() as d:
            c=Path(d)/"scope.c"; exe=Path(d)/"scope"
            c.write_text(SOURCE)
            subprocess.run([cc,"-std=c11","-Wall","-Wextra","-Werror","-I",str(ROOT/"nxnative/dm/gfx/nvn"),str(c),"-o",str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
    def test_real_replay_seam(self):
        base=ROOT/"nxnative/dm/android"
        scope=(base/"gfx_vk_resolution_scope.inc").read_text()
        replay=(base/"gfx_vk_exec.c").read_text()
        self.assertLess(scope.index("rs_scope_scan(next,&group)"),scope.index("vk_rs_allocate_fresh_group(group.target"))
        self.assertIn("!nvn_replaying_job()",scope)
        self.assertIn("vk_rs_scope_freeze_seal();g_rs_scope_end=end",scope)
        self.assertIn("rs_scope_try_begin(p+h.size)",replay)
        self.assertIn("vk_rs_suspend_discarded_group()",replay)
