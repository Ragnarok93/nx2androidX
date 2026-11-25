#include <assert.h>
#include "gfx_vk_resolution_compute_proof.h"
#define I(op,n) (((n)<<16)|(op))
/* Valid-shaped SPIR-V: imageStore(output, gl_GlobalInvocationID.xy, vec4(1)). */
static const uint32_t shader[]={
0x07230203,0x10000,0,32,0,
I(17,2),1, I(14,3),0,1,
I(15,6),5,20,0x6e69616d,0,10,
I(16,6),20,17,8,8,1,
I(71,4),10,11,28, I(71,4),11,34,0, I(71,4),11,33,7,
I(19,2),1, I(21,4),2,32,0, I(23,4),3,2,3, I(23,4),4,2,2,
I(22,3),5,32, I(23,4),6,5,4,
I(25,9),7,5,1,0,0,0,2,1,
I(32,4),8,1,3, I(32,4),9,0,7,
I(33,3),12,1, I(43,4),5,13,0x3f800000, I(44,7),6,14,13,13,13,13,
I(59,4),8,10,1, I(59,4),9,11,0,
I(54,5),1,20,0,12, I(248,2),21,
I(61,4),3,22,10, I(79,7),4,23,22,22,0,1,
I(61,4),7,24,11, I(99,4),24,23,14, I(253,1), I(56,1)
};
#define N (sizeof shader/sizeof shader[0])
static unsigned find(uint32_t *w,unsigned op){for(unsigned i=5;i<N;i+=w[i]>>16)if((w[i]&65535)==op)return i;return 0;}
int main(void){
 uint32_t w[N], local[]={8,8,1}; memcpy(w,shader,sizeof w);
 assert(nx_rs_compute_constant_fill(w,N,7,local));
 assert(!nx_rs_compute_constant_fill(w,N,8,local));
 w[find(w,79)+6]=0; assert(!nx_rs_compute_constant_fill(w,N,7,local));
 memcpy(w,shader,sizeof w); w[find(w,99)+2]=22; assert(!nx_rs_compute_constant_fill(w,N,7,local));
 memcpy(w,shader,sizeof w); w[find(w,99)+3]=23; assert(!nx_rs_compute_constant_fill(w,N,7,local));
 memcpy(w,shader,sizeof w); w[find(w,253)]=I(224,1); assert(!nx_rs_compute_constant_fill(w,N,7,local));
 memcpy(w,shader,sizeof w); w[find(w,16)+3]=4; assert(!nx_rs_compute_constant_fill(w,N,7,local));
 assert(!nx_rs_compute_constant_fill(shader,N-1,7,local));
 return 0;
}
