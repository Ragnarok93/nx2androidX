#include <assert.h>
#include "gfx_vk_resolution_compute_proof.h"
#include "gfx_vk_resolution_compute_probe_shader.h"
#define N (sizeof nx_rs_probe_shader/sizeof nx_rs_probe_shader[0])
#ifdef OLD
static int nx_rs_compute_constant_fill_format(const uint32_t *w,uint32_t n,uint32_t binding,const uint32_t local[3],unsigned format) {
    (void)format;return nx_rs_compute_constant_fill(w,n,binding,local);
}
#endif
int main(void) {
    uint32_t w[N],local[]={8,8,1};memcpy(w,nx_rs_probe_shader,sizeof w);
    unsigned f=0,coord=0;
    for(unsigned i=5;i<N;i+=w[i]>>16) {
        if((w[i]&65535u)==25) f=i+8;
        if((w[i]&65535u)==79) coord=i+5;
    }
    assert(f && coord);
    assert(nx_rs_compute_constant_fill(w,N,7,local));
    const unsigned formats[]={2,4,1};
    for(unsigned i=0;i<3;i++) {
        w[f]=formats[i];
        assert(nx_rs_compute_constant_fill_format(w,N,7,local,formats[i]));
        assert(!nx_rs_compute_constant_fill_format(w,N,7,local,formats[(i+1)%3]));
        assert(!nx_rs_compute_constant_fill_format(w,N,8,local,formats[i]));
        w[coord]=1;assert(!nx_rs_compute_constant_fill_format(w,N,7,local,formats[i]));w[coord]=0;
        assert(!nx_rs_compute_constant_fill_format(w,N-1,7,local,formats[i]));
    }
    w[f]=30;assert(!nx_rs_compute_constant_fill_format(w,N,7,local,30));
    w[f]=0;assert(!nx_rs_compute_constant_fill_format(w,N,7,local,0));
    w[f]=2;assert(!nx_rs_compute_constant_fill(w,N,7,local));
    return 0;
}
