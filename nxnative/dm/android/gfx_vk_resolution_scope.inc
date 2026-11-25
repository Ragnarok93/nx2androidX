// Closed, bounded immutable-replay lifetime proof. This is deliberately a
// narrow production seam: no raw transfers, indirect dispatch/draw, unknown
// descriptors, feedback, conditional rendering or open-ended lifetimes.
#include "gfx_vk_resolution_scope.h"
static const uint8_t *g_rs_scope_end;
static unsigned g_rs_scope_nested,g_rs_scope_program_count;
static const uint64_t *g_rs_scope_rest;
static unsigned g_rs_scope_rest_n,g_rs_scope_crossed;
enum { RS_SCOPE_PROGRAM_MAX=32, RS_SCOPE_OP_MAX=4096, RS_SCOPE_BYTES_MAX=4*1024*1024, RS_SCOPE_HANDLE_MAX=32 };
typedef struct { const uint8_t *record; const prog_t *prog; vkshader_t *sh[6]; } rs_scope_program;
static rs_scope_program g_rs_scope_programs[RS_SCOPE_PROGRAM_MAX];
typedef struct { const tex_t *target[9]; uint64_t addr[9]; unsigned mask; uint32_t w,h; } rs_scope_group;
extern vkshader_t *vk_shader_cached(uint64_t,int);
uint64_t nvn_replaying_job(void);
static uint64_t rs_scope_addr(const tex_t *t) {
    if (!t || !t->serial || t->magic!=MAGIC('T','E','X',0) || !t->pool || !t->pool->mem ||
        !t->mem || t->off<0 || !t->L.size || (t->pool->flags&NVN_MEMORY_POOL_FLAGS_VIRTUAL)) return 0;
    uint64_t base=(uintptr_t)t->pool->mem, off=(uint64_t)t->off, n=t->L.size;
    if (off>t->pool->size || n>t->pool->size-off || off>UINT64_MAX-base) return 0;
    uint64_t a=base+off;
    return a && a==(uintptr_t)t->mem && n<=UINT64_MAX-a?a:0;
}
// -1 unknown/alias, 0 disjoint, 1..9 exact original attachment.
static int rs_scope_match(const rs_scope_group *g,const tex_t *t,const view_t *v) {
    uint64_t a=rs_scope_addr(t); if (!a) return -1;
    int hit=0;
    for (unsigned i=0;i<9;i++) if (g->target[i] &&
        nx_rs_range_overlap(a,t->L.size,g->addr[i],g->target[i]->L.size)) {
        const tex_t *o=g->target[i];
        if (hit || v || t->has_layout || t->has_pkg_data || t->set_swz ||
            a!=g->addr[i] || t->pool!=o->pool || t->off!=o->off ||
            t->L.size!=o->L.size || memcmp(&t->d,&o->d,sizeof t->d)) return -1;
        hit=(int)i+1;
    }
    return hit;
}
static int rs_scope_raw(const rs_scope_group *g,uint64_t addr,uint64_t bytes,int tail) {
    if (!addr) return 0;
    vkpool_t *p=vk_pool_of(addr,1);
    if (!p || !p->buf || (p->flags&NVN_MEMORY_POOL_FLAGS_VIRTUAL) || addr<p->lo || addr>=p->hi) return 0;
    if (tail) bytes=p->hi-addr;
    if (!bytes || bytes>p->hi-addr) return 0;
    for (unsigned i=0;i<9;i++) if (g->target[i] && nx_rs_range_overlap(addr,bytes,g->addr[i],g->target[i]->L.size)) return 0;
    return 1;
}
static int rs_scope_uniform(const rs_scope_group *g,const p_bind_buf *u) {
    if (!u->addr) return 1;
    vkpool_t *p=vk_pool_of(u->addr,1);
    uint64_t align=V.props.limits.minUniformBufferOffsetAlignment,start,size;
    uint64_t n=!u->size || u->size>65536?65536:u->size;
    return p && p->buf && align && !(p->flags&NVN_MEMORY_POOL_FLAGS_VIRTUAL) && !(p->boff%align) &&
        nx_rs_buffer_span(p->lo,p->hi,u->addr,n,align,&start,&size) && rs_scope_raw(g,start,size,0);
}
static int rs_scope_draw_proof(const rs_scope_group *g,const __typeof__(S) *look,int scaled,int *samples) {
    *samples=0;
    if (!look->sh[0] || look->rast_discard || look->sh[2] || look->sh[3] || look->sh[4]) return 0;
    for (unsigned i=0;i<16;i++) if (look->vbuf[i].address && !rs_scope_raw(g,look->vbuf[i].address,0,1)) return 0;
    for (int st=0;st<2;st++) {
        vkshader_t *s=look->sh[st]; if (!s) continue;
        if (s->xfb || s->nbind<0 || s->nbind>96 || (s->nbind && !s->b) ||
            shader_wmem(s) || !nx_rs_compute_logical(s->spv,s->nspv)) return 0;
        int support=0,stage_samples=0;
        for (int i=0;i<s->nbind;i++) {
            const dmsv_bind *b=&s->b[i];
            if (b->count!=1 || b->samp==RS_UBO) return 0;
            switch (b->kind) {
            case RK_SUPPORT: case RK_C1: case RK_DRV:
                if (b->desc!=RD_UBO) return 0;
                if (b->kind==RK_SUPPORT) support=1;
                break;
            case RK_CBUF:
                if (b->desc!=RD_UBO || b->reg<0 || b->reg>=16 || !rs_scope_uniform(g,&look->ubo[st][b->reg])) return 0;
                break;
            case RK_TEX: {
                if (b->desc!=RD_SAMPLED || b->reg<0 || b->reg>=256) return 0;
                uint64_t h=vk_tex_slot_handle(look->tex[st],look->img[st],(unsigned)b->reg);
                int tid=(int)(h&0xfffff); if (!tid) break;
                const tex_t *t=NULL; const view_t *v=NULL;
                if (!vk_tex_by_id(tid,&v,&t) || !t) return 0;
                int match=rs_scope_match(g,t,v);
                if (match<0) return 0;
                if (match) {
                    unsigned slot=0,flags=0;
                    if (scaled || b->dim!=1 || (b->flags&3) ||
                        !vk_shader_resolution_binding(s,b->binding,&slot,&flags) ||
                        slot==NX_RS_REF_UNKNOWN || (flags&(NX_RS_REF_UNSUPPORTED|NX_RS_REF_WRITES))) return 0;
                    stage_samples=1;
                }
                break;
            }
            default: return 0;
            }
        }
        if ((stage_samples || (scaled && st==1)) && !support) return 0;
        *samples|=stage_samples;
    }
    return 1;
}
static int rs_scope_program_proof(__typeof__(S) *look,const uint8_t *record,const p_bind_program *bp) {
    if (g_rs_scope_program_count==RS_SCOPE_PROGRAM_MAX) return 0;
    uint32_t mask=bp->stages?(uint32_t)bp->stages&0x3f:0x3f;
    for (int st=0;st<6;st++) if (mask>>st&1) look->sh[st]=NULL;
    look->prog=bp->prog;
    if (bp->prog) {
        if (bp->prog->magic!=MAGIC('P','R','O','G') || bp->prog->nstage>6) return 0;
        int vk_shader_stage(uint64_t);
        for (unsigned i=0;i<6;i++) {
            uint8_t k[8]; if (!prog_key(bp->prog,i,k)) break;
            uint64_t key=key64(k); int st=key?vk_shader_stage(key):-1;
            if (st<0 || st>5) return 0;
            if (!(mask>>st&1)) continue;
            if (!(look->sh[st]=vk_shader_cached(key,st))) return 0;
        }
    }
    rs_scope_program *p=&g_rs_scope_programs[g_rs_scope_program_count++];
    p->record=record;p->prog=look->prog;memcpy(p->sh,look->sh,sizeof p->sh);
    return 1;
}
static void rs_scope_replay_program(const uint8_t *p) {
    for (unsigned i=0;i<g_rs_scope_program_count;i++) if (g_rs_scope_programs[i].record==p) {
        const rs_scope_program *b=&g_rs_scope_programs[i];
        S.prog=b->prog;memcpy(S.sh,b->sh,sizeof S.sh);S.prog_ok=S.sh[0]||S.sh[5];return;
    }
    die("[vk] resolution: program outside certified snapshot");
}
static int rs_scope_full_scissor(const __typeof__(S) *look,const rs_scope_group *g) {
    return !look->sc_set || (look->sc.x<=0 && look->sc.y<=0 &&
        (int64_t)look->sc.x+look->sc.w>=g->w && (int64_t)look->sc.y+look->sc.h>=g->h);
}
// 1 exactly original group, 0 disjoint native group, -1 unsupported.
static int rs_scope_rt(const rs_scope_group *g,const p_set_rt *r) {
    if (r->n<0 || r->n>8) return -1;
    unsigned original=0,native=0;
    for (unsigned i=0;i<9;i++) {
        const tex_t *t=i==8?r->depth:i<(unsigned)r->n?r->color[i]:NULL;
        if (!t) continue;
        const view_t *v=i==8?(r->has_dview?&r->dview:NULL):(r->has_cview[i]?&r->cview[i]:NULL);
        int match=rs_scope_match(g,t,v);
        if (match<0 || (match && match!=(int)i+1)) return -1;
        if (match) original|=1u<<i; else native++;
    }
    if (original==g->mask && !native) return 1;
    return !original && native?0:-1;
}
static const uint8_t *rs_scope_scan(const uint8_t *p,const rs_scope_group *g) {
    __typeof__(S) look=S;
    nx_rs_scope life; if (!nx_rs_scope_init(&life,g->mask)) return NULL;
    size_t bytes=0;unsigned crossed=0;
    g_rs_scope_crossed=0;
    for (unsigned op=0;op<RS_SCOPE_OP_MAX;op++) {
        op_hdr h;memcpy(&h,p,sizeof h);
        if (h.size<8 || (h.size&7) || h.size>RS_SCOPE_BYTES_MAX-bytes) return NULL;
        bytes+=h.size;const uint8_t *a=p+8;size_t n=h.size-8;
        // The replay job owns every flattened handle in this array. Keep
        // the same byte/op bound across all handles, not per handle.
        if (h.op==OP_END) {
            if (h.size!=8 || !g_rs_scope_rest || crossed>=g_rs_scope_rest_n || crossed>=RS_SCOPE_HANDLE_MAX) return NULL;
            p=(const uint8_t *)(uintptr_t)g_rs_scope_rest[crossed++];
            if (!p) return NULL;
            continue;
        }
        if (life.phase==2 && h.op!=OP_RS_DISCARD) return NULL;
#define RS_NEED(T) do { if (n<sizeof(T)) return NULL; } while (0)
        switch (h.op) {
        case OP_SET_RT: {
            RS_NEED(p_set_rt);
            int match=rs_scope_rt(g,(const p_set_rt *)a);
            if (match<0 || !nx_rs_scope_switch(&life,match)) return NULL;break; }
        case OP_CLEAR_COLOR: {
            RS_NEED(p_clear_color);const p_clear_color *c=(const p_clear_color *)a;
            if (life.phase==0 && (c->index<0 || c->index>=8 ||
                !nx_rs_scope_clear(&life,1u<<c->index,c->mask==15 && rs_scope_full_scissor(&look,g)))) return NULL;
            break; }
        case OP_CLEAR_DS: {
            RS_NEED(p_clear_ds);const p_clear_ds *c=(const p_clear_ds *)a;
            if (life.phase==0) {
                const vkfmt_t *f=g->target[8]?vk_fmt(g->target[8]->d.format):NULL;
                if (!f || !nx_rs_scope_clear(&life,1u<<8,rs_scope_full_scissor(&look,g) &&
                    (!f->depth || c->depth_mask) && (!f->stencil || (c->stencil_mask&255)==255))) return NULL;
            } break; }
        case OP_RS_DISCARD: {
            RS_NEED(p_rs_discard);int i=((const p_rs_discard *)a)->index;
            if (i<-1 || i>=8) return NULL;
            int result=nx_rs_scope_discard(&life,1u<<(i==-1?8:i));
            if (!result) return NULL;
            if (result==2) {g_rs_scope_crossed=crossed;return p;}
            break; }
        case OP_BIND_PROGRAM: RS_NEED(p_bind_program);
            if (!rs_scope_program_proof(&look,p,(const p_bind_program *)a)) return NULL;break;
        case OP_BIND_UBO: {
            RS_NEED(p_bind_buf);const p_bind_buf *b=(const p_bind_buf *)a;
            if (b->stage<0 || b->stage>=6 || b->index<0 || b->index>=16) return NULL;
            look.ubo[b->stage][b->index]=*b;break; }
        case OP_BIND_VBUF: {
            if (n<offsetof(p_bind_vbuf,r)) return NULL;
            const p_bind_vbuf *b=(const p_bind_vbuf *)a;
            if (b->first<0 || b->n<0 || b->first>16 || b->n>16-b->first ||
                n<offsetof(p_bind_vbuf,r)+(size_t)b->n*sizeof b->r[0]) return NULL;
            for (int i=0;i<b->n;i++) look.vbuf[b->first+i]=b->r[i];
            break; }
        case OP_BIND_TEX: case OP_BIND_TEX1: case OP_BIND_SAMP: {
            p_bind_handles tmp;
            if (h.op==OP_BIND_TEX1) { RS_NEED(p_bind_tex1); }
            else if (n<offsetof(p_bind_handles,h)) return NULL;
            const p_bind_handles *b=bind_rec(h.op,a,&tmp);
            int limit=h.op==OP_BIND_SAMP?128:256;
            if (b->defer || b->stage<0 || b->stage>=6 || b->first<0 || b->n<0 || b->n>32 ||
                b->first>limit || b->n>limit-b->first ||
                (h.op!=OP_BIND_TEX1 && n<offsetof(p_bind_handles,h)+(size_t)b->n*8)) return NULL;
            if (h.op!=OP_BIND_SAMP) for (int i=0;i<b->n;i++) look.tex[b->stage][b->first+i]=b->h[i];
            break; }
        case OP_SCISSOR: RS_NEED(p_scissor);look.sc=*(const p_scissor *)a;look.sc_set=1;break;
        case OP_RAST_DISCARD: RS_NEED(p_rast_discard);look.rast_discard=((const p_rast_discard *)a)->on;break;
        case OP_DRAW: case OP_DRAW_IDX: {
            if (h.op==OP_DRAW) {
                p_draw tmp;if (n<offsetof(p_draw,ind) || n>sizeof tmp) return NULL;
                const p_draw *d=draw_rec(a,h.size,&tmp);
                if (d->ind || d->mdi_cnt || d->count<=0 || d->first<0) return NULL;
            } else {
                p_draw_idx tmp;if (n<offsetof(p_draw_idx,ind) || n>sizeof tmp) return NULL;
                const p_draw_idx *d=draw_idx_rec(a,h.size,&tmp);
                if (d->ind || d->mdi_cnt || d->count<=0 || d->itype<0 || d->itype>2 ||
                    !rs_scope_raw(g,d->ibuf,(uint64_t)(unsigned)d->count<<d->itype,0)) return NULL;
            }
            int sampled=0;
            if (!rs_scope_draw_proof(g,&look,life.phase==0,&sampled) || !nx_rs_scope_draw(&life,1,sampled)) return NULL;
            break; }
        // State-only records, no guest data reads or resource accesses.
        case OP_BEGIN: case OP_VIEWPORTS: case OP_DEPTH_RANGE: case OP_BIND_STATE:
        case OP_BIND_VATTR: case OP_BIND_VSTREAM: case OP_STENCIL: case OP_POLY_OFFSET:
        case OP_BARRIER: case OP_SET_TEX_POOL: case OP_SET_SAMP_POOL: case OP_BLEND_COLOR:
        case OP_DEPTH_CLAMP: case OP_VP_SWIZZLE: case OP_PRIM_RESTART: case OP_DEPTH_BOUNDS: break;
        default: return NULL;
        }
#undef RS_NEED
        p+=h.size;
    }
    return NULL;
}
static void rs_scope_try_begin(const uint8_t *next) {
    // rec_copy makes immutable, flat handles with terminal OP_END records.
    // Synchronous guest command memory and nested/unflattened calls are not
    // proof inputs. Reject diagnostic modes that can inject extra accesses.
    if (!nvn_replaying_job() || S.pass_open || vk_rs_cache_has_scaled() || g_rc_skip || g_tf_on ||
        DM_GAME_RENDER_COND || g_bis_on || g_pd_state || g_x_listpasses || g_x_tracepass>=0 ||
        g_td_off>=0 || g_x_nflat || g_x_nobar || g_x_depth || g_x_color ||
        g_lrz_touch || g_ubolog_key || g_x_logfs || slot0_fill_on()) return;
    rs_scope_group group={0};nx_rs_group dims={0};
    for (unsigned i=0;i<9;i++) {
        if (i<8 && i<(unsigned)S.rt_n && S.rt_bad[i]) return;
        if (i<8 && (i>=(unsigned)S.rt_n || !S.rt_has_col[i])) continue;
        if (i==8 && !S.rt_has_dep) continue;
        if (i==8?S.rt_has_dview:S.rt_has_cview[i]) return;
        const tex_t *t=i==8?&S.rt_dep:&S.rt_col[i];
        if (!(group.addr[i]=rs_scope_addr(t)) || !t->pool ||
            !(t->pool->flags&NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS) || t->set_swz ||
            t->has_layout || t->has_pkg_data) return;
        group.target[i]=t;group.mask|=1u<<i;
        nx_rs_add(&dims,t->d.width,t->d.height,0);
    }
    group.w=dims.w;group.h=dims.h;
    nx_rs_extent ext;
    if (!nx_rs_plan(&dims,(int)g_rs_frame.target,V.props.limits.maxFramebufferWidth,V.props.limits.maxFramebufferHeight,&ext) ||
        (uint64_t)ext.w*group.h!=(uint64_t)ext.h*group.w || !vk_rs_scope_freeze_begin()) return;
    g_rs_scope_program_count=0;
    const uint8_t *end=rs_scope_scan(next,&group);
    if (!end || !vk_rs_allocate_fresh_group(group.target,ext.w,ext.h)) {
        g_rs_scope_program_count=0;g_rs_scope_crossed=0;vk_rs_scope_freeze_end();return;
    }
    vk_rs_scope_freeze_seal();g_rs_scope_end=end;
    static unsigned accepted;
    if (++accepted<=16 || accepted%300==0)
        VKLOG("[resolution-scope] authorized raster/consumer/discard group %ux%u -> %ux%u (%u)",group.w,group.h,ext.w,ext.h,accepted);
}
