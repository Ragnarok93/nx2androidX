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
    assert(nx_rs_scope_init(&s,1));assert(!nx_rs_scope_dispatch(&s,1,1));
    assert(nx_rs_scope_clear(&s,1,1) && nx_rs_scope_draw(&s,1,0));
    assert(nx_rs_scope_dispatch(&s,1,0) && s.phase==0 && !nx_rs_scope_dispatch(&s,0,1));
    assert(nx_rs_scope_dispatch(&s,1,1) && !nx_rs_scope_draw(&s,1,0));
    assert(nx_rs_scope_discard(&s,1)==2);
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
        self.assertIn("vk_rs_suspend_discarded_group()",scope)
        self.assertIn("g_rs_scope_end==p && g_rs_scope_overwrite",replay)
        self.assertIn("rs_scope_close();\n            rs_scope_try_begin(p);",replay)

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
static const uint64_t *g_rs_scope_rest;
static unsigned g_rs_scope_rest_n,g_rs_scope_crossed;
static int g_rs_scope_overwrite;
static struct {
    p_scissor sc; int sc_set,rast_discard;
    p_bind_buf ubo[6][16],ssbo[6][16]; NVNbufferRange vbuf[16];
    uint64_t tex[6][256],img[6][32];
} S;
typedef struct { const tex_t *target[9]; uint64_t addr[9]; unsigned mask; uint32_t w,h; } rs_scope_group;
typedef struct {int depth,stencil;} vkfmt_t;
static const vkfmt_t *vk_fmt(int fmt) {static const vkfmt_t f={1,1};(void)fmt;return &f;}
static int rs_scope_raw(const rs_scope_group *g,uint64_t a,uint64_t n,int tail) {
    uint64_t lo=g->addr[0],hi=lo+16384;
    if (a<lo || a>=hi) return 0;
    if (tail) n=hi-a;
    return n && n<=hi-a && !nx_rs_range_overlap(a,n,lo,g->target[0]->L.size);
}
static int rs_scope_program_proof(__typeof__(S) *s,const uint8_t *p,const p_bind_program *b) {(void)s;(void)p;(void)b;return 1;}
/* Isolate the real command walker and range matching from shader/Vulkan
 * proof dependencies. This stub makes the native consumer sample only
 * when its test texture binding is set. */
