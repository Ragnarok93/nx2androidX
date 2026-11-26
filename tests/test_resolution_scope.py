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
    assert(!nx_rs_scope_init(&s, 1, 0));
    assert(nx_rs_scope_init_compute(&s,1));
    assert(!nx_rs_scope_sample(&s,1));
    assert(!nx_rs_scope_compute_write(&s,0));
    assert(!nx_rs_scope_compute_write(&s,2));
    assert(nx_rs_scope_compute_write(&s,1));
    assert(s.initialized==1 && s.drawn==1 && !s.bound);
    assert(nx_rs_scope_sample(&s,1));
    s.exported=1;assert(!nx_rs_scope_compute_write(&s,1));
    assert(nx_rs_scope_discard(&s,1)==2);
    assert(!nx_rs_scope_compute_write(&s,1));
    assert(!nx_rs_scope_init_compute(&s,0));
    assert(!nx_rs_scope_init_compute(&s,1u<<16));
    assert(nx_rs_scope_init(&s,3,1));
    assert(!nx_rs_scope_compute_write(&s,3));
    assert(nx_rs_scope_compute_write(&s,2));
    assert(nx_rs_scope_sample(&s,2));
    assert(!nx_rs_scope_init(&s, 1u<<16, 1u<<16));
    assert(nx_rs_scope_init(&s, 259, 256));
    assert(!nx_rs_scope_draw(&s, 0));
    assert(nx_rs_scope_clear(&s, 256));
    assert(nx_rs_scope_draw(&s, 0));
    s.bound = 257;
    assert(!nx_rs_scope_draw(&s, 0));
    assert(nx_rs_scope_clear(&s, 1));
    assert(!nx_rs_scope_draw(&s, 1));
    assert(nx_rs_scope_draw(&s, 0));
    s.bound = 258;
    assert(nx_rs_scope_clear(&s, 2));
    assert(nx_rs_scope_draw(&s, 1));
    assert(nx_rs_scope_discard(&s, 256) == 1);
    assert(!nx_rs_scope_sample(&s, 256));
    s.bound = 0;
    assert(nx_rs_scope_draw(&s, 2));
    assert(nx_rs_scope_discard(&s, 1) == 1);
    assert(nx_rs_scope_discard(&s, 2) == 2);
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
        self.assertLess(scope.index("nx_rs_raster_init(&raster,"),scope.index("vk_rs_allocate_fresh_group(group.target"))
        self.assertLess(scope.index("rs_scope_scan(next,&group,&reason)"),scope.index("vk_rs_allocate_fresh_group(group.target"))
        self.assertIn("!nvn_replaying_job()",scope)
        self.assertIn("vk_rs_scope_freeze_seal();g_rs_scope_end=end",scope)
        self.assertIn("vk_rs_suspend_discarded_group()",scope)
        self.assertIn("g_rs_scope_end==p && g_rs_scope_overwrite",replay)
        clear_start = replay.index("case OP_CLEAR_COLOR: case OP_CLEAR_DS:")
        clear_dispatch = replay[clear_start:replay.index("case OP_VIEWPORTS:", clear_start)]
        self.assertIn("!g_rs_scope_end && S.pass_open!=2", clear_dispatch)
        self.assertLess(scope.index("rs_scope_scan(next,&group,&reason)"),scope.index("if (end && S.pass_open) vk_end_pass();"))
        self.assertLess(scope.index("if (end && S.pass_open) vk_end_pass();"),scope.index("vk_rs_allocate_fresh_group(group.target"))
        self.assertLess(clear_dispatch.index("rs_scope_try_begin(p)"), clear_dispatch.index("clear_color("))
        self.assertLess(clear_dispatch.index("rs_scope_try_begin(p)"), clear_dispatch.index("clear_ds("))

    def test_synchronous_replay_copies_flat_handles(self):
        cc = shutil.which("cc") or shutil.which("clang")
        if not cc:
            self.skipTest("C compiler required")
        source = (ROOT / "nxnative/dm/gfx/nvn/hle_nvn.c").read_text()
        def part(start, end):
            offset = source.index(start)
            return source[offset:source.index(end, offset)]
        code = """
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "nvn_obj.h"
#define LOG(...) ((void)0)
"""
        code += part("static size_t rec_flat(", "static uint64_t g_flat_bytes")
        code += part("static void resolution_snapshot_free(", "#endif")
        code += r"""
int main(void) {
    struct { op_hdr draw, end; } leaf = {{OP_DRAW, 8}, {OP_END, 8}};
    struct { op_hdr call; uint64_t target; op_hdr end; } root =
        {{OP_CALL, 16}, (uintptr_t)&leaf, {OP_END, 8}};
    uint64_t handles[] = {(uintptr_t)&root, (uintptr_t)&leaf};
    uint64_t *snapshot = resolution_snapshot(handles, 2);
    assert(snapshot && snapshot[0] != handles[0] && snapshot[1] != handles[1]);
    leaf.draw.op = OP_BARRIER;
    for (unsigned i = 0; i < 2; i++) {
        const op_hdr *flat = (const op_hdr *)(uintptr_t)snapshot[i];
        assert(flat[0].op == OP_DRAW && flat[1].op == OP_END);
    }
    resolution_snapshot_free(snapshot, 2);
    assert(!resolution_snapshot(handles, 257));
    assert(rec_flat((const uint8_t *)&root, NULL, 0, 1, 0, 1) == SIZE_MAX);
    leaf.draw.size = 4;
    assert(!resolution_snapshot(handles, 2));
}
"""
        with tempfile.TemporaryDirectory() as d:
            c, exe = Path(d) / "snapshot.c", Path(d) / "snapshot"
            c.write_text(code)
            subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT / "nxnative/dm/gfx/nvn"),
                            "-I", str(ROOT / "nxnative/dm/gfx/nvn/include"),
                            "-I", str(ROOT / "nxnative/dm/gfx/tex"), str(c),
                            "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

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
#include "gfx_vk_resolution_transfer.h"
#include "gfx_vk_resolution_view.h"
static int resolution_display_copy_format(const p_copy_t2t *c) {return nx_rs_display_copy_shape(c); }
static const uint64_t *g_rs_scope_rest;
static unsigned g_rs_scope_rest_n,g_rs_scope_crossed,g_rs_scope_program_count;
static int g_rs_scope_overwrite;
static struct {
    p_scissor sc; int sc_set,rast_discard;
    p_bind_buf ubo[6][16],ssbo[6][16]; NVNbufferRange vbuf[16];
    uint64_t tex[6][256],img[6][32];
} S;
typedef struct { const tex_t *target[NX_RS_SCOPE_OWNER_MAX]; uint64_t addr[NX_RS_SCOPE_OWNER_MAX]; unsigned mask; uint32_t w,h,pw,ph,ax,ay; } rs_scope_group;
typedef struct {int depth,stencil,compressed,dec,is_int;} vkfmt_t;
static const vkfmt_t *vk_fmt(int fmt) {static const vkfmt_t f={.depth=1,.stencil=1},color={0};return fmt==NVN_FORMAT_BGRA8?&color:&f;}
static uint8_t storage[4096*13];
static int rs_scope_raw(const rs_scope_group *g,uint64_t a,uint64_t n,int tail) {
    uint64_t lo=(uintptr_t)storage,hi=lo+sizeof storage;
    if (a<lo || a>=hi) return 0;
    if (tail) n=hi-a;
    if (!n || n>hi-a) return 0;
    for (unsigned i=0;i<NX_RS_SCOPE_OWNER_MAX;i++) if (g->target[i] && nx_rs_range_overlap(a,n,g->addr[i],g->target[i]->L.size)) return 0;
    return 1;
}
static int rs_scope_program_proof(__typeof__(S) *s,const uint8_t *p,const p_bind_program *b) {(void)s;(void)p;(void)b;return 1;}
/* Isolate the real command walker and range matching from shader/Vulkan
 * proof dependencies. This stub makes the native consumer sample only
 * when its test texture binding is set. */
