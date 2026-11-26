#ifndef NX_RS_VIEW_H
#define NX_RS_VIEW_H
#include "nvn_obj.h"
#include "gfx_vk_resolution_storage_format.h"
// A view is eligible only when it leaves the single physical subresource
// and its sampling semantics unchanged. Never normalize guest objects in place.
static inline int nx_rs_view_equivalent(const tex_t *t,const view_t *v) {
    if (!t) return 0;
    if (!v) return 1;
    if (t->d.target!=NVN_TEXTURE_TARGET_2D || t->d.levels!=1 || t->d.depth!=1 || t->d.samples>1) return 0;
    // The Vulkan view resolver treats count zero as all remaining levels
    // or layers. With exactly one subresource, base zero is equivalent.
    if (v->set_levels && (v->base_level || v->nlevels>1)) return 0;
    if (v->set_layers && (v->min_layer || v->nlayers>1)) return 0;
    if (v->set_format && v->format!=t->d.format) return 0;
    if (v->set_target && v->target!=t->d.target) return 0;
    if (v->set_swizzle && (t->set_swz || v->swz[0]!=NVN_TEXTURE_SWIZZLE_R || v->swz[1]!=NVN_TEXTURE_SWIZZLE_G ||
        v->swz[2]!=NVN_TEXTURE_SWIZZLE_B || v->swz[3]!=NVN_TEXTURE_SWIZZLE_A)) return 0;
    if (v->set_dsmode && v->dsmode!=(t->stencil?NVN_TEXTURE_DEPTH_STENCIL_MODE_STENCIL:NVN_TEXTURE_DEPTH_STENCIL_MODE_DEPTH)) return 0;
    return 1;
}
// Depth/stencil images cannot be color storage images. Match the native
// cache's IMAGE-hint normalization, while retaining exact depth/stencil
// format, selected aspect and guest layout.
static inline int nx_rs_depth_stencil_format(int format) {
    switch (format) {
    case NVN_FORMAT_STENCIL8: case NVN_FORMAT_DEPTH16: case NVN_FORMAT_DEPTH24:
    case NVN_FORMAT_DEPTH32F: case NVN_FORMAT_DEPTH24_STENCIL8:
    case NVN_FORMAT_DEPTH32F_STENCIL8: return 1;
    default: return 0;
    }
}
// All storage-affecting flags and the actual guest memory layout must match.
static inline int nx_rs_owner_description_equal(const tex_t *a,const tex_t *b) {
    nvn_texdesc x=a->d,y=b->d;
    x.flags&=~NVN_TEXTURE_FLAGS_COMPRESSIBLE;
    y.flags&=~NVN_TEXTURE_FLAGS_COMPRESSIBLE;
    if (nx_rs_depth_stencil_format(x.format)) x.flags&=~NVN_TEXTURE_FLAGS_IMAGE;
    if (nx_rs_depth_stencil_format(y.format)) y.flags&=~NVN_TEXTURE_FLAGS_IMAGE;
    return !memcmp(&x,&y,sizeof x) && a->stencil==b->stencil &&
        a->L.size==b->L.size && a->L.layer_size==b->L.layer_size &&
        a->L.align==b->L.align && a->L.layers==b->L.layers && a->L.bpb==b->L.bpb &&
        a->L.tileH==b->L.tileH && a->L.tileD==b->L.tileD;
}
// Match the native cache's one-way sampled-wrapper rule. A storage write
// never acquires IMAGE usage through a wrapper, and a distinct layout or
// CPU-initialized interpretation is not the same physical owner.
static inline int nx_rs_sampled_owner_description_equal(const tex_t *sample,const tex_t *owner) {
    if (nx_rs_owner_description_equal(sample,owner)) return 1;
    if ((sample->d.flags&NVN_TEXTURE_FLAGS_IMAGE) || !(owner->d.flags&NVN_TEXTURE_FLAGS_IMAGE) ||
        !nx_rs_storage_spv_format(sample->d.format) || sample->has_layout || sample->has_pkg_data ||
        sample->set_swz || owner->has_layout || owner->has_pkg_data || owner->set_swz) return 0;
    tex_t normalized=*sample;
    normalized.d.flags|=NVN_TEXTURE_FLAGS_IMAGE;
    return nx_rs_owner_description_equal(&normalized,owner);
}
#endif
