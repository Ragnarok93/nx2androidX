#ifndef NX_RS_OWNERSHIP_H
#define NX_RS_OWNERSHIP_H
#include <stdint.h>
#include <stddef.h>
enum { NX_RO_ATTACH=1u, NX_RO_SAMPLE=2u, NX_RO_ALIAS=4u, NX_RO_RAW=8u,
       NX_RO_HOST=16u, NX_RO_STORAGE=32u, NX_RO_VIEW=64u, NX_RO_UNKNOWN=128u,
       NX_RO_COMPUTE=256u, NX_RO_PRESENT=512u, NX_RO_INTRINSIC=1024u };
#define NX_RO_BLOCKERS (NX_RO_ALIAS|NX_RO_RAW|NX_RO_HOST|NX_RO_STORAGE|NX_RO_VIEW|NX_RO_UNKNOWN|NX_RO_COMPUTE|NX_RO_PRESENT|NX_RO_INTRINSIC)
enum { NX_RO_MAX=4096, NX_RO_HASH=8192 };
typedef struct { uintptr_t key; uint32_t parent, flags; uint8_t live; } nx_ro_node;
typedef struct { nx_ro_node nodes[NX_RO_MAX]; uint32_t index[NX_RO_HASH], count, exhausted; } nx_ro_graph;
static inline uint32_t nx_ro_root(nx_ro_graph *g, uint32_t id) {
    while (g->nodes[id-1].parent != id) {
        uint32_t p=g->nodes[id-1].parent;
        g->nodes[id-1].parent=g->nodes[p-1].parent;
        id=g->nodes[id-1].parent;
    }
    return id;
}
static inline uint32_t nx_ro_find(nx_ro_graph *g, uintptr_t key, int create) {
    if (!key || g->exhausted) return 0;
    uint64_t h=(uint64_t)key; h ^= h>>33; h *= UINT64_C(0xff51afd7ed558ccd); h ^= h>>33;
    uint32_t i=(uint32_t)h&(NX_RO_HASH-1);
    while (g->index[i]) {
        uint32_t id=g->index[i]; nx_ro_node *n=&g->nodes[id-1];
        if (n->live && n->key==key) return id;
        i=(i+1)&(NX_RO_HASH-1);
    }
    if (!create) return 0;
    if (g->count==NX_RO_MAX) { g->exhausted=1; return 0; }
    uint32_t id=++g->count; g->index[i]=id;
    g->nodes[id-1]=(nx_ro_node){key,id,0,1}; return id;
}
static inline uint32_t nx_ro_note(nx_ro_graph *g, uintptr_t key, uint32_t flags) {
    uint32_t id=nx_ro_find(g,key,1);
    if (id) { g->nodes[id-1].flags |= flags; g->nodes[nx_ro_root(g,id)-1].flags |= flags; }
    return id;
}
static inline void nx_ro_merge(nx_ro_graph *g, uint32_t a, uint32_t b) {
    if (!a || !b || g->exhausted) return;
    a=nx_ro_root(g,a); b=nx_ro_root(g,b);
    if (a==b) return;
    if (a>b) { uint32_t t=a;a=b;b=t; }
    g->nodes[a-1].flags |= g->nodes[b-1].flags;
    g->nodes[b-1].parent=a;
}
static inline uint32_t nx_ro_flags(nx_ro_graph *g, uint32_t id) {
    return !id || id>g->count || g->exhausted ? NX_RO_UNKNOWN : g->nodes[nx_ro_root(g,id)-1].flags;
}
static inline void nx_ro_drop(nx_ro_graph *g, uintptr_t key) {
    uint32_t id=nx_ro_find(g,key,0); if (id) g->nodes[id-1].live=0;
}
/* Necessary admission condition only: observed history cannot prove future consumers. */
static inline int nx_ro_group_admit(nx_ro_graph *g, const uintptr_t *keys, unsigned n) {
    if (!g || g->exhausted || !keys || !n || n>9) return 0;
    uint32_t ids[9]={0}, root=0;
    unsigned count=0;
    for (unsigned i=0;i<n;i++) if (keys[i]) {
        uint32_t id=nx_ro_find(g,keys[i],0);
        if (!id || !(g->nodes[id-1].flags&NX_RO_ATTACH) || (nx_ro_flags(g,id)&NX_RO_BLOCKERS)) return 0;
        uint32_t r=nx_ro_root(g,id);
        if (root && root!=r) return 0;
        root=r;
        for (unsigned j=0;j<count;j++) if (ids[j]==id) return 0;
        ids[count++]=id;
    }
    if (!count) return 0;
    for (uint32_t i=1;i<=g->count;i++) if (g->nodes[i-1].live && nx_ro_root(g,i)==root) {
        unsigned j=0;
        while (j<count && ids[j]!=i) j++;
        if (j==count) return 0;
    }
    return 1;
}
#endif
