// Closed, bounded immutable-replay lifetime proof. This is deliberately a
// narrow production seam: only disjoint bounded raw and indirect accesses, no unknown
// descriptors, feedback, conditional rendering or open-ended lifetimes.
#include "gfx_vk_resolution_scope.h"
static unsigned g_rs_scope_nested,g_rs_scope_program_count;
static int g_rs_scope_overwrite;
static const uint64_t *g_rs_scope_rest;
static unsigned g_rs_scope_rest_n,g_rs_scope_crossed;
enum { RS_SCOPE_PROGRAM_MAX=32, RS_SCOPE_OP_MAX=4096, RS_SCOPE_BYTES_MAX=4*1024*1024, RS_SCOPE_HANDLE_MAX=32 };
typedef struct { const uint8_t *record; const prog_t *prog; vkshader_t *sh[6]; } rs_scope_program;
static rs_scope_program g_rs_scope_programs[RS_SCOPE_PROGRAM_MAX];
typedef struct { const tex_t *target[NX_RS_SCOPE_OWNER_MAX]; uint64_t addr[NX_RS_SCOPE_OWNER_MAX]; unsigned mask; uint32_t w,h; } rs_scope_group;
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
// -1 unknown/alias, 0 disjoint, positive one-based owner index.
static int rs_scope_match(const rs_scope_group *g,const tex_t *t,const view_t *v) {
    uint64_t a=rs_scope_addr(t); if (!a || !vk_fmt(t->d.format)) return -1;
    int hit=0;
    for (unsigned i=0;i<NX_RS_SCOPE_OWNER_MAX;i++) if (g->target[i] &&
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
    for (unsigned i=0;i<NX_RS_SCOPE_OWNER_MAX;i++) if (g->target[i] && nx_rs_range_overlap(addr,bytes,g->addr[i],g->target[i]->L.size)) return 0;
    return 1;
}
// Match the descriptor span used by stage_set, including the aligned
// prefix. Require an explicit range instead of accepting its chunk-tail
// fallback. Compute dispatch dimensions remain native.
static int rs_scope_storage_buffer(const rs_scope_group *g,const p_bind_buf *u) {
    if (!u->addr || !u->size) return 0;
    vkpool_t *p=vk_pool_of(u->addr,1);
    uint64_t start,size;
    return p && p->buf && !(p->flags&NVN_MEMORY_POOL_FLAGS_VIRTUAL) &&
        V.props.limits.minStorageBufferOffsetAlignment<=SB_ALIGN && !(p->boff%SB_ALIGN) &&
        nx_rs_buffer_span(p->lo,p->hi,u->addr,u->size,SB_ALIGN,&start,&size) &&
        rs_scope_raw(g,start,size,0);
}
static int rs_scope_uniform(const rs_scope_group *g,const p_bind_buf *u) {
    if (!u->addr) return 1;
    vkpool_t *p=vk_pool_of(u->addr,1);
    uint64_t align=V.props.limits.minUniformBufferOffsetAlignment,start,size;
    uint64_t n=!u->size || u->size>65536?65536:u->size;
    return p && p->buf && align && !(p->flags&NVN_MEMORY_POOL_FLAGS_VIRTUAL) && !(p->boff%align) &&
        nx_rs_buffer_span(p->lo,p->hi,u->addr,n,align,&start,&size) && rs_scope_raw(g,start,size,0);
}
static int rs_scope_native_check(const tex_t *t,const view_t *v,int texel,void *ctx) {
    const rs_scope_group *g=ctx;
    if (texel) {
        uint64_t addr,bytes;
        return resolution_texel_span(t,v,&addr,&bytes) && rs_scope_raw(g,addr,bytes,0);
    }
    return t->d.target!=NVN_TEXTURE_TARGET_BUFFER && rs_scope_match(g,t,v)==0;
}
static int rs_scope_draw_proof(const rs_scope_group *g,const __typeof__(S) *look,int scaled,unsigned *samples) {
    *samples=0;
    if (!look->sh[0] || look->rast_discard || look->sh[2] || look->sh[3] || look->sh[4]) return 0;
    for (unsigned i=0;i<16;i++) if (look->vbuf[i].address && !rs_scope_raw(g,look->vbuf[i].address,0,1)) return 0;
    for (int st=0;st<2;st++) {
        vkshader_t *s=look->sh[st]; if (!s) continue;
        if (s->xfb || s->nbind<0 || s->nbind>96 || (s->nbind && !s->b) ||
            shader_wmem(s) || !nx_rs_compute_logical(s->spv,s->nspv)) return 0;
        int support=0; unsigned stage_samples=0;
        for (int i=0;i<s->nbind;i++) {
            const dmsv_bind *b=&s->b[i];
            int native=resolution_native_binding(b,look->tex[st],look->img[st],0,DM_GAME_TEX_DYN_SLOTS,rs_scope_native_check,(void *)g);
            if (native) { if (native<0) return 0; continue; }
            if (b->count!=1 || b->samp==RS_UBO) return 0;
            switch (b->kind) {
            case RK_SUPPORT: case RK_C1: case RK_DRV:
                if (b->desc!=RD_UBO) return 0;
                if (b->kind==RK_SUPPORT) support=1;
                break;
            case RK_CBUF:
                if (b->desc!=RD_UBO || b->reg<0 || b->reg>=16 || !rs_scope_uniform(g,&look->ubo[st][b->reg])) return 0;
                break;
            case RK_SBUF: case RK_UBO_PTR:
                // The shader_wmem guard above excludes graphics memory
                // writes; only explicit, disjoint native read spans qualify.
                if (b->desc!=RD_SSBO || b->reg<0 || b->reg>=16 ||
                    !rs_scope_storage_buffer(g,b->kind==RK_SBUF?&look->ssbo[st][b->reg]:&look->ubo[st][b->reg])) return 0;
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
                    if (b->dim!=1 || (b->flags&3) ||
                        !vk_shader_resolution_binding(s,b->binding,&slot,&flags) ||
                        slot==NX_RS_REF_UNKNOWN || (flags&(NX_RS_REF_UNSUPPORTED|NX_RS_REF_WRITES))) return 0;
                    stage_samples|=1u<<(match-1);
                }
                break;
            }
            default: return 0;
            }
        }
        if ((stage_samples || (scaled && st==1)) && (!support || !s->refl_extra_len)) return 0;
        *samples|=stage_samples;
    }
    return 1;
}
// Direct native-grid compute consumers: scaled inputs are sampled only,
// storage destinations must be provably disjoint native images or explicit
// bounded native SSBOs. No bindless handles, physical addressing, indirect
// grid changes; indirect argument reads are bounded and disjoint.
static int rs_scope_compute_proof(const rs_scope_group *g,const __typeof__(S) *look,const p_dispatch *d,unsigned *samples) {
    *samples=0;
    vkshader_t *s=look->sh[5];
    if (!s || s->stage!=5 || s->nbind<0 || s->nbind>96 || (s->nbind && !s->b) ||
        !nx_rs_compute_logical(s->spv,s->nspv)) return 0;
    if (d->ind) {
        if ((d->ind&3) || !rs_scope_raw(g,d->ind,12,0)) return 0;
    } else if (d->x<=0 || d->y<=0 || d->z<=0 ||
        (uint32_t)d->x>V.props.limits.maxComputeWorkGroupCount[0] ||
        (uint32_t)d->y>V.props.limits.maxComputeWorkGroupCount[1] ||
        (uint32_t)d->z>V.props.limits.maxComputeWorkGroupCount[2]) return 0;
    int support=0;
    for (int i=0;i<s->nbind;i++) {
        const dmsv_bind *b=&s->b[i];
        int native=resolution_native_binding(b,look->tex[5],look->img[5],1,DM_GAME_TEX_DYN_SLOTS,rs_scope_native_check,(void *)g);
        if (native) { if (native<0) return 0; continue; }
        if (b->count!=1 || b->samp==RS_UBO) return 0;
        switch(b->kind) {
        case RK_SUPPORT: case RK_C1: case RK_DRV:
            if (b->desc!=RD_UBO) return 0;
            if (b->kind==RK_SUPPORT) support=1;
            break;
        case RK_CBUF:
            if (b->desc!=RD_UBO || b->reg<0 || b->reg>=16 || !rs_scope_uniform(g,&look->ubo[5][b->reg])) return 0;
            break;
        case RK_SBUF:
            if (b->desc!=RD_SSBO || b->reg<0 || b->reg>=16 ||
                !rs_scope_storage_buffer(g,&look->ssbo[5][b->reg])) return 0;
            break;
        case RK_TEX: case RK_IMG: {
            int storage=b->kind==RK_IMG;
            if (b->desc!=(storage?RD_STORAGE:RD_SAMPLED) || b->reg<0 || b->reg>=(storage?32:256)) return 0;
            uint64_t handle=storage?look->img[5][b->reg]:vk_tex_slot_handle(look->tex[5],look->img[5],(unsigned)b->reg);
            int tid=(int)(handle&0xfffff);
            if (!tid) {if (storage) return 0;break;}
            const tex_t *t=NULL;const view_t *v=NULL;
            if (!vk_tex_by_id(tid,&v,&t) || !t || t->d.target==NVN_TEXTURE_TARGET_BUFFER) return 0;
            int match=rs_scope_match(g,t,v);
            if (match<0 || (match && storage)) return 0;
            if (storage && (v || t->d.target!=NVN_TEXTURE_TARGET_2D || t->d.levels!=1 ||
                t->d.depth!=1 || t->d.samples>1 || b->dim!=1 || (b->flags&3) ||
                !(t->d.flags&NVN_TEXTURE_FLAGS_IMAGE))) return 0;
            if (match) {
                unsigned slot=0,flags=0;
                if (b->dim!=1 || (b->flags&3) ||
                    !vk_shader_resolution_binding(s,b->binding,&slot,&flags) ||
                    slot==NX_RS_REF_UNKNOWN || (flags&(NX_RS_REF_UNSUPPORTED|NX_RS_REF_WRITES))) return 0;
                *samples|=1u<<(match-1);
            }
            break; }
        default:return 0;
        }
    }
    return !*samples || (support && s->refl_extra_len);
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
// Resolve the next binding through owner identities. A mixed pass can grow
// the transaction only if every new image satisfies the same extent and
// storage restrictions. Restart proof with that union to detect earlier uses
// of those images before their first certified clear.
static int rs_scope_rt(rs_scope_group *g, const p_set_rt *r, unsigned slots[9], unsigned *bound) {
    if (r->n<0 || r->n>8) return -1;
    const tex_t *new_targets[9]={0};
    unsigned present=0;
    *bound=0;
    for (unsigned i=0;i<9;i++) {
        slots[i]=0;
        const tex_t *t=i==8?r->depth:i<(unsigned)r->n?r->color[i]:NULL;
        if (!t) continue;
        present++;
        int viewed=i==8?r->has_dview:r->has_cview[i];
        const view_t *v=viewed?(i==8?&r->dview:&r->cview[i]):NULL;
        int match=rs_scope_match(g,t,v);
        if (match<0) return -1;
        if (match) {
            unsigned owner=1u<<(match-1);
            if (*bound&owner) return -1;
            *bound|=owner;
            slots[i]=owner;
        } else {
            new_targets[i]=t;
        }
    }
    if (!*bound || __builtin_popcount(*bound)==(int)present) return 0;

    rs_scope_group expanded=*g;
    for (unsigned i=0;i<9;i++) {
        const tex_t *t=new_targets[i];
        if (!t) continue;
        if ((i==8?r->has_dview:r->has_cview[i]) || t->d.width!=g->w || t->d.height!=g->h ||
            !rs_scope_addr(t) || !(t->pool->flags&NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS) ||
            t->has_layout || t->has_pkg_data || t->set_swz || rs_scope_match(&expanded,t,NULL)!=0)
            return -1;
        unsigned available=NX_RS_SCOPE_OWNER_MASK & ~expanded.mask;
        if (!available) return -1;
        unsigned index=(unsigned)__builtin_ctz(available);
        expanded.target[index]=t;
        expanded.addr[index]=rs_scope_addr(t);
        expanded.mask|=1u<<index;
    }
    *g=expanded;
    return 1;
}

// Map a complete clear to its bound owner, including both depth/stencil
// aspects where present. A batch of load-op clears must cover every
// uninitialized attachment before the pass can commit.
static unsigned rs_scope_clear_owner(const op_hdr *h, const uint8_t *a,
                                     const rs_scope_group *g, const unsigned slots[9],
                                     int full) {
    if (h->op==OP_CLEAR_COLOR && h->size-8>=sizeof(p_clear_color)) {
        const p_clear_color *c=(const p_clear_color *)a;
        if (c->index>=0 && c->index<8 && (!full || c->mask==15)) return slots[c->index];
    } else if (h->op==OP_CLEAR_DS && h->size-8>=sizeof(p_clear_ds) && slots[8]) {
        const p_clear_ds *c=(const p_clear_ds *)a;
        const vkfmt_t *f=vk_fmt(g->target[__builtin_ctz(slots[8])]->d.format);
        if (f && (!full || ((!f->depth || c->depth_mask) && (!f->stencil || (c->stencil_mask&255)==255))))
            return slots[8];
    }
    return 0;
}

static int rs_scope_clear_run(const uint8_t *p, const rs_scope_group *g, const unsigned slots[9],
                              const __typeof__(S) *look, unsigned required, unsigned ops, size_t bytes) {
    if (!rs_scope_full_scissor(look,g)) return 0;
    unsigned mask=0;
    for (unsigned i=0;i<9 && i<ops && bytes>=8;i++) {
        op_hdr h;
        memcpy(&h,p,sizeof h);
        if (h.size<8 || (h.size&7) || h.size>bytes) return 0;
        unsigned owner=rs_scope_clear_owner(&h,p+8,g,slots,1);
        if (!owner || (mask&owner)) return 0;
        mask|=owner;
        if ((mask&required)==required) return 1;
        bytes-=h.size;
        p+=h.size;
    }
    return 0;
}
static int rs_scope_indirect(const rs_scope_group *g,int indexed,uint64_t ind,uint64_t count,
                             int i,int max,int stride) {
    uint64_t bytes;
    return nx_rs_indirect_span(indexed,ind,count,i,max,stride,&bytes) &&
        (!count || rs_scope_raw(g,count,4,0)) &&
        (!ind || rs_scope_raw(g,ind,bytes,0));
}
static const uint8_t *rs_scope_scan_once(const uint8_t *p, rs_scope_group *g, unsigned initial,
                                         unsigned *budget, int *expanded, const char **reason) {
    __typeof__(S) look=S;
    unsigned slots[9];
    for (unsigned i=0;i<9;i++) slots[i]=initial&(1u<<i);
    nx_rs_scope life;
    if (!nx_rs_scope_init(&life,g->mask,initial)) return NULL;
    size_t bytes=0;unsigned crossed=0;
    g_rs_scope_crossed=0;g_rs_scope_overwrite=0;
    for (unsigned op=0;op<RS_SCOPE_OP_MAX && *budget;op++) {
        --*budget;
        op_hdr h;memcpy(&h,p,sizeof h);
        if (h.size<8 || (h.size&7) || h.size>RS_SCOPE_BYTES_MAX-bytes) return NULL;
        bytes+=h.size;const uint8_t *a=p+8;size_t n=h.size-8;
        // The submission owns every flattened handle in this array. Keep
        // the same byte/op bound across all handles, not per handle.
        if (h.op==OP_END) {
            if (h.size!=8 || !g_rs_scope_rest || crossed>=g_rs_scope_rest_n || crossed>=RS_SCOPE_HANDLE_MAX) {
                *reason="immutable replay window ended before lifetime termination";
                return NULL;
            }
            p=(const uint8_t *)(uintptr_t)g_rs_scope_rest[crossed++];
            if (!p) return NULL;
            continue;
        }
        int clearing=h.op==OP_CLEAR_COLOR || h.op==OP_CLEAR_DS;
        if (life.bound==life.owners && life.sampled && !life.discarded && clearing &&
            rs_scope_clear_run(p,g,slots,&look,life.owners,RS_SCOPE_OP_MAX-op,RS_SCOPE_BYTES_MAX-(bytes-h.size))) {
            g_rs_scope_crossed=crossed;g_rs_scope_overwrite=1;return p;
        }
#define RS_NEED(T) do { if (n<sizeof(T)) return NULL; } while (0)
        switch (h.op) {
        case OP_SET_RT: {
            RS_NEED(p_set_rt);
            int result=rs_scope_rt(g,(const p_set_rt *)a,slots,&life.bound);
            if (result) {
                *expanded=result>0;
                *reason="attachment compatibility";
                return NULL;
            }
            break; }
        case OP_CLEAR_COLOR: case OP_CLEAR_DS: {
            if (h.op==OP_CLEAR_COLOR) { RS_NEED(p_clear_color); }
            else { RS_NEED(p_clear_ds); }
            if (!life.bound) break;
            unsigned missing=life.bound & ~life.initialized;
            unsigned owner=rs_scope_clear_owner(&h,a,g,slots,0);
            unsigned full=rs_scope_clear_owner(&h,a,g,slots,1);
            if (!owner || !nx_rs_scope_clear_allowed(&life,owner) ||
                (missing && !rs_scope_clear_run(p,g,slots,&look,missing,RS_SCOPE_OP_MAX-op,RS_SCOPE_BYTES_MAX-(bytes-h.size)))) {
                *reason="attachment initialization or clear";
                return NULL;
            }
            if (full && rs_scope_full_scissor(&look,g)) nx_rs_scope_clear(&life,full);
            break; }
        case OP_RS_DISCARD: {
            RS_NEED(p_rs_discard);int i=((const p_rs_discard *)a)->index;
            if (i<-1 || i>=8) return NULL;
            unsigned owner=slots[i==-1?8:i];
            if (!owner) break;
            int result=nx_rs_scope_discard(&life,owner);
            if (!result) { *reason="lifetime termination"; return NULL; }
            if (result==2) {g_rs_scope_crossed=crossed;return p;}
            break; }
        case OP_BIND_PROGRAM: RS_NEED(p_bind_program);
            if (!rs_scope_program_proof(&look,p,(const p_bind_program *)a)) {
                *reason="shader snapshot";
                return NULL;
            }
            break;
        case OP_BIND_UBO: case OP_BIND_SSBO: {
            RS_NEED(p_bind_buf);const p_bind_buf *b=(const p_bind_buf *)a;
            if (b->stage<0 || b->stage>=6 || b->index<0 || b->index>=16) return NULL;
            if (h.op==OP_BIND_SSBO) look.ssbo[b->stage][b->index]=*b;
            else look.ubo[b->stage][b->index]=*b;
            break; }
        case OP_UPDATE_UB: {
            if (n<16) return NULL;
            uint64_t dst,len;memcpy(&dst,a,8);memcpy(&len,a+8,8);
            if (len>n-16 || (len && !rs_scope_raw(g,dst,len,0))) {
                *reason="buffer update overlap or unknown range";
                return NULL;
            }
            break; }
        case OP_COPY_B2B: {
            if (n<24) return NULL;
            uint64_t src,dst,len;memcpy(&src,a,8);memcpy(&dst,a+8,8);memcpy(&len,a+16,8);
            if (len && (!rs_scope_raw(g,src,len,0) || !rs_scope_raw(g,dst,len,0))) return NULL;
            break; }
        case OP_COPY_T2T: {
            RS_NEED(p_copy_t2t);const p_copy_t2t *c=(const p_copy_t2t *)a;
            int src=rs_scope_match(g,c->src,c->has_sv?&c->sv:NULL);
            int dst=rs_scope_match(g,c->dst,c->has_dv?&c->dv:NULL);
            if (src<0 || dst!=0) return NULL;
            if (src) {
                unsigned owner=1u<<(src-1);
                if (!resolution_display_copy_format(c) || !nx_rs_scope_sample(&life,owner)) return NULL;
                // Presentation can retain this generation after replay.
                life.exported|=owner;
            }
            break; }
        case OP_CLEAR_TEX: {
            RS_NEED(p_clear_tex);const p_clear_tex *c=(const p_clear_tex *)a;
            if (rs_scope_match(g,c->tex,c->has_view?&c->view:NULL)!=0) return NULL;
            break; }
        case OP_CLEAR_BUF: {
            RS_NEED(p_clear_buf);const p_clear_buf *c=(const p_clear_buf *)a;
            if (c->size && !rs_scope_raw(g,c->addr,c->size,0)) return NULL;
            break; }
        case OP_BIND_VBUF: {
            if (n<offsetof(p_bind_vbuf,r)) return NULL;
            const p_bind_vbuf *b=(const p_bind_vbuf *)a;
            if (b->first<0 || b->n<0 || b->first>16 || b->n>16-b->first ||
                n<offsetof(p_bind_vbuf,r)+(size_t)b->n*sizeof b->r[0]) return NULL;
            for (int i=0;i<b->n;i++) look.vbuf[b->first+i]=b->r[i];
            break; }
        case OP_BIND_TEX: case OP_BIND_TEX1: case OP_BIND_SAMP: case OP_BIND_IMG: {
            p_bind_handles tmp;
            if (h.op==OP_BIND_TEX1) { RS_NEED(p_bind_tex1); }
            else if (n<offsetof(p_bind_handles,h)) return NULL;
            const p_bind_handles *b=bind_rec(h.op,a,&tmp);
            int limit=h.op==OP_BIND_SAMP?128:h.op==OP_BIND_IMG?32:256;
            if (b->defer || b->stage<0 || b->stage>=6 || b->first<0 || b->n<0 || b->n>32 ||
                b->first>limit || b->n>limit-b->first ||
                (h.op!=OP_BIND_TEX1 && n<offsetof(p_bind_handles,h)+(size_t)b->n*8)) return NULL;
            if (h.op==OP_BIND_IMG) for (int i=0;i<b->n;i++) look.img[b->stage][b->first+i]=b->h[i];
            else if (h.op!=OP_BIND_SAMP) for (int i=0;i<b->n;i++) look.tex[b->stage][b->first+i]=b->h[i];
            break; }
        case OP_SCISSOR: RS_NEED(p_scissor);look.sc=*(const p_scissor *)a;look.sc_set=1;break;
        case OP_RAST_DISCARD: RS_NEED(p_rast_discard);look.rast_discard=((const p_rast_discard *)a)->on;break;
        case OP_DRAW: case OP_DRAW_IDX: {
            if (h.op==OP_DRAW) {
                p_draw tmp;if (n<offsetof(p_draw,ind) || n>sizeof tmp) return NULL;
                const p_draw *d=draw_rec(a,h.size,&tmp);
                if ((!d->ind && (d->count<=0 || d->first<0)) ||
                    !rs_scope_indirect(g,0,d->ind,d->mdi_cnt,d->mdi_i,d->mdi_max,d->mdi_stride)) return NULL;
            } else {
                p_draw_idx tmp;if (n<offsetof(p_draw_idx,ind) || n>sizeof tmp) return NULL;
                const p_draw_idx *d=draw_idx_rec(a,h.size,&tmp);
                if ((!d->ind && d->count<=0) || d->itype<0 || d->itype>2 ||
                    !rs_scope_indirect(g,1,d->ind,d->mdi_cnt,d->mdi_i,d->mdi_max,d->mdi_stride) ||
                    !rs_scope_raw(g,d->ibuf,(uint64_t)(unsigned)d->count<<d->itype,d->ind!=0)) return NULL;
            }
            unsigned sampled=0;
            if (!rs_scope_draw_proof(g,&look,life.bound!=0,&sampled)) {
                *reason="draw bindings or shader metadata";
                return NULL;
            }
            if (!nx_rs_scope_draw(&life,sampled)) {
                *reason="uninitialized attachment, feedback or ended lifetime";
                return NULL;
            }
            break; }
        case OP_DISPATCH: {
            RS_NEED(p_dispatch);unsigned sampled=0;
            if (!rs_scope_compute_proof(g,&look,(const p_dispatch *)a,&sampled) ||
                !nx_rs_scope_sample(&life,sampled)) {
                *reason="compute access";
                return NULL;
            }
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
static const uint8_t *rs_scope_scan(const uint8_t *next, rs_scope_group *group, const char **reason) {
    unsigned initial=group->mask;
    unsigned budget=2*RS_SCOPE_OP_MAX;
    int expanded;
    do {
        expanded=0;
        g_rs_scope_program_count=0;
        *reason="unproven command or open lifetime";
        const uint8_t *end=rs_scope_scan_once(next,group,initial,&budget,&expanded,reason);
        if (end) return end;
    } while (expanded && budget);
    return NULL;
}

static void rs_scope_reject(const char *reason) {
    if (!vk_pass_dump_user_active()) return;
    for (unsigned i=0;i<9;i++) {
        if (i<8 && (i>=(unsigned)S.rt_n || !S.rt_has_col[i])) continue;
        if (i==8 && !S.rt_has_dep) continue;
        const tex_t *t=i==8?&S.rt_dep:&S.rt_col[i];
        uint32_t w=t->d.width,h=t->d.height;
        // Small utility passes often clear first. Keep the first rejection
        // for the largest candidate so it describes scene-sized work.
        if ((uint64_t)w*h<=(uint64_t)g_capture_scope_w*g_capture_scope_h) continue;
        g_capture_scope_w=w;
        g_capture_scope_h=h;
        g_capture_scope_reason=reason;
    }
}

static void rs_scope_try_begin(const uint8_t *next) {
    // rec_copy makes immutable, flat handles with terminal OP_END records.
    // Both replay paths need a snapshot. Guest memory and nested calls are
    // not proof inputs. Diagnostic modes can inject extra accesses.
    if ((!g_rs_sync_snapshot && !nvn_replaying_job()) || S.pass_open || vk_rs_cache_has_scaled() || g_rc_skip || g_tf_on ||
        DM_GAME_RENDER_COND || g_bis_on || g_pd_state || g_x_listpasses || g_x_tracepass>=0 ||
        g_td_off>=0 || g_x_nflat || g_x_nobar || g_x_depth || g_x_color ||
        g_lrz_touch || g_ubolog_key || g_x_logfs || g_x_nodispbar || slot0_fill_on()) {
        rs_scope_reject("replay snapshot or render mode unavailable");
        return;
    }
    rs_scope_group group={0};nx_rs_group dims={0};
    for (unsigned i=0;i<9;i++) {
        if (i<8 && i<(unsigned)S.rt_n && S.rt_bad[i]) {
            rs_scope_reject("invalid color attachment");
            return;
        }
        if (i<8 && (i>=(unsigned)S.rt_n || !S.rt_has_col[i])) continue;
        if (i==8 && !S.rt_has_dep) continue;
        if (i==8?S.rt_has_dview:S.rt_has_cview[i]) {
            rs_scope_reject("attachment view");
            return;
        }
        const tex_t *t=i==8?&S.rt_dep:&S.rt_col[i];
        if (!(group.addr[i]=rs_scope_addr(t)) || !t->pool ||
            !(t->pool->flags&NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS) || t->set_swz ||
            t->has_layout || t->has_pkg_data) {
            rs_scope_reject("attachment storage provenance");
            return;
        }
        group.target[i]=t;group.mask|=1u<<i;
        nx_rs_add(&dims,t->d.width,t->d.height,0);
    }
    group.w=dims.w;group.h=dims.h;
    nx_rs_extent ext;
    nx_rs_raster raster;
    // Check the same scalar SupportBuffer round trip used by pass setup
    // before replacing any native attachment. Even an exact aspect ratio
    // can fail this check (for example, 1920x1080 at 60%).
    if (!nx_rs_plan(&dims,(int)g_rs_frame.target,V.props.limits.maxFramebufferWidth,V.props.limits.maxFramebufferHeight,&ext) ||
        !nx_rs_raster_init(&raster,group.w,group.h,ext.w,ext.h)) {
        rs_scope_reject("incompatible raster geometry");
        return;
    }
    if (!vk_rs_scope_freeze_begin()) {
        rs_scope_reject("descriptor snapshot unavailable");
        return;
    }
    g_rs_scope_program_count=0;
    const char *reason=NULL;
    const uint8_t *end=rs_scope_scan(next,&group,&reason);
    if (!end || !vk_rs_allocate_fresh_group(group.target,NX_RS_SCOPE_OWNER_MAX,ext.w,ext.h)) {
        rs_scope_reject(end ? "backing eligibility or allocation" : reason);
        g_rs_scope_program_count=0;g_rs_scope_crossed=0;g_rs_scope_overwrite=0;vk_rs_scope_freeze_end();return;
    }
    vk_rs_scope_freeze_seal();g_rs_scope_end=end;
    if (vk_pass_dump_user_active()) {
        uint64_t bytes=vk_rs_backing_bytes();
        if (bytes>g_capture_peak_rs_bytes) g_capture_peak_rs_bytes=bytes;
    }
    static unsigned accepted;
    if (++accepted<=16 || accepted%300==0)
        VKLOG("[resolution-scope] authorized raster/consumer/%s group %ux%u -> %ux%u (%u)",g_rs_scope_overwrite?"overwrite":"discard",group.w,group.h,ext.w,ext.h,accepted);
}

static void rs_scope_close(void) {
    vk_end_pass();
    if (!vk_rs_suspend_discarded_group()) die("[vk] resolution: certified lifetime publication failed");
    g_rs_scope_end=NULL;g_rs_scope_program_count=0;g_rs_scope_crossed=0;g_rs_scope_overwrite=0;
    vk_rs_scope_freeze_end();
}
