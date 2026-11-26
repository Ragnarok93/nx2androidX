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
    return 0;
}
