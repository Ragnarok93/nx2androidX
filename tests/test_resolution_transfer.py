"""Whole-display reconstruction geometry and refusal of exact raw copies."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
CODE=r"""
#include <assert.h>
#include "gfx_vk_resolution_transfer.h"
int main(void) {
    tex_t s={.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=1280,.height=720,.depth=1,.levels=1}};
    tex_t d=s;d.d.flags=NVN_TEXTURE_FLAGS_DISPLAY;
    p_copy_t2t c={.src=&s,.dst=&d,.sr={0,0,0,1280,720,1},.dr={0,0,0,1280,720,1}};
    assert(nx_rs_display_copy_shape(&c));
    d.d.flags=0;assert(!nx_rs_display_copy_shape(&c));d.d.flags=NVN_TEXTURE_FLAGS_DISPLAY;
    c.sr.width=1279;assert(!nx_rs_display_copy_shape(&c));c.sr.width=1280;
    c.has_sv=1;assert(!nx_rs_display_copy_shape(&c));c.has_sv=0;
    c.flags=DM_COPY_RESOLVE;assert(!nx_rs_display_copy_shape(&c));c.flags=1;
    d.d.format=NVN_FORMAT_BGRA8;assert(!nx_rs_display_copy_shape(&c));d.d.format=s.d.format;
    d.d.samples=4;assert(!nx_rs_display_copy_shape(&c));d.d.samples=1;
    d.d.width=1920;d.d.height=1080;c.dr.width=1920;c.dr.height=1080;
    assert(nx_rs_display_copy_shape(&c));
    d.has_pkg_data=1;assert(!nx_rs_display_copy_shape(&c));
}
"""
class ResolutionTransferTests(unittest.TestCase):
    def test_display_copy_shape(self):
        cc=shutil.which("cc") or shutil.which("clang")
        if not cc:self.skipTest("C compiler required")
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);c=p/"check.c";exe=p/"check";c.write_text(CODE)
            inc=ROOT/"nxnative/dm/gfx/nvn"
            subprocess.run([cc,"-std=c11","-Wall","-Wextra","-Werror","-I",str(inc),"-I",str(inc/"include"),"-I",str(ROOT/"nxnative/dm/gfx/tex"),str(c),"-o",str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
