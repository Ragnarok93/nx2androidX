#ifndef DM_VK_FG_FLOW_H
#define DM_VK_FG_FLOW_H
#include <stdint.h>
// Positive, zero and -1 retain their previous meanings.
enum { DM_FG_FLOW_AUTO=0, DM_FG_FLOW_ADAPTIVE=-1, DM_FG_FLOW_QUALITY=-2,
       DM_FG_FLOW_BALANCED=-3, DM_FG_FLOW_LOW=-4 };
enum { DM_FG_MODE_FIXED, DM_FG_MODE_ADAPTIVE, DM_FG_MODE_AUTO };
typedef struct {unsigned min,max;const char *name;} dm_fg_flow_range;
static inline int dm_fg_flow_normalize(int requested) {
    return requested>=DM_FG_FLOW_LOW && requested<0?requested:
        requested<=0?DM_FG_FLOW_AUTO:requested<25?25:requested>100?100:requested;
}
static inline int dm_fg_flow_mode(int requested) {
    int n=dm_fg_flow_normalize(requested);
    return n>0?DM_FG_MODE_FIXED:n<0?DM_FG_MODE_ADAPTIVE:DM_FG_MODE_AUTO;
}
static inline int dm_fg_flow_is_adaptive(int requested,int override_auto) {
    int mode=dm_fg_flow_mode(requested);
    return mode==DM_FG_MODE_ADAPTIVE || (mode==DM_FG_MODE_AUTO && override_auto);
}
static inline dm_fg_flow_range dm_fg_flow_bounds(int requested) {
    switch(requested){
    case DM_FG_FLOW_QUALITY:return (dm_fg_flow_range){70,100,"Quality"};
    case DM_FG_FLOW_BALANCED:return (dm_fg_flow_range){55,80,"Balanced"};
    case DM_FG_FLOW_LOW:return (dm_fg_flow_range){25,55,"Low"};
    default:return (dm_fg_flow_range){25,100,"Auto"};
    }
}
static inline int dm_fg_flow_preset_step(int requested,int direction) {
    const int presets[]={DM_FG_FLOW_QUALITY,DM_FG_FLOW_BALANCED,DM_FG_FLOW_LOW,DM_FG_FLOW_ADAPTIVE};
    unsigned i=0;while(i<3 && presets[i]!=requested)i++;
    return presets[(i+(direction<0?3:1))%4];
}
static inline int dm_fg_flow_select(int mode,int fixed,int preset) {
    if(mode==DM_FG_MODE_FIXED)return fixed<25?25:fixed>100?100:fixed;
    if(mode==DM_FG_MODE_ADAPTIVE)return dm_fg_flow_mode(preset)==DM_FG_MODE_ADAPTIVE?preset:DM_FG_FLOW_ADAPTIVE;
    return DM_FG_FLOW_AUTO;
}
// Compatibility until the three-way menu replaces the legacy stepper.
static inline int dm_fg_flow_step(int requested,int direction) {
    int n=dm_fg_flow_normalize(requested);
    if(direction<0)return n==0?100:n<0?0:n<=25?DM_FG_FLOW_ADAPTIVE:n-5;
    return n==0?DM_FG_FLOW_ADAPTIVE:n<0?25:n>=100?0:n+5;
}
// Stable resolution-tier Auto; adaptive presets seed within their bounds.
static inline int dm_fg_effective_flow(int requested,uint32_t w,uint32_t h) {
    if(requested>0)return requested<25?25:requested>100?100:requested;
    const uint64_t area=(uint64_t)w*h, budget=UINT64_C(1280)*720*10000;
    const int tiers[]={100,75,50,25};
    int pct=25;
    for(unsigned i=0;i<sizeof tiers/sizeof tiers[0];++i)
        if(area<=budget/((uint64_t)tiers[i]*tiers[i])){pct=tiers[i];break;}
    dm_fg_flow_range range=dm_fg_flow_bounds(requested);
    return pct<(int)range.min?(int)range.min:pct>(int)range.max?(int)range.max:pct;
}
#endif
