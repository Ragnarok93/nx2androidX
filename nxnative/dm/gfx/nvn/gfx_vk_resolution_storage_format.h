#ifndef NX_RS_STORAGE_FORMAT_H
#define NX_RS_STORAGE_FORMAT_H
/* NVN enum values, checked against nvn.h in the Vulkan backend. The
 * corresponding SPIR-V ImageFormat must match exactly (no raw-bit views). */
static inline unsigned nx_rs_storage_spv_format(unsigned nvn) {
    switch (nvn) {
    case 0x2e:return 1; /* RGBA32F / Rgba32f */
    case 0x29:return 2; /* RGBA16F / Rgba16f */
    case 0x25:return 4; /* RGBA8 / Rgba8 */
    default:return 0;
    }
}
static inline int nx_rs_storage_spv_supported(unsigned fmt) {
    return fmt==1 || fmt==2 || fmt==4;
}
#endif
