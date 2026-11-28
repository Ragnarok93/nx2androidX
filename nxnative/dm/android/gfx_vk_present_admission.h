#pragma once
#include <math.h>
#include <stdint.h>

// Reserve time for the real image; a delayed optional image must not
// consume the whole batch's source cadence. All times are monotonic ns.
static inline uint64_t dm_present_optional_deadline(uint64_t source, double hold, unsigned count) {
    if (!source || !count || !isfinite(hold) || hold <= 0 || hold > .1 || count > 3) return 0;
    double batch = hold * (count + 1.0) * 1e9;
    uint64_t span = (uint64_t)batch;
    uint64_t reserve = span / 4 < 2000000 ? span / 4 : 2000000;
    span -= reserve;
    return source <= UINT64_MAX-span ? source+span : 0;
}
static inline uint64_t dm_present_optional_wait(uint64_t now, uint64_t deadline, double hold) {
    if (!deadline || now >= deadline || !isfinite(hold) || hold <= 0 || hold > .1) return 0;
    uint64_t interval = (uint64_t)(hold * 1e9), remaining = deadline-now;
    return remaining < interval ? remaining : interval;
}
// The real source gets a larger finite WSI opportunity; never request an
// unbounded acquisition on a stalled or detached compositor.
static inline uint64_t dm_present_required_wait(uint64_t refresh) {
    if (refresh > 50000000) return 100000000;
    uint64_t wait = 2*refresh;
    return wait < 50000000 ? 50000000 : wait;
}
// Generated outputs are a chronological accepted prefix. The real image
// occupies the remaining interpolation interval, including skipped slots.
static inline double dm_present_real_period(double period, double hold, unsigned planned, unsigned accepted) {
    return planned && accepted <= planned && isfinite(hold) && hold > 0 ?
        hold * (planned+1.0-accepted) : period;
}