static int rs_scope_draw_proof(const rs_scope_group *g,const __typeof__(S) *s,int scaled,int *sampled) {
    (void)g;*sampled=s->tex[1][0]==7;return !s->rast_discard && (!scaled || !*sampled);
}
static int rs_scope_compute_proof(const rs_scope_group *g,const __typeof__(S) *s,const p_dispatch *d,int *sampled) {
    *sampled=s->tex[5][0]==7;
    const p_bind_buf *b=&s->ssbo[5][0];
    return (d->ind?(!(d->ind&3) && rs_scope_raw(g,d->ind,12,0)):(d->x>0 && d->y>0 && d->z>0)) && s->img[5][0]==8 &&
        (!b->addr || rs_scope_raw(g,b->addr,b->size,0));
}
"""
        code+=part("enum { RS_SCOPE_PROGRAM_MAX=", "typedef struct { const uint8_t *record;")
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
    at=0;memset(&S,0,sizeof S);g_rs_scope_rest=NULL;g_rs_scope_rest_n=g_rs_scope_crossed=0;
    for(unsigned i=0;i<3;i++)t[i]=(tex_t){.magic=MAGIC('T','E','X',0),.serial=i+1,.pool=&pool,.mem=storage+4096*i,.off=4096*i,.L={.size=4096},.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=64,.height=64,.depth=1,.levels=1}};
}
static void rt(tex_t *target) {p_set_rt r={.n=1,.color={target}};append(OP_SET_RT,&r,sizeof r);}
static void clear(unsigned mask) {p_clear_color c={.index=0,.mask=mask};append(OP_CLEAR_COLOR,&c,sizeof c);}
static void draw(void) {p_draw d={.count=3,.instances=1};append(OP_DRAW,&d,sizeof d);}
static void bind(unsigned id,int deferred) {p_bind_handles b={.stage=1,.first=0,.n=1,.defer=deferred,.h={id}};append(OP_BIND_TEX,&b,offsetof(p_bind_handles,h)+8);}
static void compute(int aliased,uint64_t indirect) {p_bind_handles b={.stage=5,.first=0,.n=1,.h={7}};append(OP_BIND_TEX,&b,offsetof(p_bind_handles,h)+8);b.h[0]=aliased?7:8;append(OP_BIND_IMG,&b,offsetof(p_bind_handles,h)+8);p_dispatch d={.x=8,.y=8,.z=1,.ind=indirect};append(OP_DISPATCH,&d,sizeof d);}
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
    reset();producer();compute(0,0);end=discard();assert(scan()==end);
    reset();clear(15);
    uint64_t update[]={ (uintptr_t)(storage+8192),8,42 };
    append(OP_UPDATE_UB,update,sizeof update);
    p_clear_buf cb={.addr=(uintptr_t)(storage+8192),.size=16,.value=0};
    append(OP_CLEAR_BUF,&cb,sizeof cb);
    uint64_t copy[]={ (uintptr_t)(storage+8192),(uintptr_t)(storage+12288),16 };
    append(OP_COPY_B2B,copy,sizeof copy);
    draw();consumer();rt(&t[0]);end=discard();assert(scan()==end);
    reset();producer();update[0]=(uintptr_t)storage+4092;append(OP_UPDATE_UB,update,sizeof update);
    consumer();rt(&t[0]);discard();assert(!scan());
    reset();producer();update[0]=(uintptr_t)storage+8192;update[1]=16;append(OP_UPDATE_UB,update,sizeof update);
    consumer();rt(&t[0]);discard();assert(!scan());
    reset();producer();cb.addr=(uintptr_t)storage;append(OP_CLEAR_BUF,&cb,sizeof cb);
    consumer();rt(&t[0]);discard();assert(!scan());
    reset();producer();copy[0]=(uintptr_t)storage;append(OP_COPY_B2B,copy,sizeof copy);
    consumer();rt(&t[0]);discard();assert(!scan());
    reset();producer();copy[0]=(uintptr_t)storage+8192;copy[1]=(uintptr_t)storage;append(OP_COPY_B2B,copy,sizeof copy);
    consumer();rt(&t[0]);discard();assert(!scan());
    reset();producer();p_bind_buf sb={.stage=5,.index=0,.addr=(uintptr_t)storage+8192,.size=16};
    append(OP_BIND_SSBO,&sb,sizeof sb);compute(0,0);end=discard();assert(scan()==end);
    reset();producer();sb.addr=(uintptr_t)storage;append(OP_BIND_SSBO,&sb,sizeof sb);
    compute(0,0);discard();assert(!scan());
    reset();producer();compute(1,0);discard();assert(!scan());
    reset();producer();compute(0,1);discard();assert(!scan());
    reset();producer();compute(0,(uintptr_t)(storage+8192));end=discard();assert(scan()==end);
    reset();producer();compute(0,(uintptr_t)storage);discard();assert(!scan());
    reset();clear(15);p_draw indirect={.ind=(uintptr_t)(storage+8192)};
    append(OP_DRAW,&indirect,sizeof indirect);consumer();rt(&t[0]);end=discard();assert(scan()==end);
    reset();clear(15);indirect.ind=(uintptr_t)storage;append(OP_DRAW,&indirect,sizeof indirect);
    consumer();rt(&t[0]);discard();assert(!scan());
    reset();clear(15);p_draw_idx ix={.itype=1,.ibuf=(uintptr_t)(storage+4096),.ind=(uintptr_t)(storage+8192),.mdi_cnt=(uintptr_t)(storage+12288),.mdi_max=2,.mdi_stride=20};
    append(OP_DRAW_IDX,&ix,sizeof ix);ix.mdi_i=1;ix.ind+=20;append(OP_DRAW_IDX,&ix,sizeof ix);
    consumer();rt(&t[0]);end=discard();assert(scan()==end);
    reset();clear(15);ix.mdi_i=0;ix.ibuf=(uintptr_t)storage;append(OP_DRAW_IDX,&ix,sizeof ix);
    consumer();rt(&t[0]);discard();assert(!scan());
    reset();producer();compute(0,0);draw();discard();assert(!scan());
    reset();producer();compute(0,0);uint8_t *overwrite=commands+at;clear(15);discard();assert(scan()==overwrite && g_rs_scope_overwrite);
    reset();producer();t[1].off=128;t[1].mem=storage+128;consumer();rt(&t[0]);discard();assert(!scan());
    reset();producer();consumer();rt(&t[0]);draw();discard();assert(!scan());
    reset();producer();consumer();rt(&t[0]);append(OP_RS_DISCARD,NULL,0);assert(!scan());
    reset();producer();consumer();rt(&t[0]);end=discard();assert(scan()==end && !g_rs_scope_overwrite);
    reset();producer();consumer();rt(&t[0]);overwrite=commands+at;clear(15);
    assert(scan()==overwrite && g_rs_scope_overwrite);
    reset();producer();consumer();rt(&t[0]);clear(7);assert(!scan());
    // The terminal overwrite proof must cover every member and reject
    // intervening draws and partial depth/stencil clears.
    reset();rs_scope_group two={.target={&t[0],NULL,NULL,NULL,NULL,NULL,NULL,NULL,&t[1]},.mask=257,.w=64,.h=64};
    clear(15);p_clear_ds depth={.depth_mask=1,.stencil_mask=255};append(OP_CLEAR_DS,&depth,sizeof depth);
    assert(rs_scope_overwrite_run(commands,&two,&S,9,at));
    assert(!rs_scope_overwrite_run(commands,&two,&S,1,at));
    reset();clear(15);draw();append(OP_CLEAR_DS,&depth,sizeof depth);assert(!rs_scope_overwrite_run(commands,&two,&S,9,at));
    reset();clear(15);depth.stencil_mask=127;append(OP_CLEAR_DS,&depth,sizeof depth);assert(!rs_scope_overwrite_run(commands,&two,&S,9,at));
    // A lifetime can cross immutable handle boundaries, including between
    // producer and consumer and before the terminal discard.
    reset();producer();append(OP_END,NULL,0);uint8_t *h2=commands+at;
    consumer();append(OP_END,NULL,0);uint8_t *h3=commands+at;rt(&t[0]);end=discard();
    uint64_t rest[]={ (uintptr_t)h2,(uintptr_t)h3 };
    g_rs_scope_rest=rest;g_rs_scope_rest_n=2;
    assert(scan()==end && g_rs_scope_crossed==2);
    g_rs_scope_rest_n=1;assert(!rs_scope_scan(commands,&(rs_scope_group){.target={&t[0]},.addr={(uintptr_t)t[0].mem},.mask=1,.w=64,.h=64}));
}
"""
        with tempfile.TemporaryDirectory() as d:
            c=Path(d)/"scan.c";exe=Path(d)/"scan"
            c.write_text(code)
            inc=ROOT/"nxnative/dm/gfx/nvn"
            subprocess.run([cc,"-std=c11","-Wall","-Wextra","-Werror","-Wno-misleading-indentation","-I",str(inc),"-I",str(inc/"include"),"-I",str(ROOT/"nxnative/dm/gfx/tex"),str(c),"-o",str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_scope_does_not_require_diagnostic_graph(self):
        base=ROOT/"nxnative/dm/android"
        replay=(base/"gfx_vk_exec.c").read_text()
        header=(base/"gfx_vk.h").read_text()
        tex=(base/"gfx_vk_tex.c").read_text()
        self.assertIn("g_rs_scopes=g_rs_replay && scopes",replay)
        self.assertIn("g_rs_scopes?VK_RS_TRACK_PROVENANCE:0",replay)
        self.assertIn("&VK_RS_TRACK_GRAPH",header)
        self.assertIn("if (vk_rs_provenance()) im->rs_guest_addr",tex)
        self.assertIn("!vk_rs_provenance() || !w || !h",tex)
        self.assertIn("g_rs_frame.target<100",replay)
