#ifndef NX_RS_COMPUTE_PROBE_SHADER_H
#define NX_RS_COMPUTE_PROBE_SHADER_H
#include <stdint.h>
/* Same fixed-local-size RGBA32F constant-fill shader as the host proof fixture.
 * Set 0 binding 7, GlobalInvocationId.xy, vec4(1). */
#define NX_RS_I(op,n) (((n)<<16)|(op))
static const uint32_t nx_rs_probe_shader[]={
0x07230203,0x10000,0,32,0,
NX_RS_I(17,2),1, NX_RS_I(14,3),0,1,
NX_RS_I(15,6),5,20,0x6e69616d,0,10,
NX_RS_I(16,6),20,17,8,8,1,
NX_RS_I(71,4),10,11,28, NX_RS_I(71,4),11,34,0, NX_RS_I(71,4),11,33,7,
NX_RS_I(19,2),1, NX_RS_I(21,4),2,32,0, NX_RS_I(23,4),3,2,3, NX_RS_I(23,4),4,2,2,
NX_RS_I(22,3),5,32, NX_RS_I(23,4),6,5,4,
NX_RS_I(25,9),7,5,1,0,0,0,2,1,
NX_RS_I(32,4),8,1,3, NX_RS_I(32,4),9,0,7,
NX_RS_I(33,3),12,1, NX_RS_I(43,4),5,13,0x3f800000, NX_RS_I(44,7),6,14,13,13,13,13,
NX_RS_I(59,4),8,10,1, NX_RS_I(59,4),9,11,0,
NX_RS_I(54,5),1,20,0,12, NX_RS_I(248,2),21,
NX_RS_I(61,4),3,22,10, NX_RS_I(79,7),4,23,22,22,0,1,
NX_RS_I(61,4),7,24,11, NX_RS_I(99,4),24,23,14, NX_RS_I(253,1), NX_RS_I(56,1)
};
#undef NX_RS_I
static inline unsigned nx_rs_probe_matches(const uint32_t *words,unsigned pixels) {
    if (!words || pixels>1024) return 0;
    unsigned matches=0;
    for (unsigned i=0;i<pixels;i++) {
        const uint32_t *p=words+4*i;
        matches+=(p[0]==0x3f800000u && p[1]==0x3f800000u &&
                  p[2]==0x3f800000u && p[3]==0x3f800000u);
    }
    return matches;
}
#endif