static int rs_scope_draw_proof(const rs_scope_group *g,const __typeof__(S) *s,int scaled,unsigned *sampled) {
    (void)g;(void)scaled;
    *sampled=s->tex[1][0]==7?1:s->tex[1][0]==9?2:0;
    return !s->rast_discard;
}
static int rs_scope_compute_proof(const rs_scope_group *g,const __typeof__(S) *s,const p_dispatch *d,unsigned *sampled,unsigned *writes) {
    *writes=0;
    // Isolate the scanner from the shader bytecode proof: image 10 denotes
    // the fixture's single full-output compute producer.
    if (s->img[5][0]==10) {*sampled=0;*writes=1;return !d->ind && d->x==8 && d->y==8 && d->z==1;}
    *sampled=s->tex[5][0]==7;
    const p_bind_buf *b=&s->ssbo[5][0];
    return (d->ind?(!(d->ind&3) && rs_scope_raw(g,d->ind,12,0)):(d->x>0 && d->y>0 && d->z>0)) && s->img[5][0]==8 &&
        (!b->addr || rs_scope_raw(g,b->addr,b->size,0));
}
"""
        code+=part("enum { RS_SCOPE_PROGRAM_MAX=", "typedef struct { const uint8_t *record;")
        code+=part("static uint64_t rs_scope_addr(","static int rs_scope_raw(")
        code+=part("static int rs_scope_full_scissor(","static void rs_scope_reject(")
        code+=r"""
