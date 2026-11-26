#ifndef NX_RS_VIEW_H
#define NX_RS_VIEW_H
#include "nvn_obj.h"
// A view is eligible only when it leaves the single physical subresource
// and its sampling semantics unchanged. Never normalize guest objects in place.
static inline int nx_rs_view_equivalent(const tex_t *t,const view_t *v) {
    if (!t) return 0;
    if (!v) return 1;
    if (t->d.target!=NVN_TEXTURE_TARGET_2D || t->d.levels!=1 || t->d.depth!=1 || t->d.samples>1) return 0;
    if (v->set_levels && (v->base_level || v->nlevels!=1)) return 0;
    if (v->set_layers && (v->min_layer || v->nlayers!=1)) return 0;
    if (v->set_format && v->format!=t->d.format) return 0;
    if (v->set_target && v->target!=t->d.target) return 0;
    if (v->set_swizzle && (t->set_swz || v->swz[0]!=NVN_TEXTURE_SWIZZLE_R || v->swz[1]!=NVN_TEXTURE_SWIZZLE_G ||
        v->swz[2]!=NVN_TEXTURE_SWIZZLE_B || v->swz[3]!=NVN_TEXTURE_SWIZZLE_A)) return 0;
    if (v->set_dsmode && v->dsmode!=(t->stencil?NVN_TEXTURE_DEPTH_STENCIL_MODE_STENCIL:NVN_TEXTURE_DEPTH_STENCIL_MODE_DEPTH)) return 0;
    return 1;
}
#endif
