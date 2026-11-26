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

    def test_changing_scale_rolls_back_complete_lifetime_on_failure(self):
        cc=shutil.which("cc") or shutil.which("clang")
        if not cc:self.skipTest("C compiler required")
        source=(ROOT/"nxnative/dm/android/gfx_vk_tex.c").read_text()
        start=source.index("static int rs_stage_cache_capacity(")
        end=source.index("\n}",source.index("int vk_rs_allocate_fresh_group("))+2
        code=r"""
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include "nvn_obj.h"
#include "nvn_hmap.h"
#include "gfx_vk_resolution_scope.h"
#include "gfx_vk_resolution_active.h"
#include "gfx_vk_resolution_budget.h"
#include "gfx_vk_resolution_draw.h"
#include "gfx_vk_resolution_restore.h"
#include "gfx_vk_resolution_preflight.h"
#define VK_IMAGE_LAYOUT_GENERAL 1
#define VK_IMAGE_LAYOUT_UNDEFINED 0
#define NX_RS_RESTORE_BUDGET_BYTES (64ull * 1024 * 1024)
typedef struct { uint64_t pool; int64_t off; nvn_texdesc d; uint8_t family; } akey;
typedef struct { int compressed,dec,is_int,depth,stencil; } vkfmt_t;
typedef struct {
    uint64_t bytes,native_bytes;
    unsigned physical_w,physical_h,layout,native_only;
    atomic_int refs;
} vk_rs_variant;
typedef struct {
    uint64_t pool,rs_guest_addr,used;
    int64_t size;
    unsigned layout,img,mem,sub,full,novl,packaged,lin_mem,bc_draw,bc_of;
    unsigned family_closed,rs_presented,family_formats,w,h;
    vk_rs_variant *rs_backing;
} vkimg_t;
static nvn_hmap g_by_key={.klen=sizeof(akey)};
static pthread_mutex_t g_img_mu=PTHREAD_MUTEX_INITIALIZER;
static nx_rs_active *g_rs_live, g_rs_dormant;
static uint64_t g_tex_frame=7,g_img_n,g_img_bytes;
static struct { atomic_uint_fast64_t bytes; } g_rs_restore_budget;
static unsigned created,freed,published,fail_at;
static vk_rs_variant *retired;
static int vk_sub_open(void) {return 1;}
static int vk_rs_pass_idle(void) {return 1;}
static int vk_rs_provenance(void) {return 1;}
static int packaged_tex(const tex_t *t) {(void)t;return 0;}
int nvn_ft_written(const tex_t *t) {(void)t;return 0;}
static uint64_t rs_guest_address(const tex_t *t) {return (uintptr_t)t->mem;}
static unsigned texture_family_bit(int fmt) {(void)fmt;return 0;}
static const vkfmt_t *vk_fmt(int fmt) {static const vkfmt_t f={0};(void)fmt;return &f;}
static int rs_is_scaled(const vkimg_t *im) {return im->rs_backing && im->rs_backing->physical_w!=im->w;}
static void rs_active_publish(void) {published++;}
static vk_rs_variant *vk_rs_variant_create(vkimg_t *im,uint32_t w,uint32_t h) {
    (void)im;
    if (++created==fail_at) return NULL;
    vk_rs_variant *v=calloc(1,sizeof *v);assert(v);
    v->bytes=2048;v->native_bytes=8192;
    v->physical_w=w;v->physical_h=h;v->layout=VK_IMAGE_LAYOUT_GENERAL;
    atomic_init(&v->refs,1);
    return v;
}
static void vk_rs_variant_destroy(vk_rs_variant *v) {if(v){freed++;free(v);}}
static vkimg_t *rs_fresh_owner(const tex_t *t,const vkfmt_t *f,uint32_t w,uint32_t h,uint64_t guest) {
    (void)f;
    vkimg_t *im=calloc(1,sizeof *im);assert(im);
    im->rs_backing=vk_rs_variant_create(im,w,h);
    if (!im->rs_backing) {free(im);return NULL;}
    im->pool=(uintptr_t)t->pool;im->rs_guest_addr=guest;im->size=t->L.size;
    im->w=t->d.width;im->h=t->d.height;im->layout=VK_IMAGE_LAYOUT_GENERAL;
    return im;
}
static void rs_publish_backing(vkimg_t *im,vk_rs_variant *next) {
    assert(!retired);
    retired=im->rs_backing;im->rs_backing=next;
}
"""+source[start:end]+r"""
int main(void) {
    static uint8_t storage[8192];
    pool_t pool={.mem=storage,.size=sizeof storage,.flags=NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS};
    tex_t a={.magic=MAGIC('T','E','X',0),.serial=1,.pool=&pool,.mem=storage,.L={.size=4096},
             .d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=64,.height=64,.depth=1,.levels=1}};
    tex_t b=a;b.serial=2;b.off=4096;b.mem=storage+4096;
    akey key={.pool=(uintptr_t)&pool,.d=a.d};
    vkimg_t *owner=rs_fresh_owner(&a,vk_fmt(a.d.format),48,48,(uintptr_t)a.mem);
    vk_rs_variant *old=owner->rs_backing;
    nvn_hm_put(&g_by_key,&key,owner);
    assert(nx_rs_active_update(&g_rs_dormant,(uintptr_t)owner,1));
    const tex_t *targets[NX_RS_SCOPE_OWNER_MAX]={[8]=&a,[10]=&b};
    b.d.flags=NVN_TEXTURE_FLAGS_IMAGE;
    assert(!vk_rs_allocate_fresh_group(targets,NX_RS_SCOPE_OWNER_MAX,32,32));
    assert(created==1 && !published && !freed);
    b.d.flags=0;
    fail_at=3;
    assert(!vk_rs_allocate_fresh_group(targets,NX_RS_SCOPE_OWNER_MAX,32,32));
    assert(freed==1 && !published && !g_rs_live && !retired && g_by_key.n==1);
    assert(g_rs_dormant.n==1 && owner->rs_backing==old && old->physical_w==48);
    fail_at=0;
    assert(vk_rs_allocate_fresh_group(targets,NX_RS_SCOPE_OWNER_MAX,32,32));
    assert(published==1 && !g_rs_dormant.n && g_rs_live->n==2 && g_by_key.n==2);
    assert(owner->rs_backing->physical_w==32 && retired==old && freed==1);
    for (unsigned i=0;i<g_rs_live->n;i++) {
        vkimg_t *im=(vkimg_t *)g_rs_live->owner[i];
        vk_rs_variant_destroy(im->rs_backing);free(im);
    }
    vk_rs_variant_destroy(retired);
    free(g_rs_live);free(g_by_key.keys);free(g_by_key.vals);
}
"""
        with tempfile.TemporaryDirectory() as d:
            c=Path(d)/"fresh_group.c";exe=Path(d)/"fresh_group"
            c.write_text(code)
            nvn=ROOT/"nxnative/dm/gfx/nvn"
            subprocess.run([cc,"-std=c11","-Wall","-Wextra","-Werror","-pthread","-I",str(nvn),
                            "-I",str(nvn/"include"),"-I",str(ROOT/"nxnative/dm/gfx/tex"),
                            str(c),"-o",str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
