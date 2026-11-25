#pragma once

// Stable settings.txt values. Native leaves the frame at its guest size for LSFG.
enum spatial_mode { SP_NATIVE, SP_BILINEAR, SP_FSR1, SP_SGSR1, SP_AUTO, SP_MODE_COUNT };

static inline int spatial_mode_valid(int mode) {
    return mode >= SP_NATIVE && mode < SP_MODE_COUNT;
}

static inline const char *spatial_mode_name(int mode) {
    static const char *const names[] = {"Native", "Bilinear", "FSR 1", "GSR 1", "Auto"};
    return spatial_mode_valid(mode) ? names[mode] : names[SP_NATIVE];
}

int dm_spatial_mode(void);
int dm_fsr_sharpness(void);
