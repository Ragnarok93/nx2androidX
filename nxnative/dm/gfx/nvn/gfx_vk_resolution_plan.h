// Backend-independent resolution planning. Logical NVN extents never change.
#ifndef NX_RESOLUTION_PLAN_H
#define NX_RESOLUTION_PLAN_H
#include <stdint.h>
enum {
    NX_RS_MIXED_EXTENT = 1u << 0, NX_RS_ALIAS = 1u << 1,
    NX_RS_STORAGE = 1u << 2, NX_RS_NON_2D = 1u << 3,
    NX_RS_MULTISAMPLE = 1u << 4, NX_RS_GUEST_DATA = 1u << 5,
    NX_RS_SPECIAL_VIEW = 1u << 6, NX_RS_INVALID = 1u << 7
};
typedef struct { uint32_t w, h; } nx_rs_extent;
typedef struct { uint32_t x, y, w, h; } nx_rs_rect;
typedef struct { uint32_t w, h, blockers, n; } nx_rs_group;
typedef struct { uint64_t frame; int valid; } nx_rs_timing;

static inline void nx_rs_add(nx_rs_group *g, uint32_t w, uint32_t h, uint32_t flags) {
    if (!w || !h) flags |= NX_RS_INVALID;
    if (!g->n) { g->w = w; g->h = h; }
    else if (w != g->w || h != g->h) flags |= NX_RS_MIXED_EXTENT;
    g->blockers |= flags;
    g->n++;
}
// Candidate planning only: no claim that consumers or shaders are compatible.
static inline int nx_rs_plan(const nx_rs_group *g, int pct, uint32_t mw, uint32_t mh, nx_rs_extent *e) {
    if (!g->n || g->blockers || pct < 50 || pct > 200 || !mw || !mh) return 0;
    uint64_t w = ((uint64_t)g->w * (unsigned)pct + 99) / 100;
    uint64_t h = ((uint64_t)g->h * (unsigned)pct + 99) / 100;
    if (!w || !h || w > mw || h > mh) return 0;
    *e = (nx_rs_extent){(uint32_t)w, (uint32_t)h};
    return 1;
}
// Map rectangle edges, not independently rounded width and offset. Conservative
// coverage rounds the start down and the exclusive end up, using actual extents.
static inline int nx_rs_map_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                  uint32_t lw, uint32_t lh, uint32_t pw, uint32_t ph, nx_rs_rect *r) {
    if (!lw || !lh || !pw || !ph || !w || !h || x > lw || y > lh || w > lw-x || h > lh-y) return 0;
    uint64_t x0 = (uint64_t)x*pw/lw, y0 = (uint64_t)y*ph/lh;
    uint64_t xe = (uint64_t)(x+w)*pw, ye = (uint64_t)(y+h)*ph;
    uint64_t x1 = xe/lw + (xe%lw != 0), y1 = ye/lh + (ye%lh != 0);
    *r = (nx_rs_rect){(uint32_t)x0, (uint32_t)y0, (uint32_t)(x1-x0), (uint32_t)(y1-y0)};
    return 1;
}
// GPU queries may become available several presents late. Each source frame
// contributes at most once; a late older query cannot enter the next window.
static inline int nx_rs_new_sample(nx_rs_timing *t, uint64_t frame) {
    if (t->valid && frame <= t->frame) return 0;
    t->frame = frame; t->valid = 1; return 1;
}
#endif