static pool_t pool={.mem=storage,.size=sizeof storage,.flags=NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS};
static tex_t t[13];
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
    for(unsigned i=0;i<13;i++)t[i]=(tex_t){.magic=MAGIC('T','E','X',0),.serial=i+1,.pool=&pool,.mem=storage+4096*i,.off=4096*i,.L={.size=4096},.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=64,.height=64,.depth=1,.levels=1}};
}
static void rt_ds(tex_t *color,tex_t *depth) {
    p_set_rt r={.n=color?1:0,.color={color},.depth=depth};
    append(OP_SET_RT,&r,sizeof r);
}
static void rt(tex_t *target) {rt_ds(target,NULL);}
static void clear_depth(void) {
    p_clear_ds d={.depth_mask=1,.stencil_mask=255};
    append(OP_CLEAR_DS,&d,sizeof d);
}
static void clear_slot(unsigned slot,unsigned mask) {
    p_clear_color c={.index=slot,.mask=mask};append(OP_CLEAR_COLOR,&c,sizeof c);
}
static void clear(unsigned mask) {clear_slot(0,mask);}
static void mrt(unsigned first,unsigned n) {
    p_set_rt r={.n=n,.depth=&t[0]};
    for(unsigned i=0;i<n;i++)r.color[i]=&t[first+i];
    append(OP_SET_RT,&r,sizeof r);
}
static void draw(void) {p_draw d={.count=3,.instances=1};append(OP_DRAW,&d,sizeof d);}
static void bind(unsigned id,int deferred) {p_bind_handles b={.stage=1,.first=0,.n=1,.defer=deferred,.h={id}};append(OP_BIND_TEX,&b,offsetof(p_bind_handles,h)+8);}
static void compute(int aliased,uint64_t indirect) {p_bind_handles b={.stage=5,.first=0,.n=1,.h={7}};append(OP_BIND_TEX,&b,offsetof(p_bind_handles,h)+8);b.h[0]=aliased?7:8;append(OP_BIND_IMG,&b,offsetof(p_bind_handles,h)+8);p_dispatch d={.x=8,.y=8,.z=1,.ind=indirect};append(OP_DISPATCH,&d,sizeof d);}
static uint8_t *discard_slot(int slot) {p_rs_discard d={.index=slot};return append(OP_RS_DISCARD,&d,sizeof d);}
static uint8_t *discard(void) {return discard_slot(0);}
static const char *reason;
static const uint8_t *scan_depth(void) {
    rs_scope_group g={.target={[8]=&t[0]},.addr={[8]=(uintptr_t)t[0].mem},.mask=256,.w=64,.h=64};
    append(OP_END,NULL,0);
    return rs_scope_scan(commands,&g,&reason);
}
static uint8_t *finish_depth_scope(void) {
    rt(&t[3]);bind(9,0);draw();
    rt_ds(&t[2],&t[0]);discard();
    rt_ds(&t[1],&t[0]);discard();
    return discard_slot(-1);
}
static const uint8_t *scan_from(const uint8_t *first) {rs_scope_group g={.target={&t[0]},.addr={(uintptr_t)t[0].mem},.mask=1,.w=64,.h=64};append(OP_END,NULL,0);return rs_scope_scan(first,&g,&reason);}
static const uint8_t *scan(void) {return scan_from(commands);}
static void producer(void) {clear(15);draw();}
static void consumer(void) {rt(&t[1]);bind(7,0);draw();}
static void consumer_two(void) {rt(&t[2]);bind(7,0);draw();}
int main(void) {
    reset();clear(15);rt(&t[0]);draw();consumer();rt(&t[2]);draw();rt(&t[0]);uint8_t *end=discard();assert(scan()==end);
    reset();producer();consumer();rt(&t[0]);end=discard();assert(scan()==end);
    reset();
    rs_scope_group owner_group={.target={&t[0]},.addr={(uintptr_t)t[0].mem},.mask=1,.w=64,.h=64};
    tex_t wrapper=t[0];wrapper.d.flags|=NVN_TEXTURE_FLAGS_COMPRESSIBLE;
    assert(rs_scope_match(&owner_group,&wrapper,NULL)==1);
    wrapper.L.tileH++;assert(rs_scope_match(&owner_group,&wrapper,NULL)<0);
    wrapper=t[0];wrapper.d.flags|=NVN_TEXTURE_FLAGS_IMAGE;
    assert(rs_scope_match(&owner_group,&wrapper,NULL)<0);
    t[0].d.flags=NVN_TEXTURE_FLAGS_IMAGE;wrapper=t[0];wrapper.d.flags=0;
    assert(rs_scope_match_access(&owner_group,&wrapper,NULL,1)==1);
    assert(rs_scope_match(&owner_group,&wrapper,NULL)<0);
    wrapper.L.tileH++;
    assert(rs_scope_match_access(&owner_group,&wrapper,NULL,1)<0);
    // An explicit selection of the sole mip/layer and identical format
    // must resolve to the same owner on a subsequent attachment bind.
    reset();producer();consumer();
    view_t same={.set_levels=1,.nlevels=1,.set_layers=1,.nlayers=1,
        .set_format=1,.format=NVN_FORMAT_RGBA8,.set_target=1,.target=NVN_TEXTURE_TARGET_2D};
    p_set_rt viewed={.n=1,.color={&t[0]},.has_cview={1},.cview={same}};
    append(OP_SET_RT,&viewed,sizeof viewed);end=discard();assert(scan()==end);
    reset();producer();consumer();viewed.cview[0].base_level=1;
    append(OP_SET_RT,&viewed,sizeof viewed);discard();assert(!scan());
    reset();producer();consumer();viewed.cview[0]=same;viewed.cview[0].format=NVN_FORMAT_RGBA8_SRGB;
    append(OP_SET_RT,&viewed,sizeof viewed);discard();assert(!scan());
    // Replay has retained the render targets. A prior command with no
    // attachment effect is outside the lifetime that starts at the clear.
    reset();append(OP_RESET_COUNTER,NULL,0);uint8_t *first_clear=commands+at;
    producer();consumer();rt(&t[0]);end=discard();
    assert(!scan() && scan_from(first_clear)==end);
    reset();clear(7);draw();consumer();rt(&t[0]);discard();assert(!scan());
    reset();p_scissor sc={0,0,32,64};append(OP_SCISSOR,&sc,sizeof sc);producer();consumer();rt(&t[0]);discard();assert(!scan());
    reset();producer();consumer();rt(&t[0]);assert(!scan());
    reset();producer();rt(&t[1]);draw();rt(&t[0]);discard();assert(!scan());
    reset();producer();rt(&t[1]);bind(7,1);draw();rt(&t[0]);discard();assert(!scan());
    reset();producer();append(OP_DISPATCH,NULL,0);consumer();rt(&t[0]);discard();assert(!scan());
    reset();producer();compute(0,0);end=discard();assert(scan()==end);
    reset();producer();t[1].d.flags=NVN_TEXTURE_FLAGS_DISPLAY;
    p_copy_t2t display={.src=&t[0],.dst=&t[1],.sr={0,0,0,64,64,1},.dr={0,0,0,64,64,1}};
    append(OP_COPY_T2T,&display,sizeof display);end=discard();assert(scan()==end);
    reset();producer();append(OP_COPY_T2T,&display,sizeof display);discard();assert(!scan());
    reset();producer();t[1].d.flags=NVN_TEXTURE_FLAGS_DISPLAY;display.sr.width=32;
    append(OP_COPY_T2T,&display,sizeof display);discard();assert(!scan());display.sr.width=64;
    reset();producer();display.src=&t[1];display.dst=&t[2];
    append(OP_COPY_T2T,&display,sizeof display);consumer();rt(&t[0]);end=discard();assert(scan()==end);
    reset();producer();display.dst=&t[0];append(OP_COPY_T2T,&display,sizeof display);
    consumer();rt(&t[0]);discard();assert(!scan());
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
    reset();producer();compute(0,0);draw();end=discard();assert(scan()==end);
    reset();producer();compute(0,0);uint8_t *overwrite=commands+at;clear(15);discard();assert(scan()==overwrite && g_rs_scope_overwrite);
    reset();producer();t[1].off=128;t[1].mem=storage+128;consumer();rt(&t[0]);discard();assert(!scan());
    reset();producer();consumer();rt(&t[0]);draw();discard();assert(!scan()); // bound feedback
    reset();producer();consumer();rt(&t[0]);append(OP_RS_DISCARD,NULL,0);assert(!scan());
    reset();producer();consumer();rt(&t[0]);end=discard();assert(scan()==end && !g_rs_scope_overwrite);
    reset();producer();consumer();rt(&t[0]);overwrite=commands+at;clear(15);
    assert(scan()==overwrite && g_rs_scope_overwrite);
    reset();producer();consumer();rt(&t[0]);clear(7);assert(!scan());
    // The terminal overwrite proof must cover every member and reject
    // intervening draws and partial depth/stencil clears.
    reset();rs_scope_group two={.target={&t[0],NULL,NULL,NULL,NULL,NULL,NULL,NULL,&t[1]},.mask=257,.w=64,.h=64};
    unsigned slots[9]={1,[8]=256};
    clear(15);p_clear_ds depth={.depth_mask=1,.stencil_mask=255};append(OP_CLEAR_DS,&depth,sizeof depth);
    assert(rs_scope_clear_run(commands,&two,slots,&S,257,9,at,0,0));
    assert(!rs_scope_clear_run(commands,&two,slots,&S,257,1,at,0,0));
    reset();clear(15);draw();append(OP_CLEAR_DS,&depth,sizeof depth);assert(!rs_scope_clear_run(commands,&two,slots,&S,257,9,at,0,0));
    reset();clear(15);depth.stencil_mask=127;append(OP_CLEAR_DS,&depth,sizeof depth);assert(!rs_scope_clear_run(commands,&two,slots,&S,257,9,at,0,0));
    // Full color/depth initialization and overwrite may span immutable
    // handles, but not draws, unknown barriers, or partial scissors.
    reset();clear(15);append(OP_END,NULL,0);
    uint8_t *clear_handle=commands+at;
    p_scissor full_sc={0,0,64,64};append(OP_SCISSOR,&full_sc,sizeof full_sc);
    clear_depth();draw();consumer_two();rt_ds(&t[0],&t[1]);discard();end=discard_slot(-1);
    append(OP_END,NULL,0);
    uint64_t clear_rest[]={(uintptr_t)clear_handle};
    g_rs_scope_rest=clear_rest;g_rs_scope_rest_n=1;
    two.addr[0]=(uintptr_t)t[0].mem;two.addr[8]=(uintptr_t)t[1].mem;
    assert(rs_scope_scan(commands,&two,&reason)==end && g_rs_scope_crossed==1);
    g_rs_scope_rest_n=0;
    assert(!rs_scope_scan(commands,&two,&reason));
    reset();clear(15);clear_depth();draw();consumer_two();rt_ds(&t[0],&t[1]);
    overwrite=commands+at;clear(15);append(OP_END,NULL,0);
    clear_rest[0]=(uintptr_t)(commands+at);
    append(OP_SCISSOR,&full_sc,sizeof full_sc);clear_depth();append(OP_END,NULL,0);
    g_rs_scope_rest=clear_rest;g_rs_scope_rest_n=1;
    assert(rs_scope_scan(commands,&two,&reason)==overwrite && g_rs_scope_overwrite && !g_rs_scope_crossed);
    reset();clear(15);clear_depth();draw();consumer_two();rt_ds(&t[0],&t[1]);
    clear(15);append(OP_END,NULL,0);clear_rest[0]=(uintptr_t)(commands+at);
    full_sc.w=32;append(OP_SCISSOR,&full_sc,sizeof full_sc);clear_depth();append(OP_END,NULL,0);
    g_rs_scope_rest=clear_rest;g_rs_scope_rest_n=1;
    assert(!rs_scope_scan(commands,&two,&reason));
    // A multi-owner lifetime can close at the first full overwrite even
    // when the remaining exact owner is rebound and cleared separately.
    reset();clear(15);clear_depth();draw();consumer_two();rt(&t[0]);
    overwrite=commands+at;clear(15);rt_ds(NULL,&t[1]);clear_depth();append(OP_END,NULL,0);
    assert(rs_scope_scan(commands,&two,&reason)==overwrite && g_rs_scope_overwrite);
    reset();clear(15);clear_depth();draw();consumer_two();rt(&t[0]);
    clear(15);rt_ds(NULL,&t[1]);draw();clear_depth();append(OP_END,NULL,0);
    assert(!rs_scope_scan(commands,&two,&reason));
    reset();clear(15);clear_depth();draw();consumer_two();rt(&t[0]);
    clear(15);rt_ds(NULL,&t[3]);clear_depth();append(OP_END,NULL,0);
    assert(!rs_scope_scan(commands,&two,&reason));
    // Full texture clears can end the sampled owner without rebinding it.
    reset();t[0].d.format=NVN_FORMAT_BGRA8;producer();consumer();
    p_clear_tex raw={.tex=&t[0],.r={0,0,0,64,64,1},.mask=NVN_CLEAR_COLOR_MASK_RGBA};
    overwrite=append(OP_CLEAR_TEX,&raw,sizeof raw);
    assert(scan()==overwrite && g_rs_scope_overwrite);
    reset();t[0].d.format=NVN_FORMAT_BGRA8;producer();consumer();
    raw.r.width=63;append(OP_CLEAR_TEX,&raw,sizeof raw);assert(!scan());raw.r.width=64;
    reset();t[0].d.format=NVN_FORMAT_BGRA8;producer();consumer();
    raw.mask=7;append(OP_CLEAR_TEX,&raw,sizeof raw);assert(!scan());raw.mask=NVN_CLEAR_COLOR_MASK_RGBA;
    reset();t[0].d.format=NVN_FORMAT_BGRA8;producer();consumer();
    raw.has_view=1;raw.view.set_levels=1;raw.view.base_level=1;raw.view.nlevels=1;
    append(OP_CLEAR_TEX,&raw,sizeof raw);assert(!scan());
    // An unbound compute producer must initialize before a scaled sample
    // and terminate at the exact output's complete texture overwrite.
    reset();t[0].d.format=NVN_FORMAT_BGRA8;
    p_bind_handles fill_bind={.stage=5,.first=0,.n=1,.h={10}};
    append(OP_BIND_IMG,&fill_bind,offsetof(p_bind_handles,h)+8);
    p_dispatch fill_dispatch={.x=8,.y=8,.z=1};
    append(OP_DISPATCH,&fill_dispatch,sizeof fill_dispatch);consumer();
    p_clear_tex finish_fill={.tex=&t[0],.r={0,0,0,64,64,1},.mask=NVN_CLEAR_COLOR_MASK_RGBA};
    overwrite=append(OP_CLEAR_TEX,&finish_fill,sizeof finish_fill);append(OP_END,NULL,0);
    rs_scope_group compute_group={.target={&t[0]},.addr={(uintptr_t)t[0].mem},.mask=1,.w=64,.h=64,.pw=32,.ph=32};
    assert(rs_scope_scan_initial(commands,&compute_group,0,&reason)==overwrite && g_rs_scope_overwrite);
    reset();t[0].d.format=NVN_FORMAT_BGRA8;consumer();
    append(OP_CLEAR_TEX,&finish_fill,sizeof finish_fill);append(OP_END,NULL,0);
    assert(!rs_scope_scan_initial(commands,&compute_group,0,&reason));
    reset();t[0].d.format=NVN_FORMAT_BGRA8;
    append(OP_BIND_IMG,&fill_bind,offsetof(p_bind_handles,h)+8);
    append(OP_DISPATCH,&fill_dispatch,sizeof fill_dispatch);
    rt_ds(&t[0],&t[1]);clear_depth();draw();consumer_two();
    rt_ds(&t[0],&t[1]);discard();discard_slot(-1);append(OP_END,NULL,0);
    compute_group=(rs_scope_group){.target={&t[0]},.addr={(uintptr_t)t[0].mem},.mask=1,.w=64,.h=64,.pw=32,.ph=32};
    assert(!rs_scope_scan_initial(commands,&compute_group,0,&reason));
    // A lifetime can cross immutable handle boundaries, including between
    // producer and consumer and before the terminal discard.
    reset();producer();append(OP_END,NULL,0);uint8_t *h2=commands+at;
    consumer();append(OP_END,NULL,0);uint8_t *h3=commands+at;rt(&t[0]);end=discard();
    uint64_t rest[]={ (uintptr_t)h2,(uintptr_t)h3 };
    g_rs_scope_rest=rest;g_rs_scope_rest_n=2;
    assert(scan()==end && g_rs_scope_crossed==2);
    g_rs_scope_rest_n=1;assert(!rs_scope_scan(commands,&(rs_scope_group){.target={&t[0]},.addr={(uintptr_t)t[0].mem},.mask=1,.w=64,.h=64},&reason));
    // Depth stays live when two different color images are initialized.
    reset();clear_depth();draw();rt_ds(&t[1],&t[0]);producer();
    rt_ds(&t[2],&t[0]);clear(15);bind(7,0);draw();
    end=finish_depth_scope();assert(scan_depth()==end);
    reset();clear_depth();draw();
    p_set_rt expanded_view={.n=1,.color={&t[1]},.has_cview={1},.cview={same},.depth=&t[0]};
    append(OP_SET_RT,&expanded_view,sizeof expanded_view);producer();
    rt_ds(&t[2],&t[0]);clear(15);bind(7,0);draw();
    end=finish_depth_scope();assert(scan_depth()==end);
    reset();clear_depth();draw();expanded_view.cview[0].min_layer=1;
    append(OP_SET_RT,&expanded_view,sizeof expanded_view);producer();
    rt_ds(&t[2],&t[0]);clear(15);bind(7,0);draw();
    finish_depth_scope();assert(!scan_depth());
    reset();clear_depth();draw();rt_ds(&t[1],&t[0]);producer();
    rt_ds(&t[2],&t[0]);producer();
    p_clear_ds stencil_only={.stencil_mask=255};
    append(OP_CLEAR_DS,&stencil_only,sizeof stencil_only);draw();
    end=finish_depth_scope();assert(scan_depth()==end);
    reset();clear_depth();draw();rt_ds(&t[1],&t[0]);producer();
    rt_ds(&t[2],&t[0]);clear(7);draw();finish_depth_scope();
    assert(!scan_depth() && !strcmp(reason,"attachment initialization or clear"));
    reset();clear_depth();draw();rt_ds(&t[1],&t[0]);producer();
    rt_ds(&t[2],&t[0]);draw();finish_depth_scope();
    assert(!scan_depth() && !strcmp(reason,"uninitialized attachment, feedback or ended lifetime"));
    reset();clear_depth();draw();rt_ds(&t[1],&t[0]);producer();
    rt_ds(&t[2],&t[0]);clear(15);bind(9,0);draw();finish_depth_scope();
    assert(!scan_depth());
    reset();clear_depth();draw();rt_ds(&t[1],&t[0]);producer();
    t[2].d.width=32;rt_ds(&t[2],&t[0]);producer();finish_depth_scope();
    assert(!scan_depth() && !strcmp(reason,"attachment compatibility"));
    reset();clear_depth();draw();rt_ds(&t[1],&t[0]);producer();
    t[2].off=4160;t[2].mem=storage+4160;
    rt_ds(&t[2],&t[0]);producer();finish_depth_scope();
    assert(!scan_depth() && !strcmp(reason,"attachment compatibility"));
    reset();clear_depth();draw();update[0]=(uintptr_t)t[2].mem;update[1]=8;
    append(OP_UPDATE_UB,update,sizeof update);
    rt_ds(&t[1],&t[0]);producer();rt_ds(&t[2],&t[0]);producer();finish_depth_scope();
    assert(!scan_depth());
    // Four then six distinct color owners exceed one framebuffer's slots.
    reset();clear_depth();draw();append(OP_END,NULL,0);
    uint8_t *scene=commands+at;
    mrt(1,4);for(unsigned i=0;i<4;i++)clear_slot(i,15);draw();
    mrt(5,6);for(unsigned i=0;i<6;i++)clear_slot(i,15);draw();
    rt(&t[11]);bind(9,0);draw();
    mrt(5,6);for(int i=0;i<6;i++)discard_slot(i);
    mrt(1,4);for(int i=0;i<4;i++)discard_slot(i);
    end=discard_slot(-1);
    uint64_t scene_handles[]={(uintptr_t)scene};
    g_rs_scope_rest=scene_handles;g_rs_scope_rest_n=1;
    assert(scan_depth()==end && g_rs_scope_crossed==1);
    g_rs_scope_rest_n=0;
    assert(!scan_depth() && !strcmp(reason,"immutable replay window ended before lifetime termination"));
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
