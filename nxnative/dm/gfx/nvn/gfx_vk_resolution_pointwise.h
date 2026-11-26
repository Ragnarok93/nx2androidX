#ifndef NX_RS_POINTWISE_H
#define NX_RS_POINTWISE_H
#include "gfx_vk_resolution_compute_transform.h"
// Operand positions containing IDs in the exact pointwise proof template.
static inline unsigned nx_rs_pointwise_ids(unsigned op) {
    switch (op) {
    case 15:return 0x24;
    case 16:case 71:case 19:case 21:case 22:case 248:return 0x02;
    case 23:case 25:case 27:case 33:case 43:case 59:return 0x06;
    case 32:return 0x0a;
    case 54:return 0x16;
    case 61:case 100:case 99:return 0x0e;
    case 79:return 0x1e;
    case 95:return 0x5e;
    default:return 0;
    }
}
// Only pixel-local float arithmetic over the fetched value and finite
// constants is admitted. All image coordinates and resource accesses still
// match the original template modulo bijective ID renaming.
static inline int nx_rs_compute_pointwise(const uint32_t *w,uint32_t n,
        unsigned src_binding,unsigned dst_binding,const uint32_t local[3]) {
    const uint32_t *ref=nx_rs_transform_shader;
    const unsigned rn=sizeof nx_rs_transform_shader/sizeof *ref;
    if (!w || !local || n<5 || n>rn+256 || w[0]!=ref[0] || w[1]!=ref[1] ||
        w[3]<2 || w[3]>256 || w[4] || src_binding==dst_binding ||
        !local[0] || !local[1] || local[2]!=1) return 0;
    enum { SCALAR=1,VECTOR=2,PIXEL=3 };
    uint8_t map[40]={0},used[256]={0},value[256]={0};
    unsigned i=5,k=5,extra=0;
    for (;k<rn;) {
        if (i>=n) return 0;
        const uint32_t *r=ref+k,*a=w+i;
        unsigned wc=r[0]>>16,op=r[0]&65535u,ac=a[0]>>16,ao=a[0]&65535u;
        if (!ac || ac>n-i) return 0;
        // Additional constants may appear just before the function body.
        if (op==54 && (ao==43 || ao==44)) {
            if (++extra>32 || ac!=(ao==43?4u:7u) || !a[2] || a[2]>=w[3] || used[a[2]]) return 0;
            if (ao==43) {
                if (a[1]!=map[5] || (a[3]&0x7f800000u)==0x7f800000u) return 0;
                value[a[2]]=SCALAR;
            } else {
                if (a[1]!=map[6]) return 0;
                for (unsigned j=3;j<7;j++) if (a[j]>=w[3] || value[a[j]]!=SCALAR) return 0;
                value[a[2]]=VECTOR;
            }
            used[a[2]]=1;i+=ac;continue;
        }
        // Only arithmetic on the current pixel, after the output image load
        // and before its single store. No coordinate or descriptor arithmetic.
        if (op==99 && ao!=99) {
            if (++extra>32 || ac<4 || a[1]!=map[6] || !a[2] || a[2]>=w[3] || used[a[2]] ||
                a[3]>=w[3] || value[a[3]]!=PIXEL) return 0;
            if (ao==127) {if (ac!=4) return 0;}
            else if (ao==142) {if (ac!=5 || a[4]>=w[3] || value[a[4]]!=SCALAR) return 0;}
            else if (ao==129 || ao==131 || ao==133 || ao==136) {
                if (ac!=5 || a[4]>=w[3] || (value[a[4]]!=VECTOR && value[a[4]]!=PIXEL)) return 0;
            } else return 0;
            used[a[2]]=1;value[a[2]]=PIXEL;i+=ac;continue;
        }
        // Identity transform may omit the channel shuffle entirely.
        if (op==79 && wc==9 && ao!=79) {k+=wc;continue;}
        if (a[0]!=r[0] || !wc || wc>n-i) return 0;
        unsigned ids=nx_rs_pointwise_ids(op);
        for (unsigned j=1;j<wc;j++) {
            if (op==99 && j==3) {
                if (a[j]>=w[3] || value[a[j]]!=PIXEL) return 0;
            } else if (ids&(1u<<j)) {
                unsigned from=r[j],to=a[j];
                if (!from || from>=sizeof map || !to || to>=w[3]) return 0;
                if (map[from]) {if (map[from]!=to) return 0;}
                else {if (used[to]) return 0;map[from]=(uint8_t)to;used[to]=1;}
            } else if (op==71 && r[2]==33 && j==3) {
                if (a[j]!=(r[j]==6?src_binding:dst_binding)) return 0;
            } else if (op==16 && j>=3) {
                if (a[j]!=local[j-3]) return 0;
            } else if (op==79 && wc==9 && j>=5) {
                if (a[j]>3) return 0;
            } else if (a[j]!=r[j]) return 0;
        }
        if (op==95 || (op==79 && wc==9)) value[a[2]]=PIXEL;
        i+=wc;k+=wc;
    }
    return i==n;
}
#endif
