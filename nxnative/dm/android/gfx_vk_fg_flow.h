#ifndef DM_VK_FG_FLOW_H
#define DM_VK_FG_FLOW_H
#include <stdint.h>
// Zero retains fixed resolution-tier Auto; -1 explicitly opts into the live
// controller. Old saved percentages and zero keep their meaning.
enum { DM_FG_FLOW_ADAPTIVE=-1, DM_FG_FLOW_AUTO=0 };
static inline int dm_fg_flow_normalize(int requested) {
    return requested==DM_FG_FLOW_ADAPTIVE?DM_FG_FLOW_ADAPTIVE:
        requested<=0?DM_FG_FLOW_AUTO:requested<25?25:requested>100?100:requested;
}
static inline int dm_fg_flow_is_adaptive(int requested,int override_auto) {
    return requested==DM_FG_FLOW_ADAPTIVE || (requested==DM_FG_FLOW_AUTO && override_auto);
}
static inline int dm_fg_flow_step(int requested,int direction) {
    int n=dm_fg_flow_normalize(requested);
    if(direction<0)return n==0?100:n==DM_FG_FLOW_ADAPTIVE?0:n<=25?DM_FG_FLOW_ADAPTIVE:n-5;
    return n==0?DM_FG_FLOW_ADAPTIVE:n==DM_FG_FLOW_ADAPTIVE?25:n>=100?0:n+5;
}
// Auto bounds the optical-flow grid near 720p using stable resolution tiers.
// This is a conservative initial quality choice, not a measured adaptive GPU
// controller. Full-size real/generated images are unchanged. Explicit values
// retain their requested quality and never follow the Auto policy.
static inline int dm_fg_effective_flow(int requested,uint32_t w,uint32_t h) {
    if(requested>0)return requested<25?25:requested>100?100:requested;
    const uint64_t area=(uint64_t)w*h, budget=UINT64_C(1280)*720*10000;
    const int tiers[]={100,75,50,25};
    for(unsigned i=0;i<sizeof tiers/sizeof tiers[0];++i)
        if(area<=budget/((uint64_t)tiers[i]*tiers[i]))return tiers[i];
    return 25;
}
#endif
