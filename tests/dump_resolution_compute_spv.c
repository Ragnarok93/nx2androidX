#include <stdio.h>
#include <stdint.h>
#include "gfx_vk_resolution_compute_probe_shader.h"
#include "gfx_vk_resolution_compute_transform.h"

/* Write the exact embedded words in SPIR-V little-endian byte order. */
static int dump(const char *path,const uint32_t *words,size_t count) {
    FILE *f=fopen(path,"wb");
    if(!f) return 0;
    int ok=1;
    for(size_t i=0;i<count;i++) {
        uint32_t v=words[i];
        unsigned char b[4]={(unsigned char)v,(unsigned char)(v>>8),(unsigned char)(v>>16),(unsigned char)(v>>24)};
        if(fwrite(b,1,sizeof b,f)!=sizeof b) {ok=0;break;}
    }
    if(fclose(f)) ok=0;
    return ok;
}
int main(int argc,char **argv) {
    if(argc!=3) {fprintf(stderr,"usage: %s fill.spv transform.spv\n",argv[0]);return 2;}
    if(!dump(argv[1],nx_rs_probe_shader,sizeof nx_rs_probe_shader/sizeof nx_rs_probe_shader[0]) ||
       !dump(argv[2],nx_rs_transform_shader,sizeof nx_rs_transform_shader/sizeof nx_rs_transform_shader[0])) {
        fprintf(stderr,"failed to write shader binary\n");return 1;
    }
    return 0;
}
