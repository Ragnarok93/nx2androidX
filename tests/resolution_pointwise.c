#include <assert.h>
#include <string.h>
#include "gfx_vk_resolution_pointwise.h"
#define N (sizeof nx_rs_transform_shader/sizeof nx_rs_transform_shader[0])
static unsigned find(unsigned op,unsigned wc) {
    for (unsigned i=5;i<N;i+=nx_rs_transform_shader[i]>>16)
        if (nx_rs_transform_shader[i]==((wc<<16)|op)) return i;
    assert(0);return 0;
}
int main(void) {
    uint32_t w[N],local[]={8,8,1};
    memcpy(w,nx_rs_transform_shader,sizeof w);
    assert(nx_rs_compute_pointwise(w,N,6,7,local));
    assert(!nx_rs_compute_pointwise(w,N,7,6,local));
    assert(!nx_rs_compute_pointwise(w,N-1,6,7,local));
    assert(!nx_rs_compute_pointwise(w,N,6,6,local));
    unsigned fetch=find(95,7),swizzle=find(79,9),coord=find(79,7),store=find(99,4),mode=find(16,6);
    w[fetch+4]=24;assert(!nx_rs_compute_pointwise(w,N,6,7,local));w[fetch+4]=23;
    w[fetch+5]=4;assert(!nx_rs_compute_pointwise(w,N,6,7,local));w[fetch+5]=2;
    w[coord+5]=1;assert(!nx_rs_compute_pointwise(w,N,6,7,local));w[coord+5]=0;
    w[store+2]=22;assert(!nx_rs_compute_pointwise(w,N,6,7,local));w[store+2]=23;
    w[swizzle+5]=0;assert(nx_rs_compute_pointwise(w,N,6,7,local));
    w[swizzle+5]=4;assert(!nx_rs_compute_pointwise(w,N,6,7,local));w[swizzle+5]=0;
    local[0]=16;assert(!nx_rs_compute_pointwise(w,N,6,7,local));w[mode+3]=16;
    assert(nx_rs_compute_pointwise(w,N,6,7,local));
    for (unsigned i=5;i<N;i+=w[i]>>16) {
        unsigned op=w[i]&65535u,wc=w[i]>>16,ids=nx_rs_pointwise_ids(op);
        if (op==71 && w[i+2]==33) w[i+3]+=10;
        for(unsigned j=1;j<wc;j++) if(ids&(1u<<j)) w[i+j]+=40;
    }
    w[3]=100;
    assert(nx_rs_compute_pointwise(w,N,16,17,local));
    w[fetch+4]=62;assert(!nx_rs_compute_pointwise(w,N,16,17,local));
    return 0;
}
