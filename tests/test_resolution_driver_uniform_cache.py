"""Exercise the actual replay's immutable C2 uniform cache with a counted hash."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class DriverUniformCache(unittest.TestCase):
    def test_c2_revision_and_submission_scope(self):
        replay = (ROOT / 'nxnative/dm/android/gfx_vk_exec.c').read_text()
        def section(a, b):
            return replay[replay.index(a):replay.index(b, replay.index(a))]
        c2 = section('static uint64_t c2_handle(', '\n}') + '\n}\n'
        segment = section('static uint32_t g_dispatch[3]', '// vk_samp_by_id without')
        cache = section('static struct { uint64_t h, epoch; VkBuffer b;', 'uint64_t g_set_key_bypass;')
        if 'g_bind_gen[6]' not in cache:
            cache += '\nstatic uint64_t g_bind_gen[6];\n'
        fnv = section('static inline uint64_t fnv(', '// Static descriptor content only:')
        fnv = fnv.replace('const uint8_t *b = p;', '++hashes; const uint8_t *b = p;')
        shared = section('static void drv_shared(', '// A binding that runs past')
        code = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
typedef uintptr_t VkBuffer;
typedef uint64_t VkDeviceSize;
static struct { struct { struct { uint64_t minUniformBufferOffsetAlignment; } limits; } props; } V;
static struct {
    uint64_t tex[6][256], img[6][32], samp[6][128];
    struct { uint64_t addr,size; } ssbo[6][16],ubo[6][16];
} S;
static uint64_t g_set_epoch=1;
static int g_x_nodrvc,g_x_setverify;
static unsigned hashes,allocs;
static uint8_t scratch[32][4096];
static void *vk_scratch(size_t n,uint64_t align,VkBuffer *buf,VkDeviceSize *off) {
    (void)align; assert(n==4096 && allocs<32); *buf=1;*off=4096ull*allocs;
    return scratch[allocs++];
}
enum { DRVC=1024 };
''' + c2 + segment + cache + fnv + shared + r'''
static VkDeviceSize check(int st,int bank,int dyn) {
    VkBuffer buf; VkDeviceSize off;
    drv_shared(st,bank,&buf,&off,dyn);
    uint8_t expected[4096]; drv_segment(st,bank,expected,dyn);
    assert(buf==1 && off%4096==0 && !memcmp(scratch[off/4096],expected,sizeof expected));
    if(bank && !dyn) for(int p=0x20;p<0x968;p+=8) {
        uint64_t word;memcpy(&word,expected+p,8);assert(word==c2_handle(st,p));
    }
    return off;
}
int main(void) {
    for(int i=0;i<256;i++)S.tex[0][i]=i+0x100000;
    for(int i=0;i<32;i++)S.img[0][i]=i+1000;
    for(int i=0;i<128;i++)S.samp[0][i]=(uint64_t)(i+1)<<20;
    VkDeviceSize a=check(0,2,0);
    unsigned h=hashes;
    for(int i=0;i<100;i++)assert(check(0,2,0)==a);
    assert(hashes==h && allocs==1); // old path hashes every request
    ++g_bind_gen[0]; S.tex[0][129]=123;
    assert(check(0,2,0)!=a && hashes==h+1);
    a=check(0,2,1); h=hashes;
    assert(check(0,2,1)==a && hashes==h);
    check(1,2,0); assert(hashes==h+1); // no cross-stage memo
    ++g_set_epoch; assert(check(0,2,1)!=a);
    h=hashes;g_x_nodrvc=1; a=check(0,2,1);
    assert(check(0,2,1)!=a && hashes==h+2);g_x_nodrvc=0;
    h=hashes;g_x_setverify=1;check(0,2,1);check(0,2,1);
    assert(hashes==h+2);g_x_setverify=0;
    g_dispatch[0]=10;a=check(5,0,0);g_dispatch[0]=20;
    assert(check(5,0,0)!=a); // mutable dispatch C0 never memoized
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / 'test.c'; p.write_text(code)
            exe = p.with_suffix('')
            subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror',str(p),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
