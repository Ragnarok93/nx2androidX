#ifndef NX_RS_LIVE_H
#define NX_RS_LIVE_H
#include "gfx_vk_resolution_adaptive.h"
enum { NX_RS_OFF=0, NX_RS_FIXED=1, NX_RS_ADAPTIVE=2 };
typedef struct { unsigned mode,min,max; } nx_rs_live_config;
static inline unsigned nx_rs_live_pack(nx_rs_live_config c) {
    if (c.mode>NX_RS_ADAPTIVE || !nx_rs_scale_step(c.min) ||
        !nx_rs_scale_step(c.max) || c.min>c.max) return 0;
    return c.mode|(c.min<<2)|(c.max<<9);
}
static inline nx_rs_live_config nx_rs_live_unpack(unsigned p) {
    nx_rs_live_config c={p&3,(p>>2)&127,(p>>9)&127};
    if (!p || nx_rs_live_pack(c)!=p) c=(nx_rs_live_config){NX_RS_OFF,50,100};
    return c;
}
static inline nx_rs_live_config nx_rs_live_min(nx_rs_live_config c,unsigned p) {
    if (nx_rs_scale_step(p)) { c.min=p; if(c.max<p)c.max=p; }
    return c;
}
static inline nx_rs_live_config nx_rs_live_max(nx_rs_live_config c,unsigned p) {
    if (nx_rs_scale_step(p)) { c.max=p; if(c.min>p)c.min=p; }
    return c;
}
typedef struct { unsigned config,fps,initialized; nx_rs_adaptive adaptive; } nx_rs_live_policy;
// Reconfiguration invalidates earlier GPU samples. Native and fixed modes
// never feed the adaptive policy. Returns 1 only when target must be sent.
static inline int nx_rs_live_update(nx_rs_live_policy *p,unsigned packed,unsigned fps,uint64_t frame,
                                   uint64_t sample,unsigned gpu,unsigned cpu,unsigned *target) {
    nx_rs_live_config c=nx_rs_live_unpack(packed);
    packed=nx_rs_live_pack(c);
    if (!p->initialized || p->config!=packed || (c.mode==NX_RS_ADAPTIVE && p->fps!=fps)) {
        p->initialized=1;p->config=packed;p->fps=fps;
        nx_rs_adaptive_reset(&p->adaptive,c.min,c.max,fps,frame);
        *target=c.mode==NX_RS_OFF?100:c.max;return 1;
    }
    if(c.mode==NX_RS_ADAPTIVE && nx_rs_adaptive_sample(&p->adaptive,sample,gpu,cpu)) {
        *target=p->adaptive.scale;nx_rs_adaptive_requested(&p->adaptive,frame);return 1;
    }
    return 0;
}
#endif
