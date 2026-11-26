#ifndef NX_RS_SCOPE_H
#define NX_RS_SCOPE_H

enum { NX_RS_SCOPE_OWNER_MAX=16, NX_RS_SCOPE_OWNER_MASK=(1u<<NX_RS_SCOPE_OWNER_MAX)-1 };

// Owner identities are independent of the current color/depth slots. Only
// a certified initial clear and a terminal discard close each live range.
typedef struct {
    unsigned owners, bound, initialized, drawn, sampled, discarded, exported;
} nx_rs_scope;

static inline int nx_rs_scope_init(nx_rs_scope *s, unsigned owners, unsigned bound) {
    if (!bound || (bound & ~owners) || (owners & ~NX_RS_SCOPE_OWNER_MASK)) return 0;
    *s = (nx_rs_scope){.owners=owners, .bound=bound};
    return 1;
}

// Compute-only producers begin unbound. The caller must prove a complete
// one-output dispatch and all consumers before physical publication.
static inline int nx_rs_scope_init_compute(nx_rs_scope *s, unsigned owners) {
    if (!owners || (owners & ~NX_RS_SCOPE_OWNER_MASK)) return 0;
    *s=(nx_rs_scope){.owners=owners};
    return 1;
}
static inline int nx_rs_scope_compute_write(nx_rs_scope *s,unsigned owner) {
    if (!owner || (owner & (owner-1)) || (owner & (~s->owners | s->discarded | s->exported))) return 0;
    s->initialized|=owner;
    s->drawn|=owner;
    return 1;
}

// A proven full compute overwrite can terminate a single-owner lifetime.
// The new dispatch must not read any old owner contents. Exported backing
// generations remain protected by the existing presentation references.
static inline int nx_rs_scope_terminal_compute(const nx_rs_scope *s,unsigned owner,unsigned reads) {
    return owner && !(owner&(owner-1)) && owner==s->owners &&
        s->sampled && !s->discarded && !(reads&s->owners);
}

static inline int nx_rs_scope_clear_allowed(const nx_rs_scope *s, unsigned owner) {
    return owner && !(owner & (~s->bound | s->exported | s->discarded));
}

static inline int nx_rs_scope_clear(nx_rs_scope *s, unsigned owner) {
    if (!nx_rs_scope_clear_allowed(s, owner)) return 0;
    s->initialized |= owner;
    return 1;
}

static inline int nx_rs_scope_sample(nx_rs_scope *s, unsigned owners) {
    if (owners & (~s->drawn | s->discarded)) return 0;
    s->sampled |= owners;
    return 1;
}

static inline int nx_rs_scope_draw(nx_rs_scope *s, unsigned sampled) {
    if ((s->bound & (~s->initialized | s->exported | s->discarded | sampled)) ||
        !nx_rs_scope_sample(s, sampled)) return 0;
    s->drawn |= s->bound;
    return 1;
}

// 0 rejected, 1 partial discard, 2 all owners discarded.
static inline int nx_rs_scope_discard(nx_rs_scope *s, unsigned owner) {
    if (!owner || (owner & (~s->drawn | s->discarded))) return 0;
    s->discarded |= owner;
    if (s->discarded != s->owners) return 1;
    return s->sampled ? 2 : 0;
}
#endif
