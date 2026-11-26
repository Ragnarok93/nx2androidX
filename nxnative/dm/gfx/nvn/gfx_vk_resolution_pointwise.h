#ifndef NX_RS_POINTWISE_H
#define NX_RS_POINTWISE_H
#include "gfx_vk_resolution_compute_transform.h"
// Operand positions containing IDs in the exact pointwise proof template.
// Every other operand is literal; only binding, local size and output
// channel selection may vary. No branches, extra descriptors or side effects.
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
// Match structure modulo a bijective ID renaming, not shader hash or game.
// A positive proof requires both textures to use the same physical grid.
static inline int nx_rs_compute_pointwise(const uint32_t *w,uint32_t n,
        unsigned src_binding,unsigned dst_binding,const uint32_t local[3]) {
    const uint32_t *ref=nx_rs_transform_shader;
    if (!w || !local || n!=sizeof nx_rs_transform_shader/sizeof *ref ||
        w[0]!=ref[0] || w[1]!=ref[1] || w[3]<2 || w[3]>256 || w[4] ||
        src_binding==dst_binding || !local[0] || !local[1] || local[2]!=1) return 0;
    uint8_t map[40]={0},used[256]={0};
    for (uint32_t i=5;i<n;) {
        const uint32_t *r=ref+i,*a=w+i;
        unsigned wc=r[0]>>16,op=r[0]&65535u;
        if (a[0]!=r[0] || !wc || wc>n-i) return 0;
        unsigned ids=nx_rs_pointwise_ids(op);
        for (unsigned j=1;j<wc;j++) {
            if (ids&(1u<<j)) {
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
        i+=wc;
    }
    return 1;
}
#endif
