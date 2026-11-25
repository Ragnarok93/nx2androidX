#include <assert.h>
#include "gfx_vk_resolution_compute_proof.h"
#define I(op,n) (((n)<<16)|(op))
#include "gfx_vk_resolution_compute_probe_shader.h"
#define shader nx_rs_probe_shader
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
 uint32_t pixels[4096]={0};
 assert(nx_rs_probe_matches(pixels,1024)==0);
 for(unsigned i=0;i<4096;i++) pixels[i]=0x3f800000u;
 assert(nx_rs_probe_matches(pixels,1024)==1024);
 assert(nx_rs_probe_matches(pixels,256)==256);
 pixels[4095]=0; assert(nx_rs_probe_matches(pixels,1024)==1023);
 pixels[3]=0; assert(nx_rs_probe_matches(pixels,256)==255);
 assert(nx_rs_probe_matches(pixels,1025)==0);
 assert(nx_rs_probe_matches(0,256)==0);
 assert(nx_rs_probe_matches(pixels,0)==0);
 return 0;
}
