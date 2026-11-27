#ifndef DM_VK_OUTPUT_TARGET_H
#define DM_VK_OUTPUT_TARGET_H
#include <stdint.h>
#include <limits.h>
/* 0 = Auto. Explicit targets are the shorter picture axis, not a
 * swapchain request. Keep aspect ratio in landscape AND portrait. */
static inline int dm_recon_target_extent(uint32_t iw, uint32_t ih,
        uint32_t dw, uint32_t dh, int short_edge, uint32_t *ow, uint32_t *oh) {
    *ow = *oh = 0;
    if (!iw || !ih || !dw || !dh || short_edge < 0) return 0;
    uint64_t w = dw, h = dh;
    if (short_edge) {
        if (iw >= ih) { h = (uint32_t)short_edge; w = (uint64_t)iw * h / ih; }
        else { w = (uint32_t)short_edge; h = (uint64_t)ih * w / iw; }
    }
    // VkImageBlit offsets and the existing crop contract are signed int32.
    if (!w || !h || w > INT32_MAX || h > INT32_MAX) return 0;
    *ow = (uint32_t)w; *oh = (uint32_t)h;
    return 1;
}
static inline int dm_recon_output_step(int current, int direction) {
    const int presets[] = {0,720,1080,1440,1800,2160,2880,4320};
    if (direction > 0) {
        for (unsigned i=0;i<sizeof presets/sizeof presets[0];++i)
            if (presets[i]>current) return presets[i];
        return 0;
    }
    if (current<=0) return presets[sizeof presets/sizeof presets[0]-1];
    for (unsigned i=sizeof presets/sizeof presets[0];i>0;--i)
        if (presets[i-1]<current) return presets[i-1];
    return 0;
}
static inline int dm_recon_custom_step(int current, int direction) {
    if (current<=0) return 1080;
    if (direction>0) return current>INT_MAX-120 ? INT_MAX : current+120;
    return current<240 ? 0 : current-120;
}
/* Strict settings/env parsing; reject overflows and malformed values. */
static inline int dm_recon_parse_height(const char *p) {
    if (!p || *p<'0' || *p>'9') return -1;
    int v=0;
    while (*p>='0' && *p<='9') {
        int digit=*p++-'0';
        if (v>(INT_MAX-digit)/10) return -1;
        v=v*10+digit;
    }
    while (*p==' ' || *p=='\t' || *p=='\r' || *p=='\n') ++p;
    return *p || (v && v<120) ? -1 : v;
}
int dm_recon_output_height(void);
#endif
