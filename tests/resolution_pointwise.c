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
    // Identity stores need no redundant channel shuffle.
    uint32_t identity[N];
    memcpy(identity,nx_rs_transform_shader,swizzle*4);
    memcpy(identity+swizzle,nx_rs_transform_shader+swizzle+9,(N-swizzle-9)*4);
    identity[store-9+3]=27;local[0]=8;
    assert(nx_rs_compute_pointwise(identity,N-9,6,7,local));
    identity[store-9+3]=23;assert(!nx_rs_compute_pointwise(identity,N-9,6,7,local));
    // A complete vector affine transform is still independent per pixel.
    uint32_t math[N+32],count=0;
    for(unsigned i=0;i<N;) {
        if(i<5) {math[count++]=nx_rs_transform_shader[i++];continue;}
        unsigned wc=nx_rs_transform_shader[i]>>16,op=nx_rs_transform_shader[i]&65535u;
        if(op==54) {
            uint32_t c[]={ (4u<<16)|43,5,30,0x3f000000, (7u<<16)|44,6,31,30,30,30,30};
            memcpy(math+count,c,sizeof c);count+=sizeof c/4;
        }
        if(op==99) {
            uint32_t chain[]={ (5u<<16)|142,6,32,28,30, (5u<<16)|129,6,33,32,31};
            memcpy(math+count,chain,sizeof chain);count+=sizeof chain/4;
        }
        memcpy(math+count,nx_rs_transform_shader+i,wc*4);
        if(op==99) math[count+3]=33;
        count+=wc;i+=wc;
    }
    local[0]=8;
    assert(nx_rs_compute_pointwise(math,count,6,7,local));
    unsigned mul=0,add=0,constant=0;
    for(unsigned i=5;i<count;i+=math[i]>>16) {
        unsigned op=math[i]&65535u;
        if(op==142) mul=i;
        if(op==129) add=i;
        if(op==43 && math[i+2]==30) constant=i;
    }
    assert(mul && add && constant);
    math[mul+3]=23;assert(!nx_rs_compute_pointwise(math,count,6,7,local));math[mul+3]=28;
    math[add+3]=31;assert(!nx_rs_compute_pointwise(math,count,6,7,local));math[add+3]=32;
    math[constant+3]=0x7f800000;assert(!nx_rs_compute_pointwise(math,count,6,7,local));math[constant+3]=0x3f000000;
    math[mul+2]=28;assert(!nx_rs_compute_pointwise(math,count,6,7,local));math[mul+2]=32;
    math[add]=(5u<<16)|128;assert(!nx_rs_compute_pointwise(math,count,6,7,local));math[add]=(5u<<16)|129;
    // The store cannot select an unrelated coordinate or a constant vector.
    math[count-4]=31;assert(!nx_rs_compute_pointwise(math,count,6,7,local));
    return 0;
}
