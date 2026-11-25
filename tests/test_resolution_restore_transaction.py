"""Host-side fault injection for the real restore-all transaction body.

Vulkan allocation/recording and publication are mocked; this is not GPU validation.
"""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STUB = r"""
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#define NX_RS_ACTIVE_MAX 64
#define VK_IMAGE_LAYOUT_GENERAL 1
typedef int VkCommandBuffer;
typedef struct { int image,memory,layout,format,aspect,native_only; unsigned logical_w,logical_h,physical_w,physical_h; } vk_rs_variant;
typedef struct { int img,mem,sub,full,layout,fmt,aspect; unsigned w,h; vk_rs_variant *rs_backing; } vkimg_t;
typedef struct { unsigned n; uintptr_t owner[NX_RS_ACTIVE_MAX]; } active;
static active *g_rs_live;
static unsigned allocs,destroys,copies,publishes,barriers,index_updates,fail_alloc,fail_copy;
static int rs_is_scaled(const vkimg_t *im) {return im && im->rs_backing && (im->rs_backing->physical_w!=im->w || im->rs_backing->physical_h!=im->h);}
static vk_rs_variant *vk_rs_variant_create(vkimg_t *im,unsigned w,unsigned h) {if(++allocs==fail_alloc)return NULL; vk_rs_variant *v=malloc(sizeof *v); assert(v); *v=*im->rs_backing; v->physical_w=w;v->physical_h=h;return v;}
static void vk_rs_variant_destroy(vk_rs_variant *v){if(v){destroys++;free(v);}}
static int rs_can_copy(const vk_rs_variant *a,const vk_rs_variant *b){return a&&b&&allocs!=fail_copy;}
static VkCommandBuffer vk_cmd(void){return 1;}
static void vk_img_barrier_all(VkCommandBuffer cb){assert(cb);barriers++;}
static void rs_copy(VkCommandBuffer cb,const vk_rs_variant *a,vk_rs_variant *b){assert(cb&&a&&b);copies++;}
static void rs_publish_backing(vkimg_t *im,vk_rs_variant *v){assert(v->native_only);im->rs_backing=v;publishes++;}
static void rs_active_publish(void){index_updates++;}
"""
MAIN = r"""
int main(void) {
    vk_rs_variant a={.image=1,.memory=2,.layout=1,.format=1,.aspect=1,.logical_w=800,.logical_h=600,.physical_w=400,.physical_h=300};
    vk_rs_variant b={.image=3,.memory=4,.layout=1,.format=1,.aspect=1,.logical_w=640,.logical_h=480,.physical_w=320,.physical_h=240};
    vkimg_t x={.img=1,.mem=2,.layout=1,.fmt=1,.aspect=1,.w=800,.h=600,.rs_backing=&a};
    vkimg_t y={.img=3,.mem=4,.layout=1,.fmt=1,.aspect=1,.w=640,.h=480,.rs_backing=&b};
    assert(rs_restore_all_locked() && !allocs && !barriers);
    g_rs_live=calloc(1,sizeof *g_rs_live); assert(g_rs_live);
    g_rs_live->n=2;g_rs_live->owner[0]=(uintptr_t)&x;g_rs_live->owner[1]=(uintptr_t)&y;
    fail_alloc=2;assert(!rs_restore_all_locked());
    assert(allocs==2&&destroys==1&&!copies&&!publishes&&!barriers&&!index_updates);
    assert(g_rs_live->n==2&&x.rs_backing==&a&&y.rs_backing==&b);
    allocs=destroys=fail_alloc=0;fail_copy=2;assert(!rs_restore_all_locked());
    assert(allocs==2&&destroys==2&&!copies&&!publishes&&!barriers&&!index_updates);
    assert(g_rs_live->n==2&&x.rs_backing==&a&&y.rs_backing==&b);
    allocs=destroys=fail_copy=0;assert(rs_restore_all_locked());
    assert(allocs==2&&!destroys&&copies==2&&publishes==2&&barriers==2&&index_updates==1&&!g_rs_live);
    assert(!rs_is_scaled(&x)&&!rs_is_scaled(&y));free(x.rs_backing);free(y.rs_backing);
    return 0;
}
"""

class ResolutionRestoreTransactionTests(unittest.TestCase):
    def test_atomic_restore(self):
        cc = shutil.which("cc") or shutil.which("clang")
        if not cc:
            self.skipTest("C compiler required")
        source = (ROOT / "nxnative/dm/android/gfx_vk_tex.c").read_text(encoding="utf-8")
        start = source.index("static int rs_restore_all_locked(void) {")
        end = source.index("int vk_rs_cache_restore_all(void) {", start)
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "restore.c"
            exe = Path(tmp) / "restore"
            src.write_text(STUB + source[start:end] + MAIN, encoding="utf-8")
            subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", str(src), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

if __name__ == "__main__":
    unittest.main()
