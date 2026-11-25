#ifndef NX_RS_COMPUTE_TRANSFORM_H
#define NX_RS_COMPUTE_TRANSFORM_H
#include <stdint.h>
/* SPIR-V 1.0: imageStore(dst, gid.xy, texelFetch(src,gid.xy,0).bgra).
 * Fixed local size 8x8x1, combined sampler at set 0 binding 6,
 * RGBA32F storage image at set 0 binding 7. No runtime rewrite. */
#define NX_RS_T(op,n) (((n)<<16)|(op))
static const uint32_t nx_rs_transform_shader[]={
0x07230203,0x10000,0,40,0,
NX_RS_T(17,2),1, NX_RS_T(14,3),0,1,
NX_RS_T(15,6),5,20,0x6e69616d,0,10,
NX_RS_T(16,6),20,17,8,8,1,
NX_RS_T(71,4),10,11,28, NX_RS_T(71,4),11,34,0, NX_RS_T(71,4),11,33,7,
NX_RS_T(71,4),16,34,0, NX_RS_T(71,4),16,33,6,
NX_RS_T(19,2),1, NX_RS_T(21,4),2,32,0, NX_RS_T(23,4),3,2,3, NX_RS_T(23,4),4,2,2,
NX_RS_T(22,3),5,32, NX_RS_T(23,4),6,5,4,
NX_RS_T(25,9),7,5,1,0,0,0,2,1,
NX_RS_T(25,9),13,5,1,0,0,0,1,0,
NX_RS_T(27,3),14,13,
NX_RS_T(32,4),8,1,3, NX_RS_T(32,4),9,0,7, NX_RS_T(32,4),15,0,14,
NX_RS_T(33,3),12,1, NX_RS_T(43,4),2,17,0,
NX_RS_T(59,4),8,10,1, NX_RS_T(59,4),9,11,0, NX_RS_T(59,4),15,16,0,
NX_RS_T(54,5),1,20,0,12, NX_RS_T(248,2),21,
NX_RS_T(61,4),3,22,10, NX_RS_T(79,7),4,23,22,22,0,1,
NX_RS_T(61,4),14,25,16, NX_RS_T(100,4),13,26,25,
NX_RS_T(95,7),6,27,26,23,2,17,
NX_RS_T(79,9),6,28,27,27,2,1,0,3,
NX_RS_T(61,4),7,24,11, NX_RS_T(99,4),24,23,28, NX_RS_T(253,1), NX_RS_T(56,1)
};
#undef NX_RS_T
/* Distinct finite, exactly representable float bit patterns. The half-size
 * image samples the same logical coordinate lattice at stride two. */
static inline int nx_rs_transform_shape(unsigned w,unsigned h) {
    return (w==32 && h==32)||(w==16 && h==16);
}
static inline void nx_rs_transform_pixel(unsigned x,unsigned y,unsigned stride,uint32_t p[4]) {
    x*=stride; y*=stride;
    p[0]=0x3e800000u+(x<<16); p[1]=0x3f000000u+(y<<16);
    p[2]=0x3f400000u+((x*32+y)<<8); p[3]=0x3f800000u;
}
static inline int nx_rs_transform_source(uint32_t *words,unsigned w,unsigned h) {
    if (!words || !nx_rs_transform_shape(w,h)) return 0;
    unsigned stride=32/w;
    for (unsigned y=0;y<h;y++) for (unsigned x=0;x<w;x++)
        nx_rs_transform_pixel(x,y,stride,words+4*(y*w+x));
    return 1;
}
static inline unsigned nx_rs_transform_matches(const uint32_t *words,unsigned w,unsigned h) {
    if (!words || !nx_rs_transform_shape(w,h)) return 0;
    unsigned matches=0,stride=32/w;
    for (unsigned y=0;y<h;y++) for (unsigned x=0;x<w;x++) {
        uint32_t p[4]; nx_rs_transform_pixel(x,y,stride,p);
        const uint32_t *v=words+4*(y*w+x);
        matches+=(v[0]==p[2] && v[1]==p[1] && v[2]==p[0] && v[3]==p[3]);
    }
    return matches;
}
#endif
