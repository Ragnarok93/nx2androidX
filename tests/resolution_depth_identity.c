#include <assert.h>
#include "gfx_vk_resolution_view.h"
int main(void) {
    const int formats[]={NVN_FORMAT_STENCIL8,NVN_FORMAT_DEPTH16,NVN_FORMAT_DEPTH24,NVN_FORMAT_DEPTH32F,NVN_FORMAT_DEPTH24_STENCIL8,NVN_FORMAT_DEPTH32F_STENCIL8};
    for (unsigned i=0;i<sizeof formats/sizeof formats[0];i++) {
        tex_t a={.d={.target=NVN_TEXTURE_TARGET_2D,.format=formats[i],.width=64,.height=64,.depth=1,.levels=1},.L={.size=16384,.layer_size=16384,.layers=1}};
        tex_t b=a;b.d.flags=NVN_TEXTURE_FLAGS_IMAGE;
        assert(nx_rs_owner_description_equal(&a,&b));
        assert(nx_rs_owner_description_equal(&b,&a));
        assert(nx_rs_sampled_owner_description_equal(&a,&b));
        b.stencil=1;assert(!nx_rs_owner_description_equal(&a,&b));b.stencil=0;
        b.L.tileH++;assert(!nx_rs_owner_description_equal(&a,&b));b.L.tileH--;
        b.d.flags|=NVN_TEXTURE_FLAGS_LINEAR;assert(!nx_rs_owner_description_equal(&a,&b));
        b.d.flags=NVN_TEXTURE_FLAGS_IMAGE;b.d.width--;assert(!nx_rs_owner_description_equal(&a,&b));
    }
    tex_t color={.d={.format=NVN_FORMAT_RGBA8}},image=color;
    image.d.flags=NVN_TEXTURE_FLAGS_IMAGE;
    assert(!nx_rs_owner_description_equal(&color,&image));
    assert(nx_rs_sampled_owner_description_equal(&color,&image));
    return 0;
}
