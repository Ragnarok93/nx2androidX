"""Exercise production native-only descriptor enumeration at mocked registry boundary."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class NativeDescriptorTests(unittest.TestCase):
    def test_dynamic_slot_and_texel_proof(self):
        cc=shutil.which("cc") or shutil.which("clang")
        if not cc:self.skipTest("C compiler required")
        dm=ROOT/"nxnative/dm/android"
        header=(dm/"gfx_vk.h").read_text()
        replay=(dm/"gfx_vk_exec.c").read_text()
        defs=header[header.index("enum { RK_SUPPORT"):header.index("_Static_assert(sizeof(dmsv_bind)")]
        code=r"""
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include "gfx_vk_slot.h"
typedef struct {int id;} tex_t;
typedef struct {int id;} view_t;
static tex_t t[5]={{0},{1},{2},{3},{4}};
static unsigned seen,expected_texel;
static void *vk_tex_by_id(int id,const view_t **v,const tex_t **out) {
    if(id<1||id>4)return NULL;
    *v=NULL;*out=&t[id];return &t[id];
}
static int check(const tex_t *t,const view_t *v,int texel,void *ctx) {
    (void)v;(void)ctx;assert((unsigned)texel==expected_texel);
    seen|=1u<<t->id;
    return t->id!=3;
}
"""
        code+=defs+replay[replay.index("typedef int (*rs_native_check)"):replay.index("static int resolution_texel_span(")]
        code+=r"""
int main(void) {
    uint64_t tex[256]={0},img[32]={0};
    dmsv_bind b={.kind=RK_TEXD,.desc=RD_IMAGE,.count=2};
    tex[0]=1;tex[128]=3;tex[129]=2;
    assert(resolution_native_binding(&b,tex,img,0,1,check,NULL)==1 && seen==6);
    expected_texel=1;seen=0;b.desc=RD_TBUF;
    assert(resolution_native_binding(&b,tex,img,0,1,check,NULL)==-1 && seen==8);
    seen=0;assert(resolution_native_binding(&b,tex,img,0,0,check,NULL)==1 && !seen);
    tex[128]=4;assert(resolution_native_binding(&b,tex,img,0,1,check,NULL)==1 && seen==20);
    b.count=33;assert(resolution_native_binding(&b,tex,img,0,1,check,NULL)==-1);
    b=(dmsv_bind){.kind=RK_TBUF,.desc=RD_TBUF,.count=1,.reg=32};
    img[0]=3;assert(resolution_native_binding(&b,tex,img,0,1,check,NULL)==-1);
    img[0]=2;assert(resolution_native_binding(&b,tex,img,0,1,check,NULL)==1);
    b.kind=RK_IMG_TBUF;b.desc=RD_STORAGE_TBUF;b.reg=0;
    assert(resolution_native_binding(&b,tex,img,0,1,check,NULL)==-1);
    assert(resolution_native_binding(&b,tex,img,1,1,check,NULL)==1);
    img[0]=0;assert(resolution_native_binding(&b,tex,img,1,1,check,NULL)==-1);
    b=(dmsv_bind){.kind=RK_SAMPD,.desc=RD_SAMPLER,.count=32};
    assert(resolution_native_binding(&b,tex,img,0,1,check,NULL)==1);
    b.count=33;assert(resolution_native_binding(&b,tex,img,0,1,check,NULL)==-1);
    b.kind=RK_TEX;assert(!resolution_native_binding(&b,tex,img,0,1,check,NULL));
}
"""
        with tempfile.TemporaryDirectory() as d:
            src=Path(d)/"check.c";exe=Path(d)/"check"
            src.write_text(code)
            subprocess.run([cc,"-std=c11","-Wall","-Wextra","-Werror","-I",str(dm),str(src),"-o",str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
