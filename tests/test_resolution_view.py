"""Canonical single-subresource views used by the production scaling gates."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
SOURCE=r"""
#include <assert.h>
#include "gfx_vk_resolution_view.h"
int main(void) {
    tex_t t={.d={.target=NVN_TEXTURE_TARGET_2D,.levels=1,.depth=1,.samples=1,.format=NVN_FORMAT_RGBA8}};
    view_t v={.set_levels=1,.nlevels=1,.set_layers=1,.nlayers=1,.set_format=1,.format=NVN_FORMAT_RGBA8,.set_target=1,.target=NVN_TEXTURE_TARGET_2D};
    assert(nx_rs_view_equivalent(&t,NULL));
    assert(nx_rs_view_equivalent(&t,&(view_t){0}));
    assert(nx_rs_view_equivalent(&t,&v));
    v.base_level=1; assert(!nx_rs_view_equivalent(&t,&v)); v.base_level=0;
    v.nlevels=2; assert(!nx_rs_view_equivalent(&t,&v)); v.nlevels=1;
    v.format=NVN_FORMAT_RGBA8_SRGB; assert(!nx_rs_view_equivalent(&t,&v)); v.format=NVN_FORMAT_RGBA8;
    v.min_layer=1; assert(!nx_rs_view_equivalent(&t,&v)); v.min_layer=0;
    v.nlayers=2; assert(!nx_rs_view_equivalent(&t,&v)); v.nlayers=1;
    v.set_swizzle=1; v.swz[0]=NVN_TEXTURE_SWIZZLE_R;v.swz[1]=NVN_TEXTURE_SWIZZLE_G;v.swz[2]=NVN_TEXTURE_SWIZZLE_B;v.swz[3]=NVN_TEXTURE_SWIZZLE_A;
    assert(nx_rs_view_equivalent(&t,&v));
    v.swz[0]=NVN_TEXTURE_SWIZZLE_A; assert(!nx_rs_view_equivalent(&t,&v)); v.swz[0]=NVN_TEXTURE_SWIZZLE_R;
    t.set_swz=1; assert(!nx_rs_view_equivalent(&t,&v)); t.set_swz=0;
    v.set_dsmode=1;v.dsmode=NVN_TEXTURE_DEPTH_STENCIL_MODE_STENCIL;
    assert(!nx_rs_view_equivalent(&t,&v));
    v.dsmode=NVN_TEXTURE_DEPTH_STENCIL_MODE_DEPTH;assert(nx_rs_view_equivalent(&t,&v));
    t.d.levels=2;assert(!nx_rs_view_equivalent(&t,&v));t.d.levels=1;
    t.d.depth=2;assert(!nx_rs_view_equivalent(&t,&v));t.d.depth=1;
    t.d.samples=4;assert(!nx_rs_view_equivalent(&t,&v));
}
"""
class ViewTests(unittest.TestCase):
    def test_single_subresource_equivalence(self):
        cc=shutil.which("cc") or shutil.which("clang")
        if not cc:self.skipTest("C compiler required")
        inc=ROOT/"nxnative/dm/gfx/nvn"
        with tempfile.TemporaryDirectory() as d:
            c,exe=Path(d)/"view.c",Path(d)/"view"
            c.write_text(SOURCE)
            subprocess.run([cc,"-std=c11","-Wall","-Wextra","-Werror","-I",str(inc),
                            "-I",str(inc/"include"),"-I",str(inc.parent/"tex"),str(c),"-o",str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
    def test_depth_stencil_owner_identity(self):
        cc=shutil.which("cc") or shutil.which("clang")
        if not cc:self.skipTest("C compiler required")
        inc=ROOT/"nxnative/dm/gfx/nvn"
        with tempfile.TemporaryDirectory() as d:
            exe=Path(d)/"depth_identity"
            subprocess.run([cc,"-std=c11","-Wall","-Wextra","-Werror","-I",str(inc),
                            "-I",str(inc/"include"),"-I",str(inc.parent/"tex"),
                            str(ROOT/"tests/resolution_depth_identity.c"),"-o",str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_zero_count_single_subresource(self):
        cc=shutil.which("cc") or shutil.which("clang")
        if not cc:self.skipTest("C compiler required")
        inc=ROOT/"nxnative/dm/gfx/nvn"
        with tempfile.TemporaryDirectory() as d:
            exe=Path(d)/"view_count"
            subprocess.run([cc,"-std=c11","-Wall","-Wextra","-Werror","-I",str(inc),
                            "-I",str(inc/"include"),"-I",str(inc.parent/"tex"),
                            str(ROOT/"tests/resolution_view_count.c"),"-o",str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_sampled_wrapper_identity(self):
        cc=shutil.which("cc") or shutil.which("clang")
        if not cc:self.skipTest("C compiler required")
        inc=ROOT/"nxnative/dm/gfx/nvn"
        with tempfile.TemporaryDirectory() as d:
            exe=Path(d)/"wrapper"
            subprocess.run([cc,"-std=c11","-Wall","-Wextra","-Werror","-I",str(inc),
                            "-I",str(inc/"include"),"-I",str(inc.parent/"tex"),
                            str(ROOT/"tests/resolution_sampled_wrapper.c"),"-o",str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_shared_gate(self):
        base=ROOT/"nxnative/dm/android"
        scope=(base/"gfx_vk_resolution_scope.inc").read_text()
        # Verify the specific production gates, not the total number of
        # calls: new safe consumer paths can legitimately add call sites.
        self.assertIn("if (hit || !nx_rs_view_equivalent(t,v)",scope)
        self.assertIn("if (!nx_rs_view_equivalent(t,v) || t->d.width",scope)
        self.assertIn("if (!nx_rs_view_equivalent(t,v))",scope)
        self.assertIn("if (storage && (!nx_rs_view_equivalent(t,v)",scope)
        self.assertIn("if (supported && !nx_rs_view_equivalent(t,v)) supported=0;",(base/"gfx_vk_tex.c").read_text())
        self.assertIn("if (t && nx_rs_view_equivalent(t,v))",(base/"gfx_vk_exec.c").read_text())
