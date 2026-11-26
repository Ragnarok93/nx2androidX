#include <assert.h>
#include <string.h>
#include "gfx_vk_resolution_pointwise.h"
#define N (sizeof nx_rs_transform_shader/sizeof nx_rs_transform_shader[0])
int main(void) {
    uint32_t w[N], local[]={8,8,1};memcpy(w,nx_rs_transform_shader,sizeof w);
    unsigned format=0;for(unsigned i=5;i<N;i+=w[i]>>16) if((w[i]&65535u)==25 && w[i+7]==2) format=i+8;
    assert(format);
    const unsigned nvn[]={0x2e,0x29,0x25},spv[]={1,2,4};
    for(unsigned i=0;i<3;i++) {
        assert(nx_rs_storage_spv_format(nvn[i])==spv[i]);
        w[format]=spv[i];
        assert(nx_rs_compute_pointwise_format(w,N,6,7,local,spv[i]));
        assert(!nx_rs_compute_pointwise_format(w,N,6,7,local,spv[(i+1)%3]));
    }
    w[format]=0;assert(!nx_rs_compute_pointwise_format(w,N,6,7,local,0));
    w[format]=30;assert(!nx_rs_compute_pointwise_format(w,N,6,7,local,30));
    assert(!nx_rs_storage_spv_format(0x27)); /* RGBA8UI */
    assert(!nx_rs_storage_spv_format(0x38)); /* RGBA8 sRGB */
    assert(!nx_rs_storage_spv_format(0x35)); /* D24S8 */
    return 0;
}
