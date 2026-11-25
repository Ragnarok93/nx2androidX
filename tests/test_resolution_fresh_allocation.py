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
        end=source.index("// Prepare hash capacity in a private map",start)
        with tempfile.TemporaryDirectory() as d:
            c=Path(d)/"fresh.c"; exe=Path(d)/"fresh"
            c.write_text(STUB+source[start:end]+MAIN)
            subprocess.run([cc,"-std=c11","-Wall","-Wextra","-Werror",str(c),"-o",str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_private_cache_capacity_preserves_entries(self):
        cc=shutil.which("cc") or shutil.which("clang")
        if not cc:self.skipTest("C compiler required")
        source=(ROOT/"nxnative/dm/android/gfx_vk_tex.c").read_text()
        start=source.index("static int rs_stage_cache_capacity(")
        end=source.index("// Native-to-physical replacement",start)
        main=r"""
#include <assert.h>
int main(void) {
    nvn_hmap old={.klen=sizeof(unsigned)}, next={0};
    assert(rs_stage_cache_capacity(&old,9,&next) && !old.cap && next.cap==64);
    free(next.keys);free(next.vals);next=(nvn_hmap){0};
    unsigned values[49]={0};
    for(unsigned i=0;i<48;i++)nvn_hm_put(&old,&i,&values[i]);
    assert(old.cap==64 && old.n==48);
    assert(rs_stage_cache_capacity(&old,1,&next) && next.cap==128 && next.n==48);
    for(unsigned i=0;i<48;i++)assert(nvn_hm_get(&next,&i)==&values[i] && nvn_hm_get(&old,&i)==&values[i]);
    unsigned i=48;nvn_hm_put(&next,&i,&values[i]);
    assert(next.n==49 && old.n==48 && !nvn_hm_get(&old,&i));
    free(old.keys);free(old.vals);free(next.keys);free(next.vals);
}
"""
        with tempfile.TemporaryDirectory() as d:
            c=Path(d)/"map.c"; exe=Path(d)/"map"
            c.write_text('#include "nvn_hmap.h"\\n'.replace('\\n','\n')+source[start:end]+main)
            subprocess.run([cc,"-std=c11","-Wall","-Wextra","-Werror","-I",str(ROOT/"nxnative/dm/gfx/nvn"),str(c),"-o",str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_native_overwrite_admission(self):
        cc=shutil.which("cc") or shutil.which("clang")
        if not cc:self.skipTest("C compiler required")
        source=(ROOT/"nxnative/dm/android/gfx_vk_tex.c").read_text()
        start=source.index("static int rs_native_overwrite_candidate(")
        end=source.index("// The real cache allocation transaction.",start)
        code=r"""
#include <assert.h>
#include <stddef.h>
enum { VK_IMAGE_LAYOUT_GENERAL=1,VK_IMAGE_LAYOUT_UNDEFINED=0 };
typedef struct {
    unsigned img,mem,layout,rs_backing,sub,full,rs_presented,novl,packaged,lin_mem,bc_draw,bc_of;
} vkimg_t;
"""+source[start:end]+r"""
int main(void) {
    vkimg_t im={.img=1,.mem=2,.layout=VK_IMAGE_LAYOUT_GENERAL};
    assert(rs_native_overwrite_candidate(&im));
    assert(!rs_native_overwrite_candidate(NULL));
    im.layout=VK_IMAGE_LAYOUT_UNDEFINED;assert(rs_native_overwrite_candidate(&im));
    im.layout=2;assert(!rs_native_overwrite_candidate(&im));im.layout=1;
    im.rs_presented=1;assert(!rs_native_overwrite_candidate(&im));im.rs_presented=0;
    im.sub=1;assert(!rs_native_overwrite_candidate(&im));im.sub=0;
    im.packaged=1;assert(!rs_native_overwrite_candidate(&im));im.packaged=0;
    im.rs_backing=1;assert(!rs_native_overwrite_candidate(&im));im.rs_backing=0;
    im.novl=1;assert(!rs_native_overwrite_candidate(&im));im.novl=0;
    assert(rs_native_overwrite_candidate(&im));
}
"""
        with tempfile.TemporaryDirectory() as d:
            c=Path(d)/"native.c";exe=Path(d)/"native"
            c.write_text(code)
            subprocess.run([cc,"-std=c11","-Wall","-Wextra","-Werror",str(c),"-o",str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
