#ifndef NX_RS_TRANSFER_H
#define NX_RS_TRANSFER_H
#include "gfx_vk_resolution_view.h"
// Only a full, single-level, same-format color copy into a display
// texture can be a reconstruction consumer. Raw copies to arbitrary
// guest storage retain native semantics. Format features are checked
// separately against the actual Vulkan physical device.
static inline int nx_rs_display_copy_shape(const p_copy_t2t *c) {
    if (!c || !c->src || !c->dst || (c->flags&~3u)) return 0;
    if (!nx_rs_view_equivalent(c->src,c->has_sv?&c->sv:NULL) ||
        !nx_rs_view_equivalent(c->dst,c->has_dv?&c->dv:NULL)) return 0;
    const tex_t *s=c->src,*d=c->dst;
    if (s==d || !(d->d.flags&NVN_TEXTURE_FLAGS_DISPLAY) ||
        s->set_swz || d->set_swz || s->stencil || d->stencil ||
        s->has_layout || d->has_layout || s->has_pkg_data || d->has_pkg_data ||
        s->d.target!=NVN_TEXTURE_TARGET_2D || d->d.target!=NVN_TEXTURE_TARGET_2D ||
        s->d.levels!=1 || d->d.levels!=1 || s->d.depth!=1 || d->d.depth!=1 ||
        s->d.samples>1 || d->d.samples>1 || s->d.format!=d->d.format ||
        !s->d.width || !s->d.height || !d->d.width || !d->d.height ||
        (d->d.flags&(NVN_TEXTURE_FLAGS_LINEAR|NVN_TEXTURE_FLAGS_SPARSE|NVN_TEXTURE_FLAGS_LINEAR_RENDER_TARGET))) return 0;
    const NVNcopyRegion *a=&c->sr,*b=&c->dr;
    return !a->xoffset && !a->yoffset && !a->zoffset && !b->xoffset && !b->yoffset && !b->zoffset &&
        a->width>0 && a->height>0 && b->width>0 && b->height>0 && a->depth==1 && b->depth==1 &&
        (uint32_t)a->width==s->d.width && (uint32_t)a->height==s->d.height &&
        (uint32_t)b->width==d->d.width && (uint32_t)b->height==d->d.height;
}
#endif
