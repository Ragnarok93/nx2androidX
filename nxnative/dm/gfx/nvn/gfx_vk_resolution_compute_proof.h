#ifndef NX_RS_COMPUTE_PROOF_H
#define NX_RS_COMPUTE_PROOF_H
#include <stdint.h>
#include <string.h>
/* Deliberately small, auditable subset of already validated SPIR-V: a single
 * unconditional RGBA32F constant fill at GlobalInvocationId.xy. Not a general
 * SPIR-V validator. No buffer access, sampling, branches or other side effects.
 * The caller must separately prove descriptor identity, exact grid coverage,
 * backing format/usage and lifetime. No allocation; only called in audit. */
static inline int nx_rs_compute_constant_fill(const uint32_t *w,uint32_t n,
                                              uint32_t binding,const uint32_t local[3]) {
    enum { VOID=1,U32,F32,V2U,V3U,V4F,IMAGE,PTR_IN,PTR_IMG,FN,CONST_F,CONST_V,IN,OUT };
    if (!w || !local || n<5 || n>4096 || w[0]!=0x07230203u || w[3]<2 || w[3]>256 ||
        !local[0] || !local[1] || local[2]!=1) return 0;
    uint8_t tag[256]={0}; uint32_t bound=w[3], entry=0,gid=0,img=0,set=0,bind=0;
    uint32_t gidvar=0,imgvar=0,gload=0,coord=0,iload=0;
    unsigned cap=0,model=0,mode=0,body=0;
#define RS_ID(x) ((x)>0 && (x)<bound)
#define RS_TAG(x,t) (RS_ID(x) && tag[x]==(t))
#define RS_NEW(x,t) (RS_ID(x) && !tag[x] && (tag[x]=(t)))
    for (uint32_t i=5;i<n;) {
        const uint32_t *o=w+i; uint32_t wc=o[0]>>16,op=o[0]&65535u;
        if (!wc || wc>n-i) return 0;
        if (body) {
            switch(body) {
            case 1: if(op!=248 || wc!=2 || !RS_NEW(o[1],255)) return 0; break;
            case 2: if(op!=61 || wc!=4 || !RS_TAG(o[1],V3U) || !RS_NEW(o[2],255) || o[3]!=gidvar) return 0; gload=o[2]; break;
            case 3: if(op!=79 || wc!=7 || !RS_TAG(o[1],V2U) || !RS_NEW(o[2],255) || o[3]!=gload || o[4]!=gload || o[5]!=0 || o[6]!=1) return 0; coord=o[2]; break;
            case 4: if(op!=61 || wc!=4 || !RS_TAG(o[1],IMAGE) || !RS_NEW(o[2],255) || o[3]!=imgvar) return 0; iload=o[2]; break;
            case 5: if(op!=99 || wc!=4 || o[1]!=iload || o[2]!=coord || !RS_TAG(o[3],CONST_V)) return 0; break;
            case 6: if(op!=253 || wc!=1) return 0; break;
            case 7: return op==56 && wc==1 && i+wc==n;
            default: return 0;
            }
            body++; i+=wc; continue;
        }
        switch(op) {
        case 17: if(wc!=2 || o[1]!=1 || cap++) return 0; break;
        case 14: if(wc!=3 || o[1]!=0 || o[2]!=1 || model++) return 0; break;
        case 15: if(wc!=6 || o[1]!=5 || !RS_ID(o[2]) || entry || o[3]!=0x6e69616du || o[4]!=0 || !RS_ID(o[5])) return 0; entry=o[2]; gid=o[5]; break;
        case 16: if(wc!=6 || !entry || o[1]!=entry || o[2]!=17 || mode++ || memcmp(o+3,local,12)) return 0; break;
        case 71:
            if(wc!=4 || !RS_ID(o[1])) return 0;
            if(o[2]==11 && o[3]==28 && o[1]==gid && !gidvar) gidvar=o[1];
            else if(o[2]==34 && o[3]==0 && !set && (!img || img==o[1])) { img=o[1]; set=1; }
            else if(o[2]==33 && o[3]==binding && !bind && (!img || img==o[1])) { img=o[1]; bind=1; }
            else return 0;
            break;
        case 19: if(wc!=2 || !RS_NEW(o[1],VOID)) return 0; break;
        case 21: if(wc!=4 || o[2]!=32 || o[3]!=0 || !RS_NEW(o[1],U32)) return 0; break;
        case 22: if(wc!=3 || o[2]!=32 || !RS_NEW(o[1],F32)) return 0; break;
        case 23:
            if(wc!=4) return 0;
            if(RS_TAG(o[2],U32) && o[3]==2) { if(!RS_NEW(o[1],V2U)) return 0; }
            else if(RS_TAG(o[2],U32) && o[3]==3) { if(!RS_NEW(o[1],V3U)) return 0; }
            else if(RS_TAG(o[2],F32) && o[3]==4) { if(!RS_NEW(o[1],V4F)) return 0; }
            else return 0;
            break;
        case 25: if(wc!=9 || !RS_TAG(o[2],F32) || o[3]!=1 || o[4] || o[5] || o[6] || o[7]!=2 || o[8]!=1 || !RS_NEW(o[1],IMAGE)) return 0; break;
        case 32:
            if(wc!=4) return 0;
            if(o[2]==1 && RS_TAG(o[3],V3U)) { if(!RS_NEW(o[1],PTR_IN)) return 0; }
            else if(o[2]==0 && RS_TAG(o[3],IMAGE)) { if(!RS_NEW(o[1],PTR_IMG)) return 0; }
            else return 0;
            break;
        case 33: if(wc!=3 || !RS_TAG(o[2],VOID) || !RS_NEW(o[1],FN)) return 0; break;
        case 43: if(wc!=4 || !RS_TAG(o[1],F32) || !RS_NEW(o[2],CONST_F)) return 0; break;
        case 44: if(wc!=7 || !RS_TAG(o[1],V4F) || !RS_TAG(o[3],CONST_F) || !RS_TAG(o[4],CONST_F) || !RS_TAG(o[5],CONST_F) || !RS_TAG(o[6],CONST_F) || !RS_NEW(o[2],CONST_V)) return 0; break;
        case 59:
            if(wc!=4) return 0;
            if(o[3]==1 && o[2]==gidvar && RS_TAG(o[1],PTR_IN)) { if(!RS_NEW(o[2],IN)) return 0; }
            else if(o[3]==0 && o[2]==img && RS_TAG(o[1],PTR_IMG)) { if(!RS_NEW(o[2],OUT)) return 0; imgvar=o[2]; }
            else return 0;
            break;
        case 54: if(wc!=5 || !cap || !model || !mode || !set || !bind || !RS_TAG(gidvar,IN) || !RS_TAG(imgvar,OUT) || !RS_TAG(o[1],VOID) || o[2]!=entry || !RS_NEW(o[2],255) || o[3] || !RS_TAG(o[4],FN)) return 0; body=1; break;
        default: return 0;
        }
        i+=wc;
    }
#undef RS_ID
#undef RS_TAG
#undef RS_NEW
    return 0;
}
#endif
