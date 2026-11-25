#ifndef NX_RS_STORAGE_PRIVATE_H
#define NX_RS_STORAGE_PRIVATE_H
#include <stdint.h>
/* Isolated fixed 8x8 compute control only, not guest owner authorization. */
static inline int nx_rs_private_storage_extent(uint32_t lw,uint32_t lh,uint32_t w,uint32_t h) {
    return lw && lh && w && h && lw<=32 && lh<=32 && w<=lw && h<=lh &&
        !(lw%8 || lh%8 || w%8 || h%8) && (uint64_t)w*lh==(uint64_t)h*lw;
}
#endif
