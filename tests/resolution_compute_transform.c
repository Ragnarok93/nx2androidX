#include <assert.h>
#include <string.h>
#include "gfx_vk_resolution_compute_transform.h"
int main(void) {
 uint32_t src[4096],out[4096],half[1024];
 assert(nx_rs_transform_source(src,32,32));
 assert(nx_rs_transform_source(half,16,16));
 for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++)assert(!memcmp(half+4*(y*16+x),src+4*(2*y*32+2*x),16));
 assert(nx_rs_transform_matches(src,32,32)==0);
 for(unsigned i=0;i<1024;i++){out[4*i]=src[4*i+2];out[4*i+1]=src[4*i+1];out[4*i+2]=src[4*i];out[4*i+3]=src[4*i+3];}
 assert(nx_rs_transform_matches(out,32,32)==1024);
 out[4095]=0;assert(nx_rs_transform_matches(out,32,32)==1023);
 for(unsigned i=0;i<256;i++){out[4*i]=half[4*i+2];out[4*i+1]=half[4*i+1];out[4*i+2]=half[4*i];out[4*i+3]=half[4*i+3];}
 assert(nx_rs_transform_matches(out,16,16)==256);
 uint32_t t=out[0];out[0]=out[4];out[4]=t;assert(nx_rs_transform_matches(out,16,16)==254);
 assert(!nx_rs_transform_source(src,16,32));assert(!nx_rs_transform_matches(out,33,33));assert(!nx_rs_transform_matches(0,16,16));
 return 0;
}
