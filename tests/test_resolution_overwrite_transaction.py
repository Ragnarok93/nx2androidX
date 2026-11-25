"""Exercise the real no-copy replacement transaction, with mocked Vulkan boundaries."""
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
typedef struct { int native_only; } vk_rs_variant;
typedef struct { unsigned w,h; vk_rs_variant *rs_backing; } vkimg_t;
typedef struct { uintptr_t owner[NX_RS_ACTIVE_MAX]; unsigned n; } nx_rs_active;
static nx_rs_active active;
static nx_rs_active *g_rs_live=&active;
static unsigned allocations,transitions,publishes,drops,fail_alloc;
static int rs_is_scaled(vkimg_t *im) { return im && im->rs_backing!=0; }
static vk_rs_variant *vk_rs_variant_create(vkimg_t *im,unsigned w,unsigned h) { assert(im && w==im->w && h==im->h); allocations++; return fail_alloc?NULL:calloc(1,sizeof(vk_rs_variant)); }
static void rs_prepare_overwrite(vk_rs_variant *v) { assert(v); transitions++; }
static void rs_publish_backing(vkimg_t *im,vk_rs_variant *v) { assert(v->native_only); im->rs_backing=v; publishes++; }
static void rs_active_drop(vkimg_t *im) { assert(g_rs_live->owner[0]==(uintptr_t)im); g_rs_live->n=0; drops++; }
"""
MAIN = r"""
int main(void) {
    vk_rs_variant old={0}; vkimg_t im={800,600,&old}, other={800,600,&old};
    assert(!rs_discard_native_locked(&im) && !allocations);
    active.n=1; active.owner[0]=(uintptr_t)&im;
    assert(!rs_discard_native_locked(&other) && !allocations);
    active.n=2; active.owner[1]=(uintptr_t)&im;
    assert(!rs_discard_native_locked(&im) && !allocations);
    active.n=1; fail_alloc=1;
    assert(!rs_discard_native_locked(&im) && allocations==1 && !transitions && !publishes && !drops && im.rs_backing==&old && active.n==1);
    fail_alloc=0;
    assert(rs_discard_native_locked(&im) && allocations==2 && transitions==1 && publishes==1 && drops==1 && active.n==0 && im.rs_backing!=&old);
    free(im.rs_backing);
}
"""

class ResolutionOverwriteTransactionTests(unittest.TestCase):
    def test_full_overwrite_replaces_without_copy_and_rolls_back(self):
        cc = shutil.which('cc') or shutil.which('clang')
        if not cc:
            self.skipTest('C compiler required')
        source = (ROOT / 'nxnative/dm/android/gfx_vk_tex.c').read_text()
        start = source.index('static int rs_discard_native_locked(')
        end = source.index('static int rs_require_native_locked(', start)
        with tempfile.TemporaryDirectory() as tmp:
            c = Path(tmp) / 'check.c'
            exe = Path(tmp) / 'check'
            c.write_text(STUB + source[start:end] + MAIN)
            subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror', str(c), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
