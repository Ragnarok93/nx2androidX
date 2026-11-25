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
    assert(nx_rs_scope_switch(&s,1));
    assert(nx_rs_scope_switch(&s,0));
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

    def test_actual_replay_scan(self):
        cc=shutil.which("cc") or shutil.which("clang")
        if not cc:self.skipTest("C compiler required")
        source=(ROOT/"nxnative/dm/android/gfx_vk_resolution_scope.inc").read_text()
        def part(a,b):return source[source.index(a):source.index(b,source.index(a))]
        code=r"""
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "nvn_obj.h"
#include "gfx_vk_resolution_scope.h"
#include "gfx_vk_resolution_preflight.h"
static struct {
    p_scissor sc; int sc_set,rast_discard;
    p_bind_buf ubo[6][16]; NVNbufferRange vbuf[16];
    uint64_t tex[6][256];
} S;
typedef struct { const tex_t *target[9]; uint64_t addr[9]; unsigned mask; uint32_t w,h; } rs_scope_group;
typedef struct {int depth,stencil;} vkfmt_t;
static const vkfmt_t *vk_fmt(int fmt) {static const vkfmt_t f={1,1};(void)fmt;return &f;}
static int rs_scope_raw(const rs_scope_group *g,uint64_t a,uint64_t n,int tail) {
    (void)tail;
    return a && n && !nx_rs_range_overlap(a,n,g->addr[0],g->target[0]->L.size);
}
static int rs_scope_program_proof(__typeof__(S) *s,const uint8_t *p,const p_bind_program *b) {(void)s;(void)p;(void)b;return 1;}
/* Isolate the real command walker and range matching from shader/Vulkan
 * proof dependencies. This stub makes the native consumer sample only
 * when its test texture binding is set. */
static int rs_scope_draw_proof(const rs_scope_group *g,const __typeof__(S) *s,int scaled,int *sampled) {
    (void)g;*sampled=s->tex[1][0]==7;return !s->rast_discard && (!scaled || !*sampled);
}
"""
        code+=part("static uint64_t rs_scope_addr(","static int rs_scope_raw(")
        code+=part("static int rs_scope_full_scissor(","static void rs_scope_try_begin(")
        code+=r"""
static uint8_t storage[16384];
static pool_t pool={.mem=storage,.size=sizeof storage};
static tex_t t[3];
static _Alignas(8) uint8_t commands[16384];
static size_t at;
static uint8_t *append(unsigned op,const void *payload,size_t n) {
    size_t len=(n+15)&~(size_t)7;
    assert(at+len<=sizeof commands);
    uint8_t *p=commands+at;memset(p,0,len);
    op_hdr h={op,(uint32_t)len};memcpy(p,&h,8);
    if(n)memcpy(p+8,payload,n);
    at+=len;return p;
}
static void reset(void) {
    at=0;memset(&S,0,sizeof S);
    for(unsigned i=0;i<3;i++)t[i]=(tex_t){.magic=MAGIC('T','E','X',0),.serial=i+1,.pool=&pool,.mem=storage+4096*i,.off=4096*i,.L={.size=4096},.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=64,.height=64,.depth=1,.levels=1}};
}
static void rt(tex_t *target) {p_set_rt r={.n=1,.color={target}};append(OP_SET_RT,&r,sizeof r);}
static void clear(unsigned mask) {p_clear_color c={.index=0,.mask=mask};append(OP_CLEAR_COLOR,&c,sizeof c);}
static void draw(void) {p_draw d={.count=3,.instances=1};append(OP_DRAW,&d,sizeof d);}
static void bind(unsigned id,int deferred) {p_bind_handles b={.stage=1,.first=0,.n=1,.defer=deferred,.h={id}};append(OP_BIND_TEX,&b,offsetof(p_bind_handles,h)+8);}
static uint8_t *discard(void) {p_rs_discard d={.index=0};return append(OP_RS_DISCARD,&d,sizeof d);}
static const uint8_t *scan(void) {rs_scope_group g={.target={&t[0]},.addr={(uintptr_t)t[0].mem},.mask=1,.w=64,.h=64};append(OP_END,NULL,0);return rs_scope_scan(commands,&g);}
static void producer(void) {clear(15);draw();}
static void consumer(void) {rt(&t[1]);bind(7,0);draw();}
int main(void) {
    reset();clear(15);rt(&t[0]);draw();consumer();rt(&t[2]);draw();rt(&t[0]);uint8_t *end=discard();assert(scan()==end);
    reset();producer();consumer();rt(&t[0]);end=discard();assert(scan()==end);
    reset();clear(7);draw();consumer();rt(&t[0]);discard();assert(!scan());
    reset();p_scissor sc={0,0,32,64};append(OP_SCISSOR,&sc,sizeof sc);producer();consumer();rt(&t[0]);discard();assert(!scan());
    reset();producer();consumer();rt(&t[0]);assert(!scan());
    reset();producer();rt(&t[1]);draw();rt(&t[0]);discard();assert(!scan());
    reset();producer();rt(&t[1]);bind(7,1);draw();rt(&t[0]);discard();assert(!scan());
    reset();producer();append(OP_DISPATCH,NULL,0);consumer();rt(&t[0]);discard();assert(!scan());
    reset();producer();t[1].off=128;t[1].mem=storage+128;consumer();rt(&t[0]);discard();assert(!scan());
    reset();producer();consumer();rt(&t[0]);draw();discard();assert(!scan());
    reset();producer();consumer();rt(&t[0]);append(OP_RS_DISCARD,NULL,0);assert(!scan());
    reset();producer();consumer();rt(&t[0]);end=discard();assert(scan()==end);
}
"""
        with tempfile.TemporaryDirectory() as d:
            c=Path(d)/"scan.c";exe=Path(d)/"scan"
            c.write_text(code)
            inc=ROOT/"nxnative/dm/gfx/nvn"
            subprocess.run([cc,"-std=c11","-Wall","-Wextra","-Werror","-Wno-misleading-indentation","-I",str(inc),"-I",str(inc/"include"),"-I",str(ROOT/"nxnative/dm/gfx/tex"),str(c),"-o",str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
