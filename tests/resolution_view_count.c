#include <assert.h>
#include "gfx_vk_resolution_view.h"
int main(void) {
    tex_t t={.d={.target=NVN_TEXTURE_TARGET_2D,.levels=1,.depth=1,.samples=1,.format=NVN_FORMAT_RGBA8}};
    view_t v={.set_levels=1,.set_layers=1};
    assert(nx_rs_view_equivalent(&t,&v));
    v.nlevels=1;assert(nx_rs_view_equivalent(&t,&v));
    v.nlevels=0;v.nlayers=1;assert(nx_rs_view_equivalent(&t,&v));
    v.nlayers=0;v.base_level=1;assert(!nx_rs_view_equivalent(&t,&v));
    v.base_level=0;v.min_layer=1;assert(!nx_rs_view_equivalent(&t,&v));
    v.min_layer=0;v.nlevels=2;assert(!nx_rs_view_equivalent(&t,&v));
    v.nlevels=0;v.nlayers=2;assert(!nx_rs_view_equivalent(&t,&v));
    v.nlayers=0;t.d.levels=2;assert(!nx_rs_view_equivalent(&t,&v));
    t.d.levels=1;t.d.depth=2;assert(!nx_rs_view_equivalent(&t,&v));
    t.d.depth=1;t.d.samples=4;assert(!nx_rs_view_equivalent(&t,&v));
    t.d.samples=1;
    // Explicit default channels compose with the format exactly like no
    // swizzle in the real Vulkan sample-view resolver.
    v.set_swizzle=1;
    v.swz[0]=NVN_TEXTURE_SWIZZLE_R;v.swz[1]=NVN_TEXTURE_SWIZZLE_G;
    v.swz[2]=NVN_TEXTURE_SWIZZLE_B;v.swz[3]=NVN_TEXTURE_SWIZZLE_A;
    t.set_swz=1;
    t.swz=NVN_TEXTURE_SWIZZLE_R | (NVN_TEXTURE_SWIZZLE_G<<3) |
        (NVN_TEXTURE_SWIZZLE_B<<6) | (NVN_TEXTURE_SWIZZLE_A<<9);
    assert(nx_rs_texture_identity_swizzle(&t));
    assert(nx_rs_view_equivalent(&t,&v));
    t.swz^=1;
    assert(!nx_rs_texture_identity_swizzle(&t));
    assert(!nx_rs_view_equivalent(&t,&v));
    t.swz^=1;v.swz[3]=NVN_TEXTURE_SWIZZLE_R;
    assert(!nx_rs_view_equivalent(&t,&v));
    return 0;
}
