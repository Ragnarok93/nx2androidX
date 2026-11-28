#pragma once
#include <stdint.h>
static inline int vk_pace_refresh_update(uint64_t *refresh, uint64_t sample) {
    if (sample <= 1000000) return 0;
    uint64_t was=*refresh;
    uint64_t diff=sample>was?sample-was:was-sample;
    if (was && diff < (was+99)/100) return 0;
    *refresh=sample;
    return 1;
}
static inline uint64_t vk_pace_snap(uint64_t t, uint64_t r, uint64_t a) {
    if (t <= a) return a - (a-t)/r*r;
    return a+(t-a+r-1)/r*r;
}
static inline uint64_t vk_pace_next_grid(uint64_t ideal, uint64_t previous, uint64_t refresh, uint64_t anchor) {
    uint64_t t=ideal>refresh/2?ideal-refresh/2:0;
    uint64_t minimum=previous+refresh/2;
    if (t<minimum) t=minimum;
    return vk_pace_snap(t,refresh,anchor);
}
