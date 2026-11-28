#ifndef DM_VK_FG_FLOW_H
#define DM_VK_FG_FLOW_H
#include <stdint.h>
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
