#pragma once

// Stable settings.txt values. Value 4 was Auto and is no longer selectable.
enum spatial_mode { SP_NATIVE, SP_BILINEAR, SP_FSR1, SP_SGSR1, SP_LEGACY_AUTO, SP_SGSR2 };

static inline int spatial_mode_valid(int mode) {
    return (mode >= SP_NATIVE && mode <= SP_SGSR1) || mode == SP_SGSR2;
}

static inline int spatial_mode_restore(int mode) {
    return mode == SP_LEGACY_AUTO ? SP_SGSR2 : mode;
}

static inline int spatial_mode_next(int mode) {
    if (mode >= SP_NATIVE && mode < SP_SGSR1) return mode + 1;
    return mode == SP_SGSR1 ? SP_SGSR2 : SP_NATIVE;
}

static inline const char *spatial_mode_name(int mode) {
    static const char *const names[] = {"None", "Bilinear", "FSR 1", "GSR 1", 0, "GSR 2"};
    return spatial_mode_valid(mode) ? names[mode] : names[SP_NATIVE];
}

// Sampler capabilities for the certified composition draw. Spatial
// Bilinear's original-program patch needs linear clamp; full-grid
// replacements use their own sampler or independently sampled snapshot.
// The original sampler remains bound for guestSmoothstep fallback.
enum { SP_GUEST_COLOR_SAMPLE=1, SP_GUEST_LINEAR_CLAMP=2 };
static inline int spatial_guest_sampler_caps(unsigned refusal) {
    // Missing sampler/registration, forced integer-nearest, compare or
    // reduction are not a normal color sample contract.
    if (refusal & (1u|2u|4u|128u|256u)) return 0;
    return SP_GUEST_COLOR_SAMPLE | (refusal ? 0 : SP_GUEST_LINEAR_CLAMP);
}
// Internal pipeline key only; never persist this value as a user setting.
enum { SP_GUEST_GRID_BILINEAR=9 };
static inline int spatial_guest_replay_mode(int requested,int caps,int temporal_ready) {
    if (!(caps&SP_GUEST_COLOR_SAMPLE)) return SP_NATIVE;
    if (requested==SP_SGSR2) return temporal_ready?SP_SGSR2:SP_SGSR1;
    if (requested==SP_BILINEAR && !(caps&SP_GUEST_LINEAR_CLAMP))
        return SP_GUEST_GRID_BILINEAR;
    return requested;
}
static inline int spatial_guest_report_mode(int mode) {
    return mode==SP_GUEST_GRID_BILINEAR?SP_BILINEAR:mode;
}

// Conservative indications of replay work submitted on this real frame.
enum { SP_REPLAY_SHARPENED = 1, SP_REPLAY_TEMPORAL = 2 };

int dm_spatial_mode(void);
int dm_fsr_sharpness(void);
