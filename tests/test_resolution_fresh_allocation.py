"""Test production fresh-owner allocation at mocked Vulkan boundaries."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
STUB=r"""
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
typedef struct { void *pool; int64_t off; struct { uint64_t size; } L; struct { int flags; } d; } tex_t;
typedef struct { int unused; } vkfmt_t;
typedef struct { unsigned image,memory,layout; uint64_t bytes,native_bytes; } vk_rs_variant;
typedef struct { uint64_t pool,rs_guest_addr; int64_t off,size; int flags; unsigned img,mem,layout; vk_rs_variant *rs_backing; } vkimg_t;
struct { int dev; } V;
static unsigned native_descriptions,native_memory,destroyed,variants,freed,mode;
static vkimg_t *img_new_description(const tex_t *t,const vkfmt_t *f,int unbound) {
    (void)t;(void)f;native_descriptions++;native_memory+=!unbound;
    if(mode==3)return NULL;
    vkimg_t *im=calloc(1,sizeof *im);im->img=1;return im;
}
static vk_rs_variant *vk_rs_variant_create(vkimg_t *im,uint32_t w,uint32_t h) {
    assert(im->img==1 && !im->mem && im->pool && im->size==4096 && im->rs_guest_addr==1024 && w==400 && h==300);
    if(mode==1)return NULL;
    variants++;vk_rs_variant *v=calloc(1,sizeof *v);
    *v=(vk_rs_variant){2,3,0,mode==2?4096:1024,4096};return v;
}
static void vkDestroyImage(int dev,unsigned img,void *p){(void)dev;(void)p;assert(img==1);destroyed++;}
static void vk_rs_variant_destroy(vk_rs_variant *v){if(v){freed++;free(v);}}
"""
MAIN=r"""
int main(void) {
    tex_t t={.pool=(void*)1,.off=16,.L={4096},.d={8}};vkfmt_t fmt={0};
    vkimg_t *im=rs_fresh_owner(&t,&fmt,400,300,1024);
    assert(im && im->img==2 && im->mem==3 && im->rs_backing->native_bytes==4096 && im->flags==8 && !native_memory && destroyed==1);
    vk_rs_variant_destroy(im->rs_backing);free(im);
    mode=1;assert(!rs_fresh_owner(&t,&fmt,400,300,1024) && destroyed==2 && freed==1);
    mode=2;assert(!rs_fresh_owner(&t,&fmt,400,300,1024) && destroyed==3 && freed==2);
    mode=3;assert(!rs_fresh_owner(&t,&fmt,400,300,1024) && destroyed==3);
    assert(native_descriptions==4 && !native_memory && variants==2);
}
"""
class FreshAllocationTests(unittest.TestCase):
    def test_no_native_memory_and_failure_cleanup(self):
        cc=shutil.which("cc") or shutil.which("clang")
        if not cc:self.skipTest("C compiler required")
        source=(ROOT/"nxnative/dm/android/gfx_vk_tex.c").read_text()
        start=source.index("static vkimg_t *rs_fresh_owner(")
        end=source.index("// The real cache allocation transaction.",start)
        with tempfile.TemporaryDirectory() as d:
            c=Path(d)/"fresh.c"; exe=Path(d)/"fresh"
            c.write_text(STUB+source[start:end]+MAIN)
            subprocess.run([cc,"-std=c11","-Wall","-Wextra","-Werror",str(c),"-o",str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
