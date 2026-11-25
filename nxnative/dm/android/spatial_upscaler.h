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

int dm_spatial_mode(void);
int dm_fsr_sharpness(void);
