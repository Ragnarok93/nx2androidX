#ifndef DM_ANDROID_FRONT_PROFILE_H
#define DM_ANDROID_FRONT_PROFILE_H

#include "dm.h"
#include <stdlib.h>
#include <string.h>
#include <sys/system_properties.h>

// A device-specific tuning request comes from the game's android.tsv, rather
// than a global emulator change. The validated FFRS 60 FPS profile was tested
// on an Adreno 830; don't assume that is viable on the S25 FE (Exynos 2400).
// An explicit env.txt override or the player's saved docked setting wins.
static inline void tune_ffrs_s25fe_radv(void) {
    const char *enabled = getenv("DM_FFRS_S25FE_RADV_TUNE");
    if (!enabled || strcmp(enabled, "1")) return;
    char model[PROP_VALUE_MAX] = {0};
    if (!__system_property_get("ro.product.model", model) || strncmp(model, "SM-S731", 7)) return;
    const char *driver = getenv("DM_VK_DRIVER");
    if (!driver || !(strstr(driver, "radeon") || strstr(driver, "radv"))) return;
    // UE on the original Switch targets 30 FPS; retain a correctly scoped
    // pacing reference, with DM_FPS_TARGET=60 remaining an explicit option.
    if (!getenv("DM_FPS_TARGET")) {
        setenv("DM_FPS_TARGET", "30", 0);
        LOG("[profile] FFRS S25 FE/RADV: Switch-reference 30 FPS target (override with DM_FPS_TARGET=60)");
    }
    LOG("[profile] FFRS S25 FE/RADV: effective DM_FPS_TARGET=%s", getenv("DM_FPS_TARGET"));
    // Preserve explicit fixed-60 pacing rather than allowing adaptive fallback.
    const char *fps = getenv("DM_FPS_TARGET");
    if (fps && atoi(fps) == 60) {
        if (!getenv("DM_ADAPTIVE_FPS")) setenv("DM_ADAPTIVE_FPS", "0", 0);
        if (!getenv("DM_PRESENT_INTERVAL")) setenv("DM_PRESENT_INTERVAL", "display", 0);
    }
    // The uploaded run recorded 384 omitted draws for unavailable pipelines
    // by present 4200. The asynchronous pipeline path deliberately drops
    // draws while compiling. On this one GPU/profile, require the pipeline
    // before drawing; first-use compilation hitches are a known tradeoff.
    // Explicit env.txt DM_VK_ASYNC_PIPE=1 and Snapdragon are untouched.
    if (!getenv("DM_VK_ASYNC_PIPE")) {
        setenv("DM_VK_ASYNC_PIPE", "0", 0);
        LOG("[profile] FFRS S25 FE/RADV: require first-use pipelines; async draw omission disabled");
    }
    setenv("DM_VK_COMPUTE_WARM_HISTORY", "1", 0);
    // The geometry capture shows material shaders reading an uninitialized
    // R8 mip chain. Apply the CPU-uncached upload path to existing packs too.
    setenv("DM_VK_UNCACHED_MIP_INIT", "1", 0);
    // Demand-back only eligible NVN pools on constrained S25 FE memory.
    // Explicit env.txt overrides, including 0, remain authoritative.
    if (!getenv("DM_VK_ZEROCOPY_LAZY")) {
        setenv("DM_VK_ZEROCOPY_LAZY", "2", 0);
        LOG("[profile] FFRS S25 FE/RADV: lazy backing of eligible GPU pools");
    }
    // Qualcomm's LRZ copy/writeback workaround targets Adreno. The
    // Xclipse depth attachment format is selected from advertised support.
    if (!getenv("DM_VK_LRZ_TOUCH")) {
        setenv("DM_VK_LRZ_TOUCH", "0", 0);
        LOG("[profile] FFRS Xclipse: disabled Adreno-only LRZ depth copy/writeback");
    }
    // Past trials of handheld mode, DCC_SMALL=0, dynamic rendering, DLC=1,
    // and forced single-thread replay did not repair missing actors. Do not
    // impose any of them on a new port: respect the normal NVN/Vulkan
    // behavior and any explicit settings in the game's env.txt.
    LOG("[profile] FFRS S25 FE/RADV: neutral rendering defaults (driver %s)", driver);
}
#endif
