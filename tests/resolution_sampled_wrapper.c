#include <assert.h>
#include "gfx_vk_resolution_view.h"
#ifdef OLD
#define nx_rs_sampled_owner_description_equal nx_rs_owner_description_equal
#endif
int main(void) {
    tex_t producer={.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA16F,.flags=NVN_TEXTURE_FLAGS_IMAGE,.width=64,.height=64,.depth=1,.levels=1},.L={.size=32768,.layer_size=32768,.bpb=8,.layers=1}};
    tex_t sampled=producer;sampled.d.flags=0;
    assert(nx_rs_sampled_owner_description_equal(&sampled,&producer));
    assert(!nx_rs_owner_description_equal(&sampled,&producer));
    assert(!nx_rs_sampled_owner_description_equal(&producer,&sampled));
    assert(nx_rs_sampled_owner_description_equal(&producer,&producer));
    sampled.d.flags=NVN_TEXTURE_FLAGS_COMPRESSIBLE;
    assert(nx_rs_sampled_owner_description_equal(&sampled,&producer));
    sampled.d.flags=NVN_TEXTURE_FLAGS_LINEAR;
    assert(!nx_rs_sampled_owner_description_equal(&sampled,&producer));
    sampled.d.flags=0;sampled.L.tileH++;
    assert(!nx_rs_sampled_owner_description_equal(&sampled,&producer));
    sampled=producer;sampled.d.flags=0;sampled.d.width--;
    assert(!nx_rs_sampled_owner_description_equal(&sampled,&producer));
    sampled=producer;sampled.d.flags=0;sampled.d.format=NVN_FORMAT_RGBA8;
    assert(!nx_rs_sampled_owner_description_equal(&sampled,&producer));
    sampled=producer;sampled.d.flags=0;sampled.has_pkg_data=1;
    assert(!nx_rs_sampled_owner_description_equal(&sampled,&producer));
    sampled.has_pkg_data=0;sampled.set_swz=1;
    assert(!nx_rs_sampled_owner_description_equal(&sampled,&producer));
    sampled.set_swz=0;sampled.has_layout=1;
    assert(!nx_rs_sampled_owner_description_equal(&sampled,&producer));
    producer.d.format=sampled.d.format=0x27;sampled.has_layout=0;
    assert(!nx_rs_sampled_owner_description_equal(&sampled,&producer));
    return 0;
}
