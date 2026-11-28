#pragma once
#include <math.h>
#include <stdint.h>

// Explicit gameplay limits own the presentation cadence. A slow source
// observation is evidence of pressure, not permission to lengthen its hold.
// Variable-rate sources retain the measured-period policy.
static inline double dm_fg_schedule_period(double requested, double measured, int fixed_base) {
    if (!(requested > 0) || !isfinite(requested)) return 0;
    if (fixed_base > 0) return requested;
    return isfinite(measured) && measured > requested ? measured : requested;
}
static inline double dm_fg_output_hold(double requested, double measured, int fixed_base,
                                       unsigned generated, uint64_t refresh_ns) {
    if (!generated) return 0;
    double hold = dm_fg_schedule_period(requested, measured, fixed_base) / (generated + 1.0);
    if (hold > 0 && refresh_ns) {
        double r = hold * 1e9 / (double)refresh_ns, w = floor(r + 0.5);
        if (w >= 1 && fabs(r - w) <= 0.15) {
            double snapped = w * (double)refresh_ns / 1e9;
            // For a fixed base, do not round a fractional output cadence up
            // and accumulate a persistent delay. Permit sub-microsecond noise.
            if (!fixed_base || snapped <= hold + 1e-6) hold = snapped;
        }
    }
    return hold;
}
// Default protection follows the selected base target, with 10% tolerance
// for measurement jitter. Variable cadence retains the legacy floor.
static inline float dm_fg_base_floor(int fixed_base) {
    return fixed_base > 0 ? (float)fixed_base * 0.90f : 30.0f;
}
