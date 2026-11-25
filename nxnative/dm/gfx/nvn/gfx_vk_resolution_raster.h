// Logical coordinates are mapped only for explicitly scaled attachments.
#ifndef NX_RS_RASTER_H
#define NX_RS_RASTER_H
#include <stdint.h>
typedef struct { uint32_t lw, lh, pw, ph; float scale; } nx_rs_raster;
typedef struct { float x, y, w, h; } nx_rs_viewport;
typedef struct { uint32_t x, y, w, h; } nx_rs_raster_rect;
static inline int nx_rs_raster_init(nx_rs_raster *r, uint32_t lw, uint32_t lh, uint32_t pw, uint32_t ph) {
    if (!r || !lw || !lh || !pw || !ph || lw > INT32_MAX || lh > INT32_MAX || pw > INT32_MAX || ph > INT32_MAX ||
        (uint64_t)pw * lh != (uint64_t)ph * lw) return 0;
    float s = (float)pw / (float)lw;
    if (!(s >= 0.5f && s <= 2.0f) || (float)pw / s != (float)lw || (float)ph / s != (float)lh) return 0;
    *r = (nx_rs_raster){lw, lh, pw, ph, s};
    return 1;
}
static inline void nx_rs_scale_viewport(const nx_rs_raster *r, nx_rs_viewport *v) {
    if (r->scale == 1.0f) return;
    v->x *= r->scale; v->y *= r->scale; v->w *= r->scale; v->h *= r->scale;
}
static inline int nx_rs_clip_scissor(const nx_rs_raster *r, int32_t x, int32_t y, uint32_t w, uint32_t h, int bottom_up, nx_rs_raster_rect *out) {
    if (!r || !out || !w || !h) return 0;
    int64_t x0=x, x1=(int64_t)x+w, y0=y, y1=(int64_t)y+h;
    if (bottom_up) { int64_t t=(int64_t)r->lh-y1; y1=(int64_t)r->lh-y0; y0=t; }
    if (x0<0) x0=0;
    if (y0<0) y0=0;
    if (x1>r->lw) x1=r->lw;
    if (y1>r->lh) y1=r->lh;
    if (x1<=x0 || y1<=y0) return 0;
    uint64_t a=(uint64_t)x0*r->pw/r->lw, b=(uint64_t)y0*r->ph/r->lh;
    uint64_t c=((uint64_t)x1*r->pw+r->lw-1)/r->lw, d=((uint64_t)y1*r->ph+r->lh-1)/r->lh;
    *out=(nx_rs_raster_rect){(uint32_t)a,(uint32_t)b,(uint32_t)(c-a),(uint32_t)(d-b)};
    return 1;
}
#endif
