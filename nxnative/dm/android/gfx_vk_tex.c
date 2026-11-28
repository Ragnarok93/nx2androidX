// Android port, Vulkan backend: formats, textures, views, samplers and the texture / sampler ID tables (see gfx_vk.h).
//   A texture object's image is found by its storage: textures over the same pool, offset and description share one image (as nvn_mtl.m's storage-sharing
//   table), which is how a render target and the texture a later pass samples it through meet. A packaged texture (packaged layout, CPU-written) is filled
//   from the guest's block-linear bytes (nvn_deswizzle on the CPU into scratch memory, copied in by the upload command buffer) and filled again when the
//   dirty-page tracker saw a write to its pages; any other texture is the GPU's (render targets, images), filled only by GPU work.
//   Version 1 keeps every image in VK_IMAGE_LAYOUT_GENERAL after its first use, ordered by whole-pipeline barriers.
#include "gfx_vk.h"
#include "gfx_vk_resolution_ownership.h"
#include "gfx_vk_d24_copy.h"
#include "gfx_vk_resolution_budget.h"
#include "gfx_vk_resolution_scope.h"
#include "gfx_vk_resolution_storage_format.h"
_Static_assert(NVN_FORMAT_RGBA32F==0x2e && NVN_FORMAT_RGBA16F==0x29 && NVN_FORMAT_RGBA8==0x25, "resolution storage format table");
#include "gfx_vk_resolution_window.h"
#include "ownership_exchange.h"
#include "gfx_vk_resolution_restore.h"
#include "gfx_vk_resolution_preflight.h"
#include "gfx_vk_resolution_active.h"
#include "gfx_vk_resolution_storage_private.h"
#include "nvn_tex.h"
#include "../gfx/tex/nvn_deswizzle.h"
#include "nvn_hmap.h"
#include <string.h>
#include <math.h>
#include "../gfx/tex/nvn_bc.h"

int nvn_dirty_since_span(const void *span, const void *addr, size_t len, uint64_t stamp);
const void *nvn_dirty_span(const void *addr, size_t len);
void nvn_dirty_protect(const void *addr, size_t len); void nvn_dirty_release(const void *addr, size_t len); uint64_t nvn_dirty_released(void); int nvn_dirty_runs(void);
uint64_t nvn_dirty_now(void);

// ---------------- formats ----------------
enum { K_NONE, K_FLOAT, K_INT, K_SINT };
typedef struct { uint16_t vk; uint8_t noalpha, rb, kind; } fent;   // vk: VkFormat (16 bits suffice for the core formats; ASTC / packed extras computed)
#define F(v) {v, 0, 0, K_FLOAT}
#define FX(v) {v, 1, 0, K_FLOAT}
#define U(v) {v, 0, 0, K_INT}
#define UX(v) {v, 1, 0, K_INT}
#define I(v) {v, 0, 0, K_SINT}
#define IX(v) {v, 1, 0, K_SINT}
static const fent FT[0x9D] = {
    [0x01] = F(VK_FORMAT_R8_UNORM), F(VK_FORMAT_R8_SNORM), U(VK_FORMAT_R8_UINT), I(VK_FORMAT_R8_SINT),
    [0x05] = F(VK_FORMAT_R16_SFLOAT), F(VK_FORMAT_R16_UNORM), F(VK_FORMAT_R16_SNORM), U(VK_FORMAT_R16_UINT), I(VK_FORMAT_R16_SINT),
    [0x0A] = F(VK_FORMAT_R32_SFLOAT), U(VK_FORMAT_R32_UINT), I(VK_FORMAT_R32_SINT),
    [0x0D] = F(VK_FORMAT_R8G8_UNORM), F(VK_FORMAT_R8G8_SNORM), U(VK_FORMAT_R8G8_UINT), I(VK_FORMAT_R8G8_SINT),
    [0x11] = F(VK_FORMAT_R16G16_SFLOAT), F(VK_FORMAT_R16G16_UNORM), F(VK_FORMAT_R16G16_SNORM), U(VK_FORMAT_R16G16_UINT), I(VK_FORMAT_R16G16_SINT),
    [0x16] = F(VK_FORMAT_R32G32_SFLOAT), U(VK_FORMAT_R32G32_UINT), I(VK_FORMAT_R32G32_SINT),
    // 0x19-0x24 three-component formats: not sampleable on mobile GPUs (vertex input only, see vk_vertex_format)
    [0x25] = F(VK_FORMAT_R8G8B8A8_UNORM), F(VK_FORMAT_R8G8B8A8_SNORM), U(VK_FORMAT_R8G8B8A8_UINT), I(VK_FORMAT_R8G8B8A8_SINT),
    [0x29] = F(VK_FORMAT_R16G16B16A16_SFLOAT), F(VK_FORMAT_R16G16B16A16_UNORM), F(VK_FORMAT_R16G16B16A16_SNORM), U(VK_FORMAT_R16G16B16A16_UINT), I(VK_FORMAT_R16G16B16A16_SINT),
    [0x2E] = F(VK_FORMAT_R32G32B32A32_SFLOAT), U(VK_FORMAT_R32G32B32A32_UINT), I(VK_FORMAT_R32G32B32A32_SINT),
    [0x31] = U(VK_FORMAT_S8_UINT), F(VK_FORMAT_D16_UNORM), F(VK_FORMAT_X8_D24_UNORM_PACK32), F(VK_FORMAT_D32_SFLOAT), F(VK_FORMAT_D24_UNORM_S8_UINT), F(VK_FORMAT_D32_SFLOAT_S8_UINT),
    [0x37] = FX(VK_FORMAT_R8G8B8A8_SRGB), F(VK_FORMAT_R8G8B8A8_SRGB),
    // NVN packed names run from low bits to high bits; Vulkan PACK16 names
    // run from high bits to low bits. RGB565 has R in bits 0..4, B in 11..15.
    [0x39] = F(VK_FORMAT_A4B4G4R4_UNORM_PACK16), FX(VK_FORMAT_A1B5G5R5_UNORM_PACK16_KHR), F(VK_FORMAT_A1B5G5R5_UNORM_PACK16_KHR), F(VK_FORMAT_B5G6R5_UNORM_PACK16),
    [0x3D] = F(VK_FORMAT_A2B10G10R10_UNORM_PACK32), U(VK_FORMAT_A2B10G10R10_UINT_PACK32), F(VK_FORMAT_B10G11R11_UFLOAT_PACK32), F(VK_FORMAT_E5B9G9R9_UFLOAT_PACK32),
    [0x41] = F(VK_FORMAT_BC1_RGB_UNORM_BLOCK), F(VK_FORMAT_BC1_RGBA_UNORM_BLOCK), F(VK_FORMAT_BC2_UNORM_BLOCK), F(VK_FORMAT_BC3_UNORM_BLOCK),
             F(VK_FORMAT_BC1_RGB_SRGB_BLOCK), F(VK_FORMAT_BC1_RGBA_SRGB_BLOCK), F(VK_FORMAT_BC2_SRGB_BLOCK), F(VK_FORMAT_BC3_SRGB_BLOCK),
    [0x49] = F(VK_FORMAT_BC4_UNORM_BLOCK), F(VK_FORMAT_BC4_SNORM_BLOCK), F(VK_FORMAT_BC5_UNORM_BLOCK), F(VK_FORMAT_BC5_SNORM_BLOCK),
             F(VK_FORMAT_BC7_UNORM_BLOCK), F(VK_FORMAT_BC7_SRGB_BLOCK), F(VK_FORMAT_BC6H_SFLOAT_BLOCK), F(VK_FORMAT_BC6H_UFLOAT_BLOCK),
    // *_UI2F / *_I2F: integer storage read as float without normalization = Vulkan's scaled formats
    [0x51] = F(VK_FORMAT_R8_USCALED), F(VK_FORMAT_R8_SSCALED), F(VK_FORMAT_R16_USCALED), F(VK_FORMAT_R16_SSCALED), U(VK_FORMAT_R32_UINT), I(VK_FORMAT_R32_SINT),
    [0x57] = F(VK_FORMAT_R8G8_USCALED), F(VK_FORMAT_R8G8_SSCALED), F(VK_FORMAT_R16G16_USCALED), F(VK_FORMAT_R16G16_SSCALED), U(VK_FORMAT_R32G32_UINT), I(VK_FORMAT_R32G32_SINT),
    [0x63] = F(VK_FORMAT_R8G8B8A8_USCALED), F(VK_FORMAT_R8G8B8A8_SSCALED), F(VK_FORMAT_R16G16B16A16_USCALED), F(VK_FORMAT_R16G16B16A16_SSCALED), U(VK_FORMAT_R32G32B32A32_UINT), I(VK_FORMAT_R32G32B32A32_SINT),
    [0x69] = F(VK_FORMAT_A2B10G10R10_SNORM_PACK32), I(VK_FORMAT_A2B10G10R10_SINT_PACK32), F(VK_FORMAT_A2B10G10R10_USCALED_PACK32), F(VK_FORMAT_A2B10G10R10_SSCALED_PACK32),
    [0x6D] = FX(VK_FORMAT_R8G8B8A8_UNORM), FX(VK_FORMAT_R8G8B8A8_SNORM), UX(VK_FORMAT_R8G8B8A8_UINT), IX(VK_FORMAT_R8G8B8A8_SINT),
    [0x71] = FX(VK_FORMAT_R16G16B16A16_SFLOAT), FX(VK_FORMAT_R16G16B16A16_UNORM), FX(VK_FORMAT_R16G16B16A16_SNORM), UX(VK_FORMAT_R16G16B16A16_UINT), IX(VK_FORMAT_R16G16B16A16_SINT),
    [0x76] = FX(VK_FORMAT_R32G32B32A32_SFLOAT), UX(VK_FORMAT_R32G32B32A32_UINT), IX(VK_FORMAT_R32G32B32A32_SINT),
    [0x95] = F(VK_FORMAT_R5G6B5_UNORM_PACK16), FX(VK_FORMAT_A1R5G5B5_UNORM_PACK16), F(VK_FORMAT_A1R5G5B5_UNORM_PACK16), F(VK_FORMAT_R5G5B5A1_UNORM_PACK16),
    [0x99] = FX(VK_FORMAT_B8G8R8A8_UNORM), F(VK_FORMAT_B8G8R8A8_UNORM), FX(VK_FORMAT_B8G8R8A8_SRGB), F(VK_FORMAT_B8G8R8A8_SRGB),
};
static vkfmt_t g_fmt[0x9D]; static uint8_t g_fmt_done[0x9D];
// BC decoded on the CPU: what each BC format decodes into (dec 1 BC1 RGB, 2 BC1, 3 BC2, 4 BC3, 5 / 6 BC4 unsigned / signed, 7 / 8 BC5, 9 / 10 BC6H unsigned / signed, 11 BC7)
static void bc_target(vkfmt_t *o) {
    int srgb = o->vk == VK_FORMAT_BC1_RGB_SRGB_BLOCK || o->vk == VK_FORMAT_BC1_RGBA_SRGB_BLOCK || o->vk == VK_FORMAT_BC2_SRGB_BLOCK || o->vk == VK_FORMAT_BC3_SRGB_BLOCK || o->vk == VK_FORMAT_BC7_SRGB_BLOCK;
    VkFormat rgba = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    switch (o->vk) {
    case VK_FORMAT_BC1_RGB_UNORM_BLOCK: case VK_FORMAT_BC1_RGB_SRGB_BLOCK: o->dec = 1; o->vk = rgba; o->dbpp = 4; break;
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK: case VK_FORMAT_BC1_RGBA_SRGB_BLOCK: o->dec = 2; o->vk = rgba; o->dbpp = 4; break;
    case VK_FORMAT_BC2_UNORM_BLOCK: case VK_FORMAT_BC2_SRGB_BLOCK: o->dec = 3; o->vk = rgba; o->dbpp = 4; break;
    case VK_FORMAT_BC3_UNORM_BLOCK: case VK_FORMAT_BC3_SRGB_BLOCK: o->dec = 4; o->vk = rgba; o->dbpp = 4; break;
    case VK_FORMAT_BC4_UNORM_BLOCK: o->dec = 5; o->vk = VK_FORMAT_R8_UNORM; o->dbpp = 1; break;
    case VK_FORMAT_BC4_SNORM_BLOCK: o->dec = 6; o->vk = VK_FORMAT_R8_SNORM; o->dbpp = 1; break;
    case VK_FORMAT_BC5_UNORM_BLOCK: o->dec = 7; o->vk = VK_FORMAT_R8G8_UNORM; o->dbpp = 2; break;
    case VK_FORMAT_BC5_SNORM_BLOCK: o->dec = 8; o->vk = VK_FORMAT_R8G8_SNORM; o->dbpp = 2; break;
    case VK_FORMAT_BC6H_UFLOAT_BLOCK: o->dec = 9; o->vk = VK_FORMAT_R16G16B16A16_SFLOAT; o->dbpp = 8; break;
    case VK_FORMAT_BC6H_SFLOAT_BLOCK: o->dec = 10; o->vk = VK_FORMAT_R16G16B16A16_SFLOAT; o->dbpp = 8; break;
    default: o->dec = 11; o->vk = rgba; o->dbpp = 4; break;   // BC7
    }
}
// Scene depth images are sampled, attached and transferred. RADV Xclipse
// cc87fdf reports no D24S8 support but can accept vkCreateImage requests.
static VkFormat vk_d24s8_attachment_format(void) {
    const VkFormat candidates[] = {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT};
    VkImageUsageFlags usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                              VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    const VkFormatFeatureFlags required = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    for (unsigned i = 0; i < sizeof candidates / sizeof candidates[0]; i++) {
        VkFormatProperties features;
        vkGetPhysicalDeviceFormatProperties(V.pd, candidates[i], &features);
        if ((features.optimalTilingFeatures & required) != required) continue;
        VkImageFormatProperties props;
        if (vkGetPhysicalDeviceImageFormatProperties(V.pd, candidates[i], VK_IMAGE_TYPE_2D,
                VK_IMAGE_TILING_OPTIMAL, usage, 0, &props) == VK_SUCCESS) return candidates[i];
    }
    return VK_FORMAT_UNDEFINED;
}
// Decodes BC blocks (rows of whole blocks pitch bytes apart, (h + 3) / 4 rows a slice, z slices) into w x h x z texels of f->vk, rows of w * f->dbpp bytes
static void bc_decode(const vkfmt_t *f, const uint8_t *src, size_t pitch, uint32_t w, uint32_t h, uint32_t z, uint8_t *dst) {
    // Retain the renderer's tight-output interface; the shared decoder also
    // accepts explicit row/slice pitches and buffer bounds for copy paths.
    static const uint8_t formats[] = {0, 0x41, 0x42, 0x43, 0x44, 0x49, 0x4a, 0x4b, 0x4c, 0x50, 0x4f, 0x4d};
    if (!w || !h || !z || !f->dbpp || h > UINT32_MAX - 3 || f->dec >= sizeof formats || !formats[f->dec])
        die("[vk] invalid CPU BC decode dimensions or format");
    size_t rows = (h + 3) / 4;
    if (pitch > SIZE_MAX / rows || pitch * rows > SIZE_MAX / z ||
        (size_t)w > SIZE_MAX / f->dbpp || (size_t)w * f->dbpp > SIZE_MAX / h ||
        (size_t)w * f->dbpp * h > SIZE_MAX / z) die("[vk] CPU BC decode size overflow");
    size_t row = (size_t)w * f->dbpp, slice = pitch * rows;
    if (nvn_bc_decode(formats[f->dec], w, h, z, src, slice * z, pitch, slice,
                      dst, row * h * z, row, row * h)) die("[vk] invalid CPU BC decode layout");
}
void vk_bc_decode(const vkfmt_t *f, const uint8_t *src, size_t pitch, uint32_t w, uint32_t h, uint32_t z, uint8_t *dst) { bc_decode(f, src, pitch, w, h, z, dst); }
static pthread_mutex_t g_fmt_mu = PTHREAD_MUTEX_INITIALIZER;
const vkfmt_t *vk_fmt(int f) {
    if (f <= 0 || f >= 0x9D) return NULL;
    if (__atomic_load_n(&g_fmt_done[f], __ATOMIC_ACQUIRE)) return g_fmt[f].kind ? &g_fmt[f] : NULL;   // made already: no lock (the replay looks formats up for every texture it binds)
    pthread_mutex_lock(&g_fmt_mu);
    if (!g_fmt_done[f]) {
        vkfmt_t *o = &g_fmt[f]; memset(o, 0, sizeof *o);
        fent e = FT[f];
        if (f >= 0x79 && f <= 0x94) { int k = (f - 0x79) % 14; e = (fent){(uint16_t)(VK_FORMAT_ASTC_4x4_UNORM_BLOCK + 2 * k + (f >= 0x87)), 0, 0, K_FLOAT}; }
        if (e.vk) {
            int bw, bh; o->bpb = (uint8_t)nvn_fmt_bpb(f, &bw, &bh); o->bw = (uint8_t)bw; o->bh = (uint8_t)bh;
            o->vk = (VkFormat)(e.vk == (uint16_t)VK_FORMAT_A1B5G5R5_UNORM_PACK16_KHR ? VK_FORMAT_A1B5G5R5_UNORM_PACK16_KHR : e.vk);
            if (f == 0x33) {   // DEPTH24: X8_D24 where the device renders to it, else D32
                VkFormatProperties fp; vkGetPhysicalDeviceFormatProperties(V.pd, VK_FORMAT_X8_D24_UNORM_PACK32, &fp);
                if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) o->vk = VK_FORMAT_D32_SFLOAT;
            }
            if (f == 0x31) {   // STENCIL8 alone: a combined format where the device has no S8 attachments
                VkFormatProperties fp; vkGetPhysicalDeviceFormatProperties(V.pd, VK_FORMAT_S8_UINT, &fp);
                if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) o->vk = VK_FORMAT_D24_UNORM_S8_UINT;
            }
            if (f == 0x35) {
                o->vk = vk_d24s8_attachment_format();
                if (o->vk == VK_FORMAT_D32_SFLOAT_S8_UINT)
                    VKLOG("[depth] NVN D24S8 uses Vulkan D32F+S8 backing (D24S8 unavailable for sampled attachments)");
                else if (o->vk == VK_FORMAT_UNDEFINED)
                    VKRED_ONCE(127, "device has no usable Vulkan backing for NVN D24S8");
            }
            o->depth = f >= 0x32 && f <= 0x36; o->stencil = f == 0x31 || f == 0x35 || f == 0x36 || (f == 0x31);
            o->compressed = o->bw > 1 || o->bh > 1;
            o->is_int = e.kind == K_INT || e.kind == K_SINT; o->is_sint = e.kind == K_SINT; o->kind = o->vk != VK_FORMAT_UNDEFINED;
            o->swz = (VkComponentMapping){VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, e.noalpha ? VK_COMPONENT_SWIZZLE_ONE : VK_COMPONENT_SWIZZLE_IDENTITY};
            if (o->compressed && o->vk >= VK_FORMAT_BC1_RGB_UNORM_BLOCK && o->vk <= VK_FORMAT_BC7_SRGB_BLOCK && !V.has_bc) {
                // no BC on the device (the Adreno 618 / 619 drivers of 2021, 512.530): an uncompressed image the CPU decodes into (bc_decode), 4 to 8 times the memory
                VkFormat bc = o->vk; bc_target(o); o->compressed = 0;
                VKLOG("texture format %#x (VkFormat %d): the device has no BC textures, decoded on the CPU into VkFormat %d", f, bc, o->vk);
            }
            if (o->vk >= VK_FORMAT_ASTC_4x4_UNORM_BLOCK && o->vk <= VK_FORMAT_ASTC_12x12_SRGB_BLOCK && !V.has_astc) {
                o->kind = 0;   // said once per format: these textures went missing with nothing in the log
                VKLOG("[RED] texture format %#x (VkFormat %d) needs ASTC (textureCompressionASTC_LDR), which the device lacks: textures in it are not drawn", f, o->vk);
            }
        }
        __atomic_store_n(&g_fmt_done[f], 1, __ATOMIC_RELEASE);   // published once complete
    }
    pthread_mutex_unlock(&g_fmt_mu);
    return g_fmt[f].kind ? &g_fmt[f] : NULL;
}
static VkImageAspectFlags aspect_of(const vkfmt_t *f) {
    if (f->vk == VK_FORMAT_S8_UINT) return VK_IMAGE_ASPECT_STENCIL_BIT;
    if (f->depth || f->stencil) return VK_IMAGE_ASPECT_DEPTH_BIT | (f->vk == VK_FORMAT_D24_UNORM_S8_UINT || f->vk == VK_FORMAT_D32_SFLOAT_S8_UINT ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

// ---------------- images ----------------
static unsigned layers_of(const nvn_texdesc *d) { return d->target == 4 || d->target == 9 ? (d->depth ? d->depth : 1) : d->target == 8 ? 6 : d->target == 3 ? (d->height ? d->height : 1) : 1; }
static VkImageViewType vtype_of(int target) {
    switch (target) {
    case 0: return VK_IMAGE_VIEW_TYPE_1D; case 3: return VK_IMAGE_VIEW_TYPE_1D_ARRAY; case 2: return VK_IMAGE_VIEW_TYPE_3D;
    case 4: case 6: return VK_IMAGE_VIEW_TYPE_2D_ARRAY; case 8: return VK_IMAGE_VIEW_TYPE_CUBE; case 9: return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    default: return VK_IMAGE_VIEW_TYPE_2D;
    }
}
// storage key: textures with the same pool, offset and description share one image
// Keep a storage-capable linear allocation for each mutable-format family.
// X and A formats have identical storage; each descriptor retains its own
// sampled alpha mapping and linear/sRGB conversion in its image view.
static NVNformat texture_backing_format(NVNformat format) {
    switch (format) {
        case NVN_FORMAT_RGBX8: case NVN_FORMAT_RGBX8_SRGB: case NVN_FORMAT_RGBA8_SRGB:
            return NVN_FORMAT_RGBA8;
        case NVN_FORMAT_BGRX8: case NVN_FORMAT_BGRX8_SRGB: case NVN_FORMAT_BGRA8_SRGB:
            return NVN_FORMAT_BGRA8;
        default: return format;
    }
}
typedef struct { uint64_t pool; int64_t off; nvn_texdesc d; uint8_t family; } akey;
static nvn_hmap g_by_key = {.klen = sizeof(akey)};     // akey → vkimg_t*
static nvn_hmap g_by_serial = {.klen = sizeof(uint64_t)};  // serial → vkimg_t* (images with no storage key: no pool memory)
static pthread_mutex_t g_img_mu = PTHREAD_MUTEX_INITIALIZER;
// Allocated only for the first actual nonnative cache exchange. All mutations
// occur under g_img_mu. The published count is an O(1) replay fast path.
static nx_rs_active *g_rs_live;
// Discarded contents are undefined, but their backing can be reused at the
// next certified full overwrite. Dormant owners are not active consumers.
static nx_rs_active g_rs_dormant;
static unsigned g_rs_live_count;
static void rs_active_publish(void) {
    __atomic_store_n(&g_rs_live_count,g_rs_live?g_rs_live->n:0,__ATOMIC_RELEASE);
}
static void rs_active_drop(vkimg_t *im) {
    nx_rs_active_update(&g_rs_dormant,(uintptr_t)im,0);
    if (!g_rs_live) return;
    nx_rs_active_update(g_rs_live,(uintptr_t)im,0);
    rs_active_publish();
    if (!g_rs_live->n) { free(g_rs_live); g_rs_live=NULL; }
}
// Each owning key remains unique. Exact descriptions win, including packaged/plain
// peers. A family admits a metadata peer only after that exact format has joined;
// metadata then closes membership, while existing aliases keep their shared data.
static unsigned texture_family_bit(NVNformat format) {
    switch (format) {
    case NVN_FORMAT_RGBA8: case NVN_FORMAT_BGRA8: return 1;
    case NVN_FORMAT_RGBA8_SRGB: case NVN_FORMAT_BGRA8_SRGB: return 2;
    case NVN_FORMAT_RGBX8: case NVN_FORMAT_BGRX8: return 4;
    case NVN_FORMAT_RGBX8_SRGB: case NVN_FORMAT_BGRX8_SRGB: return 8;
    default: return 0;
    }
}
// g_img_mu held. On a miss, k selects the sole owning key of the new image.
static vkimg_t *texture_storage_lookup(akey *k, const tex_t *t) {
    vkimg_t *im = nvn_hm_get(&g_by_key, k); if (im) return im;
    unsigned bit = texture_family_bit(t->d.format);
    if (!bit || !t->mem || (t->pool->flags & NVN_MEMORY_POOL_FLAGS_VIRTUAL) || t->d.samples > 1 ||
        (t->d.target != NVN_TEXTURE_TARGET_2D && t->d.target != NVN_TEXTURE_TARGET_2D_ARRAY) ||
        (t->d.flags & (NVN_TEXTURE_FLAGS_LINEAR | NVN_TEXTURE_FLAGS_LINEAR_RENDER_TARGET))) return NULL;
    akey family = *k; family.family = 1; family.d.format = texture_backing_format(t->d.format);
    int metadata = t->has_layout || t->has_pkg_data;
    im = nvn_hm_get(&g_by_key, &family);
    if (im && ((im->family_formats & bit) || (!metadata && !im->family_closed))) {
        im->family_formats |= bit; if (metadata) im->family_closed = 1;
        return im;
    }
    if (!im && !metadata) *k = family;
    return NULL;
}
// Read-only exact lookup for scaling preflight. Canonical family storage
// is eligible only before another interpretation has joined; never mutate
// a family while determining whether its current scaled owner is safe.
static vkimg_t *rs_exact_owner(const akey *k) {
    vkimg_t *im=nvn_hm_get(&g_by_key,k);
    if (im) return im;
    if (k->d.format!=NVN_FORMAT_RGBA8 && k->d.format!=NVN_FORMAT_BGRA8) return NULL;
    akey family=*k; family.family=1;
    im=nvn_hm_get(&g_by_key,&family);
    return im && im->family_formats==1 && !im->family_closed ? im : NULL;
}
// The sRGB / UNORM twin of a format (the one reinterpretation games make of uncompressed colour targets), VK_FORMAT_UNDEFINED when it has none
static VkFormat srgb_twin(VkFormat f) {
    switch (f) {
    case VK_FORMAT_R8G8B8A8_UNORM: return VK_FORMAT_R8G8B8A8_SRGB; case VK_FORMAT_R8G8B8A8_SRGB: return VK_FORMAT_R8G8B8A8_UNORM;
    case VK_FORMAT_B8G8R8A8_UNORM: return VK_FORMAT_B8G8R8A8_SRGB; case VK_FORMAT_B8G8R8A8_SRGB: return VK_FORMAT_B8G8R8A8_UNORM;
    case VK_FORMAT_A8B8G8R8_UNORM_PACK32: return VK_FORMAT_A8B8G8R8_SRGB_PACK32; case VK_FORMAT_A8B8G8R8_SRGB_PACK32: return VK_FORMAT_A8B8G8R8_UNORM_PACK32;
    default: return VK_FORMAT_UNDEFINED;
    }
}
// the uint (sint) format of a texel size, for a storage view that reads or writes the raw bits of a float or unorm texture (gfx_vk_exec.c tex_desc_for). VK_FORMAT_UNDEFINED
// when the size has none, or the device cannot store into that format
VkFormat vk_int_fmt(uint32_t bytes, int sint) {
    VkFormat f;
    switch (bytes) {
    case 1: f = sint ? VK_FORMAT_R8_SINT : VK_FORMAT_R8_UINT; break;
    case 2: f = sint ? VK_FORMAT_R16_SINT : VK_FORMAT_R16_UINT; break;
    case 4: f = sint ? VK_FORMAT_R32_SINT : VK_FORMAT_R32_UINT; break;
    case 8: f = sint ? VK_FORMAT_R32G32_SINT : VK_FORMAT_R32G32_UINT; break;
    case 16: f = sint ? VK_FORMAT_R32G32B32A32_SINT : VK_FORMAT_R32G32B32A32_UINT; break;
    default: return VK_FORMAT_UNDEFINED;
    }
    VkFormatProperties fp; vkGetPhysicalDeviceFormatProperties(V.pd, f, &fp);
    return fp.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT ? f : VK_FORMAT_UNDEFINED;
}
// DM_VK_UBWC (start-up, bits): 1, uncompressed colour images get a format list (their format and its sRGB twin) instead of a free MUTABLE_FORMAT, which Adreno
// needs to keep UBWC framebuffer compression; views in another format then use the image's own (vk_tex_view). 2, depth / stencil images are made without
// MUTABLE_FORMAT and EXTENDED_USAGE (their views always take the image's own format). Unicorn Overlord on an Adreno 730: 1 cut GPU time by 35-40%. Both are the
// default (3): Super Mario Bros. Wonder's world map on the Adreno 830 took 8.7-8.9 ms of GPU a frame instead of 11.8, the same picture (1 alone: 9.5-10.0 ms).
// Xenoblade Chronicles 3's field took 22.9-23.0 ms against 23.0-23.6 (its own shading dominates), with clouds in every frame of a sky turn and no idle flicker;
// Graveyard Keeper 2, Advance Wars and DKCR drew the same pictures. A colour view in another format than the image's and its twin is logged [RED] (vk_tex_view):
// such a game wants DM_VK_UBWC=2 in its profile. 4 (off by default): with 1, colour images flagged as images also list the uint and the sint format of their texel
// size, for storage views that read or write their raw bits (tex_desc_for). A cross-class list may make Adreno / Turnip drop UBWC on those images, which was not
// measured, so only a profile that needs it turns it on (DM_VK_UBWC=7); without it such a view falls back to the image's own format (vk_tex_view_as)
static int ubwc_mode(void) { static int v = -1; if (v < 0) { const char *e = getenv("DM_VK_UBWC"); v = e ? atoi(e) : 3; } return v; }
static vkimg_t *img_new_description(const tex_t *t, const vkfmt_t *f, int unbound) {
    vkimg_t *im = calloc(1, sizeof *im);
    if (!im) return NULL;
    const nvn_texdesc *d = &t->d;
    im->w = d->width ? d->width : 1; im->h = d->target == 0 || d->target == 3 ? 1 : (d->height ? d->height : 1);
    im->d = d->target == 2 ? (d->depth ? d->depth : 1) : 1; im->layers = layers_of(d);
    im->levels = d->levels ? d->levels : 1; im->samples = d->samples > 1 ? d->samples : 1; im->target = d->target; im->nvn_format = d->format;
    im->fmt = f->vk; im->vtype = vtype_of(d->target); im->aspect = aspect_of(f);
    VkFormatProperties fp; vkGetPhysicalDeviceFormatProperties(V.pd, f->vk, &fp);
    VkImageUsageFlags u = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) u |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) u |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if ((d->flags & NVN_TEXTURE_FLAGS_IMAGE) && (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) u |= VK_IMAGE_USAGE_STORAGE_BIT;
    // a block-compressed texture can be drawn into through a view in the uncompressed format of its block size (Mario Kart 8 Deluxe writes BC3 blocks into its
    // reflection cubemaps through RGBA32UI views): with EXTENDED_USAGE the image may carry a usage its own format lacks, and an attachment view needs it on the image
    if (f->compressed && !f->dec && (f->bpb == 16 || f->bpb == 8)) { VkFormatProperties up; vkGetPhysicalDeviceFormatProperties(V.pd, f->bpb == 16 ? VK_FORMAT_R32G32B32A32_UINT : VK_FORMAT_R32G32_UINT, &up);
        if (up.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) u |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; }
    VkImageCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL,
        // a square single-sample 2D array with at least six layers too: shaders read one as a cube (Pokémon Legends: Arceus's reflection probes; the view was refused by the validation layer without the flag)
        VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT | (d->target == 8 || d->target == 9 || (d->target == 4 && im->layers >= 6 && im->w == im->h && im->samples <= 1) ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0) |
        (d->target == 2 ? VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT : 0) | (f->compressed ? VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT : 0),
        d->target == 2 ? VK_IMAGE_TYPE_3D : d->target == 0 || d->target == 3 ? VK_IMAGE_TYPE_1D : VK_IMAGE_TYPE_2D, f->vk,
        {im->w, im->h, im->d}, im->levels, im->layers, (VkSampleCountFlagBits)im->samples, VK_IMAGE_TILING_OPTIMAL, u, VK_SHARING_MODE_EXCLUSIVE, 0, NULL, VK_IMAGE_LAYOUT_UNDEFINED};
    if (im->samples > 1) ci.flags &= ~(VkImageCreateFlags)VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
    VkFormat fl[4] = {f->vk, srgb_twin(f->vk), 0, 0}; uint32_t nfl = fl[1] ? 2 : 1;
    VkImageFormatListCreateInfo fli = {VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO, NULL, nfl, fl};
    if (!f->compressed && (ubwc_mode() & (im->aspect == VK_IMAGE_ASPECT_COLOR_BIT ? 1 : 2))) {   // (a depth format has no twin: a list of itself, no MUTABLE_FORMAT)
        im->fixed_fmt = 1; im->fmt2 = fl[1]; ci.pNext = &fli;
        // a colour image flagged as an image may be stored into as integers (a shader declares r32ui over an RGBA8 texture, FINAL FANTASY TACTICS' UI composite:
        // tex_desc_for): with DM_VK_UBWC bit 4, its list takes the uint and the sint format of its texel size, which a storage view in them then may use
        if ((ubwc_mode() & 4) && (d->flags & NVN_TEXTURE_FLAGS_IMAGE) && im->aspect == VK_IMAGE_ASPECT_COLOR_BIT) {   // (DM_VK_UBWC bit 4)
            im->fmt_cls[0] = vk_int_fmt(f->bpb, 0); im->fmt_cls[1] = vk_int_fmt(f->bpb, 1);
            if (im->fmt_cls[0]) fl[nfl++] = im->fmt_cls[0];
            if (im->fmt_cls[1]) fl[nfl++] = im->fmt_cls[1];
            fli.viewFormatCount = nfl; }
        ci.flags &= ~(VkImageCreateFlags)VK_IMAGE_CREATE_EXTENDED_USAGE_BIT; if (nfl == 1) ci.flags &= ~(VkImageCreateFlags)VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    }
    im->usage = ci.usage; im->cflags = ci.flags;
    VkResult r = vkCreateImage(V.dev, &ci, NULL, &im->img);
    if (r && f->compressed && (ci.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {   // the driver refused the attachment usage on a compressed image: as before, without it
        VKRED_ONCE(96, "compressed image format %#x refused colour-attachment usage (%d): created without it", d->format, r);
        ci.usage &= ~(VkImageUsageFlags)VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; im->usage = ci.usage; r = vkCreateImage(V.dev, &ci, NULL, &im->img); }
    if (r) { VKRED_ONCE(10, "vkCreateImage failed (%d): %ux%ux%u format %#x (vk %d) target %d levels %u layers %u samples %u", r, im->w, im->h, im->d, d->format, f->vk, d->target, im->levels, im->layers, im->samples); free(im); return NULL; }
    // An unbound native description provides the exact native memory
    // requirement to the fresh scaled allocator without allocating it.
    im->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (unbound) return im;
    VkMemoryRequirements req; vkGetImageMemoryRequirements(V.dev, im->img, &req);
    if ((ci.usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) && im->w >= 256) { static int n; if (n++ < 16)   // UBWC check: a compressed target takes room for its metadata
        VKLOG("target %ux%u format %d%s: %llu bytes", im->w, im->h, f->vk, im->fixed_fmt ? " (format-listed)" : "", (unsigned long long)req.size); }
    VkDeviceSize moff;
    if (!(im->mem = vk_image_memory(&req, vk_mem_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT), &moff, &im->sub))) {
        VKRED_ONCE(11, "image memory allocation failed (%llu bytes)", (unsigned long long)req.size); vkDestroyImage(V.dev, im->img, NULL); free(im); return NULL; }
    VKCHK(vkBindImageMemory(V.dev, im->img, im->mem, moff));
    im->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    return im;
}
static vkimg_t *img_new(const tex_t *t,const vkfmt_t *f) {
    return img_new_description(t,f,0);
}
// P1: allocate isolated physical variants. Dedicated allocations make the
// budget equal to actual Vulkan allocation sizes, including deferred frees.
static nx_rs_budget g_rs_budget, g_rs_restore_budget;
static uint64_t g_rs_generation;
#define NX_RS_BUDGET_BYTES (64ull * 1024 * 1024)
#define NX_RS_RESTORE_BUDGET_BYTES (64ull * 1024 * 1024)
static vk_rs_variant *rs_variant_allocate(const vkimg_t *logical,uint32_t w,uint32_t h,int private_storage) {
    if (!logical || !w || !h || !vk_sub_open()) return NULL;
    VkImageUsageFlags usage=logical->usage;
    VkImageCreateFlags flags=logical->cflags;
    VkFormat list[4] = {logical->fmt,0,0,0};
    uint32_t nfmt=1;
    if (logical->fmt2) list[nfmt++]=logical->fmt2;
    for (unsigned i=0;i<2;i++) if (logical->fmt_cls[i]) list[nfmt++]=logical->fmt_cls[i];
    VkImageFormatListCreateInfo fli = {VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO,NULL,nfmt,list};
    VkImageFormatProperties props;
    if (vkGetPhysicalDeviceImageFormatProperties(V.pd, logical->fmt, VK_IMAGE_TYPE_2D,
            VK_IMAGE_TILING_OPTIMAL, usage, flags, &props) != VK_SUCCESS ||
        w > props.maxExtent.width || h > props.maxExtent.height ||
        w > V.props.limits.maxFramebufferWidth || h > V.props.limits.maxFramebufferHeight ||
        !(props.sampleCounts & VK_SAMPLE_COUNT_1_BIT)) return NULL;
    vk_rs_variant *v = calloc(1, sizeof *v);
    if (!v) return NULL;
    atomic_init(&v->refs, 1);
    v->logical_w = logical->w; v->logical_h = logical->h;
    v->physical_w = w; v->physical_h = h; v->format = logical->fmt; v->aspect = logical->aspect;
    v->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, logical->fixed_fmt ? &fli : NULL, flags, VK_IMAGE_TYPE_2D,
        v->format, {w,h,1}, 1, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL,
        usage, VK_SHARING_MODE_EXCLUSIVE, 0, NULL, VK_IMAGE_LAYOUT_UNDEFINED};
    if (vkCreateImage(V.dev, &ci, NULL, &v->image) != VK_SUCCESS) goto fail;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(V.dev, v->image, &req);
    if (private_storage && (!req.size || req.size>(1u<<20))) goto fail;
    uint32_t type = vk_mem_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    if (type == UINT32_MAX) goto fail;
    if (logical->rs_backing) v->native_bytes=logical->rs_backing->native_bytes;
    else if (logical->img && !logical->sub) {
        VkMemoryRequirements native_req;
        vkGetImageMemoryRequirements(V.dev,logical->img,&native_req);
        v->native_bytes=native_req.size;
    }
    int restore=logical->rs_backing && w==logical->w && h==logical->h &&
        (logical->rs_backing->physical_w!=logical->w || logical->rs_backing->physical_h!=logical->h);
    if (restore) {
        if (!nx_rs_restore_admit(v->native_bytes,req.size) ||
            !nx_rs_reserve(&g_rs_restore_budget,req.size,NX_RS_RESTORE_BUDGET_BYTES)) goto fail;
        v->restore_budget=1;
    } else if (!nx_rs_reserve(&g_rs_budget, req.size, NX_RS_BUDGET_BYTES)) goto fail;
    v->bytes = req.size;
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, req.size, type};
    if (vkAllocateMemory(V.dev, &ai, NULL, &v->memory) != VK_SUCCESS) { v->memory = VK_NULL_HANDLE; goto fail; }
    if (vkBindImageMemory(V.dev, v->image, v->memory, 0) != VK_SUCCESS) goto fail;
    VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, v->image,
        VK_IMAGE_VIEW_TYPE_2D, v->format, {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY}, {v->aspect, 0, 1, 0, 1}};
    if (vkCreateImageView(V.dev, &vi, NULL, &v->view) != VK_SUCCESS) { v->view = VK_NULL_HANDLE; goto fail; }
    v->generation = ++g_rs_generation;
    return v;
fail:
    vk_rs_variant_destroy(v); // never recorded or published
    return NULL;
}
vk_rs_variant *vk_rs_variant_create(const vkimg_t *logical,uint32_t w,uint32_t h) {
    if (!logical || !w || !h || !vk_sub_open() || logical->novl || logical->packaged ||
        logical->lin_mem || logical->target != NVN_TEXTURE_TARGET_2D ||
        logical->d != 1 || logical->layers != 1 || logical->levels != 1 || logical->samples != 1 ||
        logical->bc_draw || logical->bc_of || !logical->pool ||
        (logical->flags & (NVN_TEXTURE_FLAGS_LINEAR | NVN_TEXTURE_FLAGS_SPARSE | NVN_TEXTURE_FLAGS_LINEAR_RENDER_TARGET))) return NULL;
    const vkfmt_t *fmt = vk_fmt(logical->nvn_format);
    if (!fmt || fmt->compressed || fmt->is_int || !logical->aspect) return NULL;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    int storage=(logical->flags&NVN_TEXTURE_FLAGS_IMAGE)!=0 && logical->aspect==VK_IMAGE_ASPECT_COLOR_BIT;
    if (storage) {
        // Admit canonical float/UNORM color storage only. Actual writes
        // require a matching SPIR-V declaration and certified dispatch.
        if (!nx_rs_storage_spv_format(logical->nvn_format) || logical->fmt!=fmt->vk) return NULL;
        usage|=VK_IMAGE_USAGE_STORAGE_BIT;
    } else {
        usage |= logical->aspect == VK_IMAGE_ASPECT_COLOR_BIT ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        if (logical->usage & VK_IMAGE_USAGE_STORAGE_BIT) return NULL;
    }
    if ((logical->usage & usage) != usage) return NULL;
    return rs_variant_allocate(logical,w,h,0);
}
// A private storage control has no guest pool, address or cache owner. It
// shares the budget and cleanup machinery without relaxing live eligibility.
vk_rs_variant *vk_rs_storage_variant_create_private(uint32_t lw,uint32_t lh,uint32_t w,uint32_t h) {
    if (!nx_rs_private_storage_extent(lw,lh,w,h) || !vk_sub_open() || !vk_rs_pass_idle()) return NULL;
    const vkimg_t logical={
        .usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .fmt=VK_FORMAT_R32G32B32A32_SFLOAT,.aspect=VK_IMAGE_ASPECT_COLOR_BIT,
        .w=lw,.h=lh,.d=1,.levels=1,.layers=1,.samples=1,.target=NVN_TEXTURE_TARGET_2D
    };
    return rs_variant_allocate(&logical,w,h,1);
}
void vk_rs_variant_destroy(vk_rs_variant *v) {
    if (!v || atomic_fetch_sub(&v->refs, 1) != 1) return;
    if (v->view) vkDestroyImageView(V.dev, v->view, NULL);
    if (v->image) vkDestroyImage(V.dev, v->image, NULL);
    if (v->memory) vkFreeMemory(V.dev, v->memory, NULL);
    if (v->bytes) {
        if (v->restore_budget) nx_rs_release(&g_rs_restore_budget,v->bytes);
        else nx_rs_release(&g_rs_budget, v->bytes);
    }
    free(v);
}
void vk_rs_variant_clear(VkCommandBuffer cb, vk_rs_variant *v) {
    if (!v || v->layout != VK_IMAGE_LAYOUT_UNDEFINED) return;
    VkImageSubresourceRange range = {v->aspect, 0, 1, 0, 1};
    VkImageMemoryBarrier b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, NULL, 0,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
        VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, v->image, range};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 1, &b);
    if (v->aspect == VK_IMAGE_ASPECT_COLOR_BIT) {
        VkClearColorValue value = {{0,0,0,0}};
        vkCmdClearColorImage(cb, v->image, VK_IMAGE_LAYOUT_GENERAL, &value, 1, &range);
    } else {
        VkClearDepthStencilValue value = {0.625f, 0};
        vkCmdClearDepthStencilImage(cb, v->image, VK_IMAGE_LAYOUT_GENERAL, &value, 1, &range);
    }
    vk_img_barrier_all(cb);
    v->layout = VK_IMAGE_LAYOUT_GENERAL;
}
// Private variants have one authoritative image per attachment. A scale
// change creates replacements only for changed extents; failure cannot mutate
// the published group. Guest textures are not yet routed through this API.
static int rs_can_copy(const vk_rs_variant *src, const vk_rs_variant *dst) {
    if (src->layout != VK_IMAGE_LAYOUT_GENERAL || src->format != dst->format ||
        src->aspect != dst->aspect || src->logical_w != dst->logical_w ||
        src->logical_h != dst->logical_h || src->image == dst->image) return 0;
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(V.pd, src->format, &fp);
    const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
    return (fp.optimalTilingFeatures & need) == need;
}
static void rs_copy(VkCommandBuffer cb, const vk_rs_variant *src, vk_rs_variant *dst) {
    // All formats, allocation and source layouts were checked before recording.
    VkImageSubresourceRange range = {dst->aspect, 0, 1, 0, 1};
    VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, NULL,
        0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
        VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, dst->image, range};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 1, &barrier);
    // Depth and stencil must be separate regions, with identical formats and
    // nearest filtering. Do not assume all mobile formats support blitting.
    const VkImageAspectFlags aspects[] = {VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_ASPECT_STENCIL_BIT};
    for (unsigned i = 0; i < 3; i++) if (src->aspect & aspects[i]) {
        VkImageBlit blit = {{aspects[i],0,0,1}, {{0,0,0},{(int32_t)src->physical_w,(int32_t)src->physical_h,1}},
                            {aspects[i],0,0,1}, {{0,0,0},{(int32_t)dst->physical_w,(int32_t)dst->physical_h,1}}};
        vkCmdBlitImage(cb, src->image, VK_IMAGE_LAYOUT_GENERAL, dst->image,
                       VK_IMAGE_LAYOUT_GENERAL, 1, &blit, VK_FILTER_NEAREST);
    }
    dst->layout = VK_IMAGE_LAYOUT_GENERAL;
}
int vk_rs_group_migrate(vk_rs_images *group, const vkimg_t *const logical[VK_RS_GROUP_SIZE], uint32_t w, uint32_t h) {
    if (!group || !logical || !w || !h || w > INT32_MAX || h > INT32_MAX || !vk_sub_open()) return 0;
    vk_rs_images staged = {0};
    unsigned live = 0, changed = 0;
    // Validate slot identity and allocate all changed attachments before
    // recording any commands. Logical extents do not become physical extents.
    for (unsigned i = 0; i < VK_RS_GROUP_SIZE; i++) {
        const vkimg_t *im = logical[i];
        vk_rs_variant *old = group->slot[i];
        if (!im) { if (old) goto rollback; continue; }
        live++;
        if (old) {
            if (old->logical_w != im->w || old->logical_h != im->h ||
                old->format != im->fmt || old->aspect != im->aspect ||
                old->layout != VK_IMAGE_LAYOUT_GENERAL) goto rollback;
            if (old->physical_w == w && old->physical_h == h) continue;
        }
        staged.slot[i] = vk_rs_variant_create(im, w, h);
        if (!staged.slot[i] || (old && !rs_can_copy(old, staged.slot[i]))) goto rollback;
        changed++;
    }
    if (!live) return 0;
    if (!changed) return 1; // no copies, allocations, barriers or retirement
    VkCommandBuffer cb = vk_cmd();
    vk_img_barrier_all(cb); // previous uses of all old representations
    for (unsigned i = 0; i < VK_RS_GROUP_SIZE; i++) if (staged.slot[i]) {
        if (group->slot[i]) rs_copy(cb, group->slot[i], staged.slot[i]);
        else vk_rs_variant_clear(cb, staged.slot[i]);
    }
    vk_img_barrier_all(cb);
    for (unsigned i = 0; i < VK_RS_GROUP_SIZE; i++) if (staged.slot[i]) {
        vk_rs_variant *old = group->slot[i];
        group->slot[i] = staged.slot[i];
        if (old) vk_rs_variant_retire(old);
    }
    return 2;
rollback:
    for (unsigned i = 0; i < VK_RS_GROUP_SIZE; i++) vk_rs_variant_destroy(staged.slot[i]);
    return 0;
}
void vk_rs_group_retire(vk_rs_images *group) {
    if (!group) return;
    for (unsigned i = 0; i < VK_RS_GROUP_SIZE; i++) {
        if (group->slot[i]) vk_rs_variant_retire(group->slot[i]);
        group->slot[i] = NULL;
    }
}
static void vk_tex_view_drop_all(vkimg_t *im);
// Shared publication step: called only after every replacement is allocated,
// validated and recorded. Old backing and descriptor views retire by fence.
static void rs_publish_backing(vkimg_t *im, vk_rs_variant *next) {
    vk_rs_variant *old=im->rs_backing;
    vk_lrz_forget(im); vk_tex_view_drop_all(im);
    if (old) vk_rs_variant_retire(old);
    else vk_defer_free_image(im->img,im->mem);
    im->img=next->image; im->mem=next->memory; im->layout=next->layout;
    im->rs_backing=next; im->depth_sub=im->depth_cut=im->fdz_frame=0;
    // Guest storage keys, logical extents and content provenance are unchanged.
}
// Publish a replacement backing into the existing cache-owner object.
// The caller must have proved ALL consumers and hold exclusive ownership;
// the current only caller uses detached copies, never live guest storage.
int vk_rs_cache_exchange(vkimg_t *const owners[VK_RS_GROUP_SIZE], uint32_t w, uint32_t h) {
    if (!owners || !vk_sub_open()) return 0;
    nx_rs_backing_state states[VK_RS_GROUP_SIZE] = {0};
    vk_rs_variant sources[VK_RS_GROUP_SIZE] = {0};
    vk_rs_images staged = {0};
    for (unsigned i=0;i<VK_RS_GROUP_SIZE;i++) {
        vkimg_t *im=owners[i]; if (!im) continue;
        if (!im->img || !im->mem || im->sub || im->full || im->layout!=VK_IMAGE_LAYOUT_GENERAL ||
            im->novl || im->packaged || im->lin_mem || im->bc_draw || im->bc_of ||
            im->target!=NVN_TEXTURE_TARGET_2D || im->levels!=1 || im->layers!=1 || im->samples!=1 || im->d!=1) return 0;
        vk_rs_variant *old=im->rs_backing;
        if (old && old->native_only && (w!=im->w || h!=im->h)) return 0;
        if (old && (old->image!=im->img || old->memory!=im->mem || old->logical_w!=im->w ||
                    old->logical_h!=im->h || old->format!=im->fmt || old->aspect!=im->aspect ||
                    old->layout!=im->layout)) return 0;
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(V.dev,im->img,&req);
        states[i]=(nx_rs_backing_state){(uintptr_t)im,im->w,im->h,old?old->physical_w:im->w,old?old->physical_h:im->h,req.size};
        sources[i]=(vk_rs_variant){.image=im->img,.memory=im->mem,.layout=im->layout,.format=im->fmt,.aspect=im->aspect,
            .logical_w=im->w,.logical_h=im->h,.physical_w=states[i].pw,.physical_h=states[i].ph};
    }
    unsigned changed=0;
    if (!nx_rs_exchange_plan(states,VK_RS_GROUP_SIZE,w,h,&changed)) return 0;
    if (!changed) return 1;
    for (unsigned i=0;i<VK_RS_GROUP_SIZE;i++) if (changed & (1u<<i)) {
        vk_rs_variant *next=vk_rs_variant_create(owners[i],w,h);
        staged.slot[i]=next;
        if (!next || !nx_rs_exchange_benefit(&states[i],w,h,next->bytes) ||
            !rs_can_copy(&sources[i],next)) goto rollback;
    }
    // No failures after recording begins. Retired generations keep their
    // budget until fence retirement, and old descriptors remain alive.
    VkCommandBuffer cb=vk_cmd();
    vk_img_barrier_all(cb);
    for (unsigned i=0;i<VK_RS_GROUP_SIZE;i++) if (staged.slot[i]) rs_copy(cb,&sources[i],staged.slot[i]);
    vk_img_barrier_all(cb);
    for (unsigned i=0;i<VK_RS_GROUP_SIZE;i++) if (staged.slot[i]) {
        rs_publish_backing(owners[i],staged.slot[i]);
    }
    return 2;
rollback:
    for (unsigned i=0;i<VK_RS_GROUP_SIZE;i++) vk_rs_variant_destroy(staged.slot[i]);
    return 0;
}
static int rs_is_scaled(const vkimg_t *im) {
    return im && im->rs_backing &&
        (im->rs_backing->physical_w!=im->w || im->rs_backing->physical_h!=im->h);
}
// Snapshot direct storage while the texture and pool objects are available.
// Never dereference an old pool pointer from an active cache owner.
static uint64_t rs_guest_address(const tex_t *t) {
    if (!t || !t->pool || !t->pool->mem || !t->mem || t->off<0 || !t->L.size ||
        (t->pool->flags & NVN_MEMORY_POOL_FLAGS_VIRTUAL)) return 0;
    uint64_t base=(uint64_t)(uintptr_t)t->pool->mem, off=(uint64_t)t->off, n=t->L.size;
    if (off>t->pool->size || n>t->pool->size-off || off>UINT64_MAX-base) return 0;
    uint64_t addr=base+off;
    return addr && addr==(uint64_t)(uintptr_t)t->mem && n<=UINT64_MAX-addr ? addr : 0;
}
static int ro_live_group_admit(vkimg_t *const owners[VK_RS_GROUP_SIZE]);
// Explicit bridge into cache ownership. Private probes do not enter this
// index. The caller still needs a complete consumer/lifetime proof.
static int rs_exchange_live_locked(vkimg_t *const owners[VK_RS_GROUP_SIZE],uint32_t w,uint32_t h) {
    if (!owners || !vk_sub_open() || !vk_rs_pass_idle()) return 0;
    nx_rs_active staged={0};
    if (g_rs_live) staged=*g_rs_live;
    for (unsigned i=0;i<VK_RS_GROUP_SIZE;i++) {
        vkimg_t *im=owners[i]; if (!im) continue;
        unsigned matches=0;
        int nonnative=w!=im->w || h!=im->h;
        if (nonnative && (!im->rs_guest_addr || im->size<=0)) return 0;
        // Slow membership/alias check only during an exchange, not a draw.
        for (uint32_t j=0;j<g_by_key.cap;j++) {
            vkimg_t *o=g_by_key.vals[j]; if (!o) continue;
            if (o==im) { matches++; continue; }
            if (nonnative && (!o->rs_guest_addr || o->size<=0 ||
                nx_rs_range_overlap(o->rs_guest_addr,(uint64_t)o->size,im->rs_guest_addr,(uint64_t)im->size))) return 0;
        }
        if (matches!=1 || !nx_rs_active_update(&staged,(uintptr_t)im,w!=im->w || h!=im->h)) return 0;
    }
    // Do not admit a scaled set whose eventual native restore is already
    // impossible under the explicit restore ceiling. Charge the original
    // dedicated requirements (upper bounds), plus still-live and fence-retired
    // restoration allocations. This is accounting, not a Vulkan OOM guarantee.
    int downscale=0;
    for (unsigned i=0;i<VK_RS_GROUP_SIZE;i++) if (owners[i] &&
        (w!=owners[i]->w || h!=owners[i]->h)) { downscale=1; break; }
    if (downscale) {
        if (!ro_live_group_admit(owners)) return 0;
        uint64_t needed=atomic_load(&g_rs_restore_budget.bytes);
        for (unsigned i=0;i<staged.n;i++) {
            vkimg_t *im=(vkimg_t *)staged.owner[i];
            uint64_t bytes=0;
            if (!im->img || !im->mem || im->sub) return 0;
            if (im->rs_backing) bytes=im->rs_backing->native_bytes;
            else {
                VkMemoryRequirements req;
                vkGetImageMemoryRequirements(V.dev,im->img,&req);
                bytes=req.size;
            }
            if (!bytes || needed>NX_RS_RESTORE_BUDGET_BYTES ||
                bytes>NX_RS_RESTORE_BUDGET_BYTES-needed) return 0;
            needed+=bytes;
        }
    }
    nx_rs_active *allocated=NULL;
    if (!g_rs_live && staged.n && !(allocated=malloc(sizeof *allocated))) return 0;
    int result=vk_rs_cache_exchange(owners,w,h);
    if (!result) { free(allocated); return 0; }
    if (allocated) g_rs_live=allocated;
    if (g_rs_live) *g_rs_live=staged;
    rs_active_publish();
    if (g_rs_live && !g_rs_live->n) { free(g_rs_live); g_rs_live=NULL; }
    return result;
}
int vk_rs_cache_exchange_live(vkimg_t *const owners[VK_RS_GROUP_SIZE],uint32_t w,uint32_t h) {
    // The historical ownership graph is a rejection filter, not a lifetime
    // certificate. Until a replay lifetime proof is available, this public
    // entry cannot destructively downscale a real guest owner.
    if (!owners) return 0;
    for (unsigned i=0;i<VK_RS_GROUP_SIZE;i++)
        if (owners[i] && (w!=owners[i]->w || h!=owners[i]->h)) return 0;
    pthread_mutex_lock(&g_img_mu);
    int result=rs_exchange_live_locked(owners,w,h);
    pthread_mutex_unlock(&g_img_mu);
    return result;
}
// A proven complete overwrite does not need the old texels. Prepare only
// the new image layout; the caller immediately records the actual clear.
static void rs_prepare_overwrite(vk_rs_variant *v) {
    VkImageMemoryBarrier b={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,NULL,0,VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED,
        v->image,{v->aspect,0,1,0,1}};
    vkCmdPipelineBarrier(vk_cmd(),VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,0,NULL,0,NULL,1,&b);
    v->layout=VK_IMAGE_LAYOUT_GENERAL;
}
// No blit, source layout dependency, or zero-fill. Allocation failure must
// leave the old cache owner and active index untouched.
static int rs_discard_native_locked(vkimg_t *im,int overwrite) {
    if (!rs_is_scaled(im) || !g_rs_live || g_rs_live->n>NX_RS_ACTIVE_MAX) return 0;
    unsigned matches=0;
    for (unsigned i=0;i<g_rs_live->n;i++) matches+=g_rs_live->owner[i]==(uintptr_t)im;
    if (matches!=1) return 0;
    vk_rs_variant *next=vk_rs_variant_create(im,im->w,im->h);
    if (!next) return 0;
    next->native_only=1;
    // A clear follows immediately only for the overwrite case. Explicit
    // discard leaves the new image undefined until normal preparation.
    if (overwrite) rs_prepare_overwrite(next);
    rs_publish_backing(im,next);
    rs_active_drop(im);
    return 1;
}
static int rs_require_native_locked(vkimg_t *im) {
    if (!rs_is_scaled(im)) return 1;
    if (!vk_sub_open() || !vk_rs_pass_idle()) return 0;
    vkimg_t *owners[VK_RS_GROUP_SIZE]={im};
    if (!rs_exchange_live_locked(owners,im->w,im->h)) return 0;
    if (rs_is_scaled(im) || !im->rs_backing) return 0;
    im->rs_backing->native_only=1;
    return 1;
}
int vk_rs_require_native(vkimg_t *im) {
    if (!rs_is_scaled(im)) return 1;
    pthread_mutex_lock(&g_img_mu);
    int result=rs_require_native_locked(im);
    pthread_mutex_unlock(&g_img_mu);
    return result;
}
uint64_t vk_rs_backing_bytes(void) {
    return atomic_load(&g_rs_budget.bytes) + atomic_load(&g_rs_restore_budget.bytes);
}

int vk_rs_cache_has_scaled(void) {
    return __atomic_load_n(&g_rs_live_count,__ATOMIC_ACQUIRE)!=0;
}
// Absolute guest address comparison also catches separate pool objects that
// map overlapping memory. Missing range provenance fails closed.
int vk_rs_preflight_buffer(uint64_t addr,uint64_t size) {
    if (!vk_rs_cache_has_scaled()) return 1;
    if (!addr || !size || size>UINT64_MAX-addr) return 0;
    int ok=1;
    pthread_mutex_lock(&g_img_mu);
    for (unsigned i=0;g_rs_live && i<g_rs_live->n;i++) {
        vkimg_t *im=(vkimg_t *)g_rs_live->owner[i];
        if (!rs_is_scaled(im) || !im->rs_guest_addr || im->size<=0 ||
            nx_rs_range_overlap(addr,size,im->rs_guest_addr,(uint64_t)im->size)) { ok=0; break; }
    }
    pthread_mutex_unlock(&g_img_mu);
    return ok;
}
// Native-only consumers may require unrelated image sizes. Stage the entire
// active set before any copy or publication, rather than calling the
// equal-extent attachment transaction repeatedly. The cache lock protects
// membership and ownership for the full transaction.
static int rs_restore_all_locked(void) {
    if (!g_rs_live || !g_rs_live->n) return 1;
    unsigned n=g_rs_live->n;
    if (n>NX_RS_ACTIVE_MAX) return 0;
    vk_rs_variant *staged[NX_RS_ACTIVE_MAX]={0};
    vk_rs_variant sources[NX_RS_ACTIVE_MAX]={0};
    for (unsigned i=0;i<n;i++) {
        vkimg_t *im=(vkimg_t *)g_rs_live->owner[i];
        if (!rs_is_scaled(im) || !im->img || !im->mem || im->sub || im->full ||
            im->layout!=VK_IMAGE_LAYOUT_GENERAL || !im->w || !im->h ||
            im->w>INT32_MAX || im->h>INT32_MAX) goto rollback;
        vk_rs_variant *old=im->rs_backing;
        if (old->image!=im->img || old->memory!=im->mem ||
            old->logical_w!=im->w || old->logical_h!=im->h ||
            old->format!=im->fmt || old->aspect!=im->aspect ||
            old->layout!=im->layout || !old->physical_w || !old->physical_h ||
            old->physical_w>INT32_MAX || old->physical_h>INT32_MAX) goto rollback;
        for (unsigned j=0;j<i;j++) if (g_rs_live->owner[j]==g_rs_live->owner[i]) goto rollback;
        sources[i]=*old;
        staged[i]=vk_rs_variant_create(im,im->w,im->h);
        if (!staged[i] || !rs_can_copy(&sources[i],staged[i])) goto rollback;
        staged[i]->native_only=1;
    }
    // Every allocation and format check succeeded. Do not mutate any owner
    // or the active index before this point.
    VkCommandBuffer cb=vk_cmd();
    vk_img_barrier_all(cb);
    for (unsigned i=0;i<n;i++) rs_copy(cb,&sources[i],staged[i]);
    vk_img_barrier_all(cb);
    for (unsigned i=0;i<n;i++)
        rs_publish_backing((vkimg_t *)g_rs_live->owner[i],staged[i]);
    free(g_rs_live); g_rs_live=NULL;
    rs_active_publish();
    return 1;
rollback:
    for (unsigned i=0;i<n;i++) vk_rs_variant_destroy(staged[i]);
    return 0;
}
int vk_rs_cache_restore_all(void) {
    if (!vk_rs_cache_has_scaled()) return 1;
    if (!vk_sub_open() || !vk_rs_pass_idle()) return 0;
    pthread_mutex_lock(&g_img_mu);
    int ok=rs_restore_all_locked();
    pthread_mutex_unlock(&g_img_mu);
    return ok;
}
void vk_tex_prepare(VkCommandBuffer cb, vkimg_t *im) {
    if (im->layout == VK_IMAGE_LAYOUT_GENERAL) return;
    VkImageMemoryBarrier b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, NULL, 0, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
        VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, im->img, {im->aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS}};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, &b);
    im->layout = VK_IMAGE_LAYOUT_GENERAL;
    if (im->rs_backing) im->rs_backing->layout=im->layout;
    // A new image's contents are undefined in Vulkan, where new Switch memory reads 0 (nxnative's Metal backend zeroes new textures too). Xenoblade Chronicles 3's water keeps
    // a texture that builds on its previous frame: made again by the switch to docked mode, it started from leftover memory, and blocks of it stayed on the water from then on.
    // So an image starts as zeros, before an upload or a draw replaces them. Block-compressed images can't be cleared; they are always uploaded
    const vkfmt_t *f = vk_fmt(im->nvn_format);
    if (!(im->usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) || (f && f->compressed)) return;
    VkImageSubresourceRange r = {im->aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};
    if (im->aspect & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) { VkClearDepthStencilValue z = {0, 0}; vkCmdClearDepthStencilImage(cb, im->img, VK_IMAGE_LAYOUT_GENERAL, &z, 1, &r); }
    else { VkClearColorValue z = {{0, 0, 0, 0}}; vkCmdClearColorImage(cb, im->img, VK_IMAGE_LAYOUT_GENERAL, &z, 1, &r); }
    VkMemoryBarrier mb = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, NULL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
}
// Upload a packaged texture from guest memory: each level and layer deswizzled into scratch memory, copied in by the upload command buffer.
// A texture of a sparse virtual pool (hle_nvn.c: OCTOPATH TRAVELER streams each texture into a virtual pool of its own and maps its tiles from physical pools) has no memory at its
// address on Android: private memory cannot be aliased on Linux (darwin_compat.c vm_remap), so its bytes are read through the pool's page table (nvn_sparse_copy, unmapped pages as 0)
// and uploaded again when a mapping in its range changes or the CPU writes a physical page behind it (nvn_sparse_since), as the Mac's replay does
int nvn_sparse_pool(const void *pool); uint64_t nvn_sparse_gen(void); uint64_t nvn_sparse_copy(const void *pool, void *dst, uint64_t a, uint64_t n);
int nvn_sparse_since(const void *pool, uint64_t a, uint64_t n, uint64_t gen, uint64_t stamp); void nvn_sparse_protect(const void *pool, uint64_t a, uint64_t n);
static int sparse_tex(const tex_t *t) { return t->pool && nvn_sparse_pool(t->pool); }
uint64_t g_pkg_n, g_pkg_bytes, g_pkg_ns;   // packaged texture uploads: count, bytes, CPU time (the replay's per-frame line)
static void pkg_upload_(vkimg_t *im, const tex_t *t, const vkfmt_t *f);
static uint64_t pkg_hash(const uint8_t *m, size_t n) {   // FNV-1a over 8-byte words
    uint64_t h = 1469598103934665603ull; size_t i = 0;
    for (; i + 8 <= n; i += 8) { uint64_t w; memcpy(&w, m + i, 8); h = (h ^ w) * 1099511628211ull; }
    for (; i < n; i++) h = (h ^ m[i]) * 1099511628211ull;
    return h;
}
static void pkg_upload(vkimg_t *im, const tex_t *t, const vkfmt_t *f) {
    struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
    pkg_upload_(im, t, f);
    clock_gettime(CLOCK_MONOTONIC, &b); g_pkg_ns += (uint64_t)((b.tv_sec - a.tv_sec) * 1000000000ll + (b.tv_nsec - a.tv_nsec)); g_pkg_n++; g_pkg_bytes += t->L.size;
}
// Convert tight guest D24S8 words to the two host transfer planes. Depth
// occupies the same four-byte stride, so the upload can reuse its staging.
static void vk_d24s8_unpack_upload(uint8_t *depth, uint8_t *stencil, size_t n) {
    for (size_t i = 0; i < n; i++) {
        uint32_t packed;
        memcpy(&packed, depth + i * 4, 4);
        stencil[i] = (uint8_t)(packed >> 24);
        float z = (float)(packed & 0xffffffu) / 16777215.f;
        memcpy(depth + i * 4, &z, 4);
    }
}
static void pkg_upload_(vkimg_t *im, const tex_t *t, const vkfmt_t *f) {
    { static int n; const char *e = getenv("DM_VK_LOG_UPLOADS"); if (e && atoi(e) && n < 60) { n++;   // which packaged textures are (re-)uploaded
        VKLOG("upload: serial %llu %ux%ux%u nvn format %#x (vk %d) target %d levels %u layers %u flags %#x stride %u size %llu mem %p, tileH*100+pkg_block_h %d%s",
              (unsigned long long)t->serial, t->d.width, t->d.height, t->d.depth, t->d.format, f->vk, t->d.target, im->levels, im->layers, t->d.flags, t->d.stride, (unsigned long long)t->L.size,
              (const void *)t->mem, (int)t->L.tileH * 100 + t->d.pkg_block_h, im->packaged > 1 ? " (again)" : ""); } }
    im->guest_clear_write = 0;
    if (im->packaged < 100) im->packaged++;
    VkCommandBuffer cb = vk_upload_cmd();
    if (im->layout != VK_IMAGE_LAYOUT_GENERAL) vk_tex_prepare(cb, im);
    else {
        // A streamed replacement must follow any GPU use of the old contents.
        VkImageMemoryBarrier b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, NULL,
            VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED, im->img,
            {im->aspect, 0, im->levels, 0, im->layers}};
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, NULL, 0, NULL, 1, &b);
    }
    const uint8_t *mem = t->mem; uint8_t *copy = NULL;
    if ((im->pkg_sparse = sparse_tex(t))) {   // through the page table (see above); its physical pages stay protected, so no sweep releases them (pkg_mem NULL)
        im->pkg_vgen = nvn_sparse_gen(); nvn_sparse_protect(t->pool, (uint64_t)(uintptr_t)t->mem, t->L.size); im->pkg_stamp = nvn_dirty_now(); im->pkg_seen = im->pkg_stamp;
        im->pkg_span = NULL; im->pkg_mem = NULL; im->pkg_len = 0; im->pkg_released = 0;
        if (!(copy = malloc(t->L.size))) return;
        uint64_t miss = nvn_sparse_copy(t->pool, copy, (uint64_t)(uintptr_t)t->mem, t->L.size); mem = copy;
        { static int n; const char *e = getenv("DM_VK_LOG_SPARSE"); if (e && atoi(e) && n < 400) { n++;   // which sparse textures are read, and how much of them was not mapped yet
            VKLOG("sparse upload: serial %llu %ux%ux%u format %#x levels %u flags %#x size %#llx mem %p pool %p tileH %u: %#llx bytes not mapped%s", (unsigned long long)t->serial, t->d.width, t->d.height,
                  t->d.depth, t->d.format, im->levels, t->d.flags, (unsigned long long)t->L.size, (const void *)t->mem, t->pool, t->L.tileH, (unsigned long long)miss, im->packaged > 1 ? " (again)" : ""); } }
    } else {
        nvn_dirty_protect(t->mem, t->L.size);
        im->pkg_stamp = nvn_dirty_now(); im->pkg_span = nvn_dirty_span(t->mem, t->L.size); im->pkg_seen = im->pkg_stamp;
        im->pkg_mem = t->mem; im->pkg_len = t->L.size; im->pkg_released = 0; im->pkg_hash = pkg_hash(t->mem, t->L.size);
    }
    int linear = (t->d.flags & (NVN_TEXTURE_FLAGS_LINEAR | NVN_TEXTURE_FLAGS_LINEAR_RENDER_TARGET)) != 0;
    unsigned bw = f->bw ? f->bw : 1, bh = f->bh ? f->bh : 1, bpb = f->bpb;
    if (!bpb) { free(copy); VKRED_ONCE(12, "packaged texture format %#x has no block size", t->d.format); return; }
    for (unsigned lv = 0; lv < im->levels; lv++) {
        uint32_t w = im->w >> lv ? im->w >> lv : 1, h = im->h >> lv ? im->h >> lv : 1, z = im->d >> lv ? im->d >> lv : 1;
        uint32_t bx = (w + bw - 1) / bw, by = (h + bh - 1) / bh; size_t pitch = (size_t)bx * bpb, slab = pitch * by * z;
        for (unsigned ly = 0; ly < im->layers; ly++) {
            VkBuffer sb; VkDeviceSize so; uint8_t *blocks = f->dec ? malloc(slab) : NULL, *dst = blocks ? blocks : vk_scratch(slab, 16, &sb, &so);
            if (f->dec && !blocks) { free(copy); return; }
            if (linear) {   // linear storage: rows of the texture's stride, one level, one layer
                size_t stride = t->d.stride ? t->d.stride : pitch;
                for (uint32_t y = 0; y < by * z; y++) memcpy(dst + y * pitch, mem + (size_t)y * stride, pitch);
            } else if (nvn_deswizzle(&t->d, lv, ly, mem, dst, pitch)) { free(blocks); free(copy); VKRED_ONCE(13, "deswizzle refused texture format %#x target %d", t->d.format, t->d.target); return; }
            if (blocks) { dst = vk_scratch((size_t)w * h * z * f->dbpp, 16, &sb, &so); bc_decode(f, blocks, pitch, w, h, z, dst); free(blocks); }   // (BC the device lacks)
            VkBufferImageCopy r = {so, 0, 0, {im->aspect & VK_IMAGE_ASPECT_COLOR_BIT ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT, lv, ly, 1}, {0, 0, 0}, {w, h, z}};
            if (t->d.format == 0x35 && im->fmt == VK_FORMAT_D32_SFLOAT_S8_UINT) {
                VkBuffer stencil_buffer; VkDeviceSize stencil_offset;
                uint8_t *stencil = vk_scratch(slab / 4, 16, &stencil_buffer, &stencil_offset);
                vk_d24s8_unpack_upload(dst, stencil, slab / 4);
                VkBufferImageCopy sr = r;
                sr.bufferOffset = stencil_offset;
                sr.imageSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
                vkCmdCopyBufferToImage(cb, stencil_buffer, im->img, VK_IMAGE_LAYOUT_GENERAL, 1, &sr);
            }
            vkCmdCopyBufferToImage(cb, sb, im->img, VK_IMAGE_LAYOUT_GENERAL, 1, &r);
        }
    }
    free(copy);
}
static int packaged_tex(const tex_t *t);
static int cpu_content_tex(const tex_t *t, const vkfmt_t *f);
static int uncached_material_storage(const tex_t *t);
static int lin_copy(vkimg_t *im, const tex_t *t, const vkfmt_t *f);
// Audit only: sticky lifetime restrictions propagate across attachment groups.
// Do not use an observed group as proof of future accesses. Pointer identity
// is removed at image destruction, so reuse does not inherit the old lifetime.
int g_vk_rs_tracking;
static nx_ro_graph *g_ro;
static int g_ro_oom;
static pthread_mutex_t g_ro_mu = PTHREAD_MUTEX_INITIALIZER;
static uint32_t ro_note_locked(vkimg_t *im, uint32_t flags) {
    if (!im || g_ro_oom) return 0;
    if (!g_ro && !(g_ro=calloc(1,sizeof *g_ro))) { g_ro_oom=1; return 0; }
    const vkfmt_t *fmt=vk_fmt(im->nvn_format);
    if (!im->pool || im->target!=NVN_TEXTURE_TARGET_2D || im->d!=1 || im->layers!=1 ||
        im->levels!=1 || im->samples!=1 || !fmt || fmt->compressed || fmt->is_int ||
        im->bc_of || im->bc_draw || im->family_closed) flags|=NX_RO_INTRINSIC;
    if (im->packaged || im->lin_mem || (im->flags & (NVN_TEXTURE_FLAGS_LINEAR | NVN_TEXTURE_FLAGS_SPARSE | NVN_TEXTURE_FLAGS_LINEAR_RENDER_TARGET))) flags|=NX_RO_HOST;
    if (im->flags & NVN_TEXTURE_FLAGS_IMAGE) flags|=NX_RO_STORAGE;
    if (im->novl) flags|=NX_RO_ALIAS;
    return nx_ro_note(g_ro,(uintptr_t)im,flags);
}
void vk_rs_note(vkimg_t *im, uint32_t flags) {
    if (!im) return;
    pthread_mutex_lock(&g_ro_mu); ro_note_locked(im,flags); pthread_mutex_unlock(&g_ro_mu);
}
void vk_rs_note_group(vkimg_t *const *images, unsigned count) {
    pthread_mutex_lock(&g_ro_mu);
    uint32_t root=0;
    for (unsigned i=0;i<count;i++) {
        uint32_t id=ro_note_locked(images[i],NX_RO_ATTACH);
        if (!id) continue;
        if (!root) root=id; else nx_ro_merge(g_ro,root,id);
    }
    pthread_mutex_unlock(&g_ro_mu);
}
// Called only at nonnative exchange, with g_img_mu held. Never promote an
// observed candidate on its own: this is an additional rejection gate.
static int ro_live_group_admit(vkimg_t *const owners[VK_RS_GROUP_SIZE]) {
    if (!vk_rs_tracking()) return 0;
    uintptr_t keys[VK_RS_GROUP_SIZE];
    for (unsigned i=0;i<VK_RS_GROUP_SIZE;i++) keys[i]=(uintptr_t)owners[i];
    pthread_mutex_lock(&g_ro_mu);
    int ok=!g_ro_oom && nx_ro_group_admit(g_ro,keys,VK_RS_GROUP_SIZE);
    pthread_mutex_unlock(&g_ro_mu);
    return ok;
}
static void ro_drop(vkimg_t *im) {
    pthread_mutex_lock(&g_ro_mu);
    if (g_ro) nx_ro_drop(g_ro,(uintptr_t)im);
    pthread_mutex_unlock(&g_ro_mu);
}
static void ro_report(void) {
    unsigned live=0, attachments=0, candidates=0, reasons[11]={0}, exhausted=0;
    pthread_mutex_lock(&g_ro_mu);
    exhausted=g_ro_oom || (g_ro && g_ro->exhausted);
    if (g_ro) for (uint32_t i=0;i<g_ro->count;i++) if (g_ro->nodes[i].live) {
        live++; uint32_t flags=nx_ro_flags(g_ro,i+1);
        if (!(g_ro->nodes[i].flags & NX_RO_ATTACH)) continue;
        attachments++;
        if (!exhausted && !(flags & NX_RO_BLOCKERS)) candidates++;
        for (unsigned j=2;j<11;j++) if (flags & (1u<<j)) reasons[j]++;
    }
    pthread_mutex_unlock(&g_ro_mu);
    VKLOG("[resolution-ownership] live=%u attachment_owners=%u observed_candidates=%u exhausted=%u blockers alias=%u raw=%u host=%u storage=%u view=%u unknown=%u compute=%u present=%u intrinsic=%u (not authorization)",
          live,attachments,candidates,exhausted,reasons[2],reasons[3],reasons[4],reasons[5],reasons[6],reasons[7],reasons[8],reasons[9],reasons[10]);
}
static void vk_tex_view_drop_all(vkimg_t *im);
// Images over the same memory with different descriptions (the storage key differs) are separate Vulkan images: on the Switch a texture sees what another
// one over its memory rendered, here it does not. They are linked both ways at creation (up to 8 each), and a sampled image older than an overlapping
// image's last rendering is logged ([alias] stale read), to find what a game reinterprets
static uint64_t g_wseq;
void vk_tex_mark_written(vkimg_t *im) {
    im->gpu_w = 1;
    im->wseq = ++g_wseq;
}
static void alias_link(vkimg_t *im) {
    for (uint32_t i = 0; i < g_by_key.cap; i++) {
        vkimg_t *o = g_by_key.vals[i]; if (!o || o == im || o->pool != im->pool || o->off >= im->off + im->size || im->off >= o->off + o->size) continue;
        // Observe ALL overlaps, even when the legacy eight-neighbor list is full.
        if (vk_rs_tracking()) { vk_rs_note(im,NX_RO_ALIAS); vk_rs_note(o,NX_RO_ALIAS); }
        if (im->novl < 8 && o->novl < 8) { im->ovl[im->novl++] = o; o->ovl[o->novl++] = im; }
    }
}
static void alias_unlink(vkimg_t *im) {
    for (int i = 0; i < im->novl; i++) { vkimg_t *o = im->ovl[i]; for (int j = 0; j < o->novl; j++) if (o->ovl[j] == im) { o->ovl[j] = o->ovl[--o->novl]; break; } }
    im->novl = 0;
}
static void alias_check(vkimg_t *im) {   // im is about to be sampled
    for (int i = 0; i < im->novl; i++) {
        vkimg_t *o = im->ovl[i]; if (o->wseq <= im->wseq || o->wseq <= im->rseen) continue;
        im->rseen = o->wseq;
        static int n; if (n++ < 200) VKLOG("[alias] possible stale read: pool %#llx, %ux%u fmt %#x levels %u at %+lld..%+lld sampled (CPU uploads %d, attachment sequence %llu), overlaps %ux%u fmt %#x levels %u at %+lld..%+lld (attachment sequence %llu)",
            (unsigned long long)im->pool, im->w, im->h, im->nvn_format, im->levels,
            (long long)im->off, (long long)(im->off + im->size), im->packaged, (unsigned long long)im->wseq,
            o->w, o->h, o->nvn_format, o->levels, (long long)o->off, (long long)(o->off + o->size), (unsigned long long)o->wseq);
    }
}
int vk_tex_trace_alias(vkimg_t *im, uint64_t shader, unsigned stage, unsigned binding,
                        uint64_t submission, int pass, uint64_t capture, vkimg_t *producer) {
    // Diagnostics only, with one report per sampled storage/shader/binding.
    enum { LIMIT = 80 };
    static struct { uint64_t pool, shader; int64_t offset; unsigned stage, binding; } seen[LIMIT];
    static unsigned n;
    static uint64_t last_capture;
    if (capture != last_capture) { n = 0; last_capture = capture; }
    if (!im->novl || n == LIMIT) return 0;
    int discovered = 0;
    pthread_mutex_lock(&g_img_mu);
    const vkimg_t *writer = NULL;
    for (int i = 0; i < im->novl; i++) {
        const vkimg_t *other = im->ovl[i];
        if (other->wseq > (writer ? writer->wseq : im->wseq)) writer = other;
    }
    if (writer) {
        unsigned i = 0;
        for (; i < n; i++) if (seen[i].pool == im->pool && seen[i].offset == im->off &&
                               seen[i].shader == shader && seen[i].stage == stage && seen[i].binding == binding) break;
        if (i == n) {
            if (producer) *producer = *writer;
            discovered = 1;
            seen[n++] = (typeof(seen[0])){im->pool, shader, im->off, stage, binding};
            VKLOG("[alias-consumer] submission %llu pass %d shader %016llx stage %u binding %u pool %#llx: sampled fmt %#x +%lld size %lld uploads %d, overlaps GPU writer fmt %#x +%lld size %lld sequence %llu",
                  (unsigned long long)submission, pass, (unsigned long long)shader, stage, binding,
                  (unsigned long long)im->pool, im->nvn_format, (long long)im->off, (long long)im->size,
                  im->packaged, writer->nvn_format, (long long)writer->off, (long long)writer->size,
                  (unsigned long long)writer->wseq);
        }
    }
    pthread_mutex_unlock(&g_img_mu);
    return discovered;
}
extern uint64_t g_tex_frame;   // presents so far, for the packaged textures' last use (vk_tex_sweep)
// Images are kept per storage key until their pool goes, so a texture the game streams into memory another texture had before gets an image of its own and the old one
// stayed: Xenoblade Chronicles 3 streams its field textures through pools that live for good, the device's GPU memory grew by about 470 MB a minute while walking (6.3 GB
// after 20 minutes) and Android's low-memory killer ended the game. A new image takes over: the images of the same pool over its memory that were not looked up in the
// last EVICT_KEEP presents go (their memory now holds another texture; one drawn again is made and filled again). Called with g_img_mu held
enum { EVICT_KEEP = 60 };
static uint64_t g_img_n, g_img_bytes, g_img_evicted;   // live images and their bytes, images evicted so far (vk_tex_sweep's line)
static void img_drop(vkimg_t *im) { if (im->rs_backing) rs_active_drop(im); if (vk_rs_tracking()) { ro_drop(im); if (im->bc_draw) ro_drop(im->bc_draw); } if (im->bc_draw) { vkimg_t *d = im->bc_draw; vk_lrz_forget(d); vk_tex_view_drop_all(d); if (d->sub) vk_defer_free_image_sub(d->img, d->sub); else vk_defer_free_image(d->img, d->mem); free(d); }
    vk_lrz_forget(im); if (im->lin_mem) nvn_dirty_release(im->lin_mem, im->lin_len); if (im->packaged && !im->pkg_released && im->pkg_mem) nvn_dirty_release(im->pkg_mem, im->pkg_len); g_img_n--; g_img_bytes -= im->size > 0 ? (uint64_t)im->size : 0; alias_unlink(im); vk_tex_view_drop_all(im); if (im->rs_backing) vk_rs_variant_retire(im->rs_backing); else if (im->sub) vk_defer_free_image_sub(im->img, im->sub); else vk_defer_free_image(im->img, im->mem); free(im); }
// g_img_mu held. Remove only discarded content; never blit it back to
// logical dimensions. Retire the physical allocation by the normal fence.
static void rs_evict_dormant_owner(vkimg_t *im) {
    for (uint32_t j=0;j<g_by_key.cap;j++) if (g_by_key.vals[j]==im) {
        akey k; memcpy(&k,g_by_key.keys+(size_t)j*g_by_key.klen,sizeof k);
        nvn_hm_del(&g_by_key,&k); img_drop(im); return;
    }
    die("[vk] resolution: dormant owner absent from cache");
}
static void rs_evict_dormant_overlaps(uint64_t addr,uint64_t size) {
    for (unsigned i=0;i<g_rs_dormant.n;) {
        vkimg_t *im=(vkimg_t *)g_rs_dormant.owner[i];
        if (!addr || !size || !im->rs_guest_addr || im->size<=0 ||
            nx_rs_range_overlap(addr,size,im->rs_guest_addr,(uint64_t)im->size)) {
            rs_evict_dormant_owner(im);
        } else i++;
    }
}
void vk_rs_release_dormant(void) {
    pthread_mutex_lock(&g_img_mu);
    rs_evict_dormant_overlaps(0,0);
    pthread_mutex_unlock(&g_img_mu);
}
// Caller has witnessed complete discards for every remaining active owner
// within the certified replay lifetime. No allocation, copy or global wait.
int vk_rs_suspend_discarded_group(void) {
    if (!vk_sub_open() || !vk_rs_pass_idle()) return 0;
    pthread_mutex_lock(&g_img_mu);
    nx_rs_active staged=g_rs_dormant;
    for (unsigned i=0;g_rs_live && i<g_rs_live->n;i++) {
        vkimg_t *im=(vkimg_t *)g_rs_live->owner[i];
        if (!rs_is_scaled(im) || !nx_rs_active_update(&staged,(uintptr_t)im,1)) {
            pthread_mutex_unlock(&g_img_mu); return 0;
        }
    }
    g_rs_dormant=staged;
    free(g_rs_live); g_rs_live=NULL; rs_active_publish();
    pthread_mutex_unlock(&g_img_mu);
    return 1;
}
static void evict_overlaps(uint64_t pool, int64_t off, int64_t size) {
    akey ks[64]; int n = 0;
    for (uint32_t i = 0; i < g_by_key.cap && n < 64; i++) {
        vkimg_t *o = g_by_key.vals[i]; if (!o || o->pool != pool || o->off >= off + size || off >= o->off + o->size || g_tex_frame - o->used < EVICT_KEEP) continue;
        memcpy(&ks[n++], g_by_key.keys + (size_t)i * g_by_key.klen, sizeof(akey));
    }
    for (int j = 0; j < n; j++) { vkimg_t *o = nvn_hm_get(&g_by_key, &ks[j]); if (!o) continue; nvn_hm_del(&g_by_key, &ks[j]); img_drop(o); g_img_evicted++; }
}
static vkimg_t *vk_tex_lookup(const tex_t *t, int sampled);
static vkimg_t *vk_tex_get(const tex_t *t, int sampled) { extern int g_prof_on; extern uint64_t g_prof_ns[8]; struct timespec a, b; if (g_prof_on) clock_gettime(CLOCK_MONOTONIC, &a);
    vkimg_t *im = vk_tex_lookup(t, sampled); if (g_prof_on) { clock_gettime(CLOCK_MONOTONIC, &b); g_prof_ns[1] += (uint64_t)((b.tv_sec - a.tv_sec) * 1000000000ll + (b.tv_nsec - a.tv_nsec)); } return im; }
vkimg_t *vk_tex(const tex_t *t) {
    vkimg_t *im=vk_tex_get(t,0);
    if (vk_rs_tracking()) vk_rs_note(im,NX_RO_RAW);
    if (rs_is_scaled(im) && !vk_rs_require_native(im)) die("[vk] resolution: native-only texture access could not restore backing");
    return im;
}
// A certified display reconstruction may consume physical backing
// directly. No raw caller may use this helper without preflight.
vkimg_t *vk_rs_copy_source(const tex_t *t) {
    nx_rs_draw_extent extent;
    if (vk_rs_preflight_view(t,NULL,1,&extent)!=1) return NULL;
    vkimg_t *im=vk_tex_get(t,0);
    return rs_is_scaled(im)?im:NULL;
}
vkimg_t *vk_tex_for_present(const tex_t *t) {
    // Presentation can resolve a new wrapper outside a replay submission.
    // Restore before lookup so the overlap guard cannot see an unprepared
    // alias and so an asynchronous present never receives scaled storage.
    if (vk_rs_cache_has_scaled() && t) {
        nx_rs_draw_extent extent;
        if (vk_rs_preflight_view(t,NULL,0,&extent)==2) {
            void mtl_sub_begin(void); uint64_t mtl_sub_commit(void); int mtl_wait(uint64_t,uint64_t);
            int own=!vk_sub_open();
            if (own) mtl_sub_begin();
            int ok=vk_rs_cache_restore_all();
            if (own) mtl_wait(mtl_sub_commit(),~0ull);
            if (!ok) die("[vk] resolution: presentation overlap could not restore native backing");
        }
    }
    vkimg_t *im=vk_tex_get(t,0);
    if (im && vk_rs_provenance()) im->rs_presented=1;
    if (vk_rs_tracking()) vk_rs_note(im,NX_RO_PRESENT);
    // Presentation currently expects logical-sized images and may queue a
    // pointer for asynchronous use. Restore before handing it to that queue.
    if (rs_is_scaled(im)) {
        void mtl_sub_begin(void); uint64_t mtl_sub_commit(void); int mtl_wait(uint64_t,uint64_t);
        int own=!vk_sub_open();
        if (own) mtl_sub_begin();
        int ok=vk_rs_require_native(im);
        if (own) mtl_wait(mtl_sub_commit(),~0ull);
        if (!ok) die("[vk] resolution: native presentation backing could not be restored");
    }
    return im;
}
// g_img_mu held. Mirror the native read-only IMAGE-cleared wrapper fallback,
// but only for a written, active physical owner; never substitute an
// independent native texture or an integer reinterpretation.
static vkimg_t *rs_sampled_peer(const akey *k,const tex_t *t) {
    if ((k->d.flags&NVN_TEXTURE_FLAGS_IMAGE) || !nx_rs_storage_spv_format(t->d.format) ||
        t->has_layout || t->has_pkg_data || t->set_swz || !t->pool ||
        !(t->pool->flags&NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS)) return NULL;
    akey producer=*k;producer.d.flags|=NVN_TEXTURE_FLAGS_IMAGE;
    vkimg_t *peer=rs_exact_owner(&producer);
    return rs_is_scaled(peer) && peer->gpu_w && !peer->packaged && !peer->family_closed &&
        peer->size==(int64_t)t->L.size && peer->nvn_format==t->d.format &&
        nx_rs_active_contains(g_rs_live,(uintptr_t)peer)?peer:NULL;
}
// No image creation, upload, view creation, family mutation or guest writes.
static int rs_preflight_view_access(const tex_t *t,const view_t *v,int supported,int sampled,nx_rs_draw_extent *extent) {
    if (!t || !extent) return 2;
    *extent=(nx_rs_draw_extent){t->d.width,t->d.height,t->d.width,t->d.height,1};
    uint64_t guest=rs_guest_address(t);
    if (!guest) return 2;
    if (t->off<0 || !t->L.size) return 2;
    const vkfmt_t *f=vk_fmt(t->d.format);
    if (!f) return 2;
    akey k; memset(&k,0,sizeof k); k.pool=(uint64_t)(uintptr_t)t->pool; k.off=t->off; k.d=t->d;
    k.d.flags &= NVN_TEXTURE_FLAGS_LINEAR | NVN_TEXTURE_FLAGS_SPARSE | NVN_TEXTURE_FLAGS_LINEAR_RENDER_TARGET | NVN_TEXTURE_FLAGS_MINIMAL_LAYOUT | (f->depth || f->stencil ? 0 : NVN_TEXTURE_FLAGS_IMAGE);
    if (supported && !nx_rs_view_equivalent(t,v)) supported=0;
    if (t->has_layout || t->has_pkg_data || ((t->d.flags&NVN_TEXTURE_FLAGS_IMAGE) && !f->depth && !f->stencil && !nx_rs_storage_spv_format(t->d.format)) ||
        (t->d.flags & (NVN_TEXTURE_FLAGS_LINEAR | NVN_TEXTURE_FLAGS_SPARSE | NVN_TEXTURE_FLAGS_LINEAR_RENDER_TARGET))) supported=0;
    int result=0;
    pthread_mutex_lock(&g_img_mu);
    vkimg_t *exact_owner=rs_exact_owner(&k);
    if (!exact_owner && sampled && supported && g_rs_live) exact_owner=rs_sampled_peer(&k,t);
    for (unsigned i=0;g_rs_live && i<g_rs_live->n;i++) {
        vkimg_t *im=(vkimg_t *)g_rs_live->owner[i];
        if (!rs_is_scaled(im)) { result=2; break; }
        if (!im->rs_guest_addr || im->off<0 || im->size<=0) { result=2; break; }
        int exact=im==exact_owner && im->pool==k.pool && im->rs_guest_addr==guest &&
            im->off==k.off && (uint64_t)im->size==(uint64_t)t->L.size &&
            im->w==t->d.width && im->h==t->d.height && im->nvn_format==t->d.format &&
            !im->novl && !im->packaged && !im->lin_mem && !im->family_closed &&
            im->rs_backing->image==im->img;
        int access=nx_rs_preflight_overlap(guest,t->L.size,im->rs_guest_addr,(uint64_t)im->size,exact,supported);
        if (access==2 || (access==1 && result==1)) { result=2; break; }
        if (access==1) {
            result=1; extent->pw=im->rs_backing->physical_w; extent->ph=im->rs_backing->physical_h;
        }
    }
    pthread_mutex_unlock(&g_img_mu);
    // CPU marks are checked outside the image-table lock.
    int nvn_ft_written(const tex_t *);
    if (result==1 && (packaged_tex(t) || nvn_ft_written(t))) return 2;
    return result;
}
int vk_rs_preflight_view(const tex_t *t,const view_t *v,int supported,nx_rs_draw_extent *extent) {
    return rs_preflight_view_access(t,v,supported,0,extent);
}
int vk_rs_preflight_sampled_view(const tex_t *t,const view_t *v,int supported,nx_rs_draw_extent *extent) {
    return rs_preflight_view_access(t,v,supported,1,extent);
}
// Restricted to exact whole-image color clears, with no view override or
// guest-visible upload. Never authorize initial downscaling from a clear.
vkimg_t *vk_rs_native_for_full_clear(const p_clear_tex *c) {
    if (!c || !c->tex || c->has_view || c->mask!=NVN_CLEAR_COLOR_MASK_RGBA ||
        !vk_sub_open() || !vk_rs_pass_idle()) return NULL;
    const tex_t *t=c->tex;
    if (!t->serial || t->magic!=MAGIC('T','E','X',0) ||
        t->d.target!=NVN_TEXTURE_TARGET_2D || t->d.levels!=1 || t->d.depth!=1 || t->d.samples>1 ||
        c->r.xoffset || c->r.yoffset || c->r.zoffset || c->r.depth!=1 ||
        c->r.width<=0 || c->r.height<=0 ||
        (uint32_t)c->r.width!=(uint32_t)t->d.width || (uint32_t)c->r.height!=(uint32_t)t->d.height) return NULL;
    const vkfmt_t *fmt=vk_fmt(t->d.format);
    if (!fmt || fmt->depth || fmt->stencil || fmt->compressed || fmt->is_int) return NULL;
    nx_rs_draw_extent extent;
    if (vk_rs_preflight_view(t,NULL,1,&extent)!=1) return NULL;
    uint64_t guest=rs_guest_address(t);
    akey k={0}; k.pool=(uint64_t)(uintptr_t)t->pool; k.off=t->off; k.d=t->d;
    k.d.flags &= NVN_TEXTURE_FLAGS_LINEAR | NVN_TEXTURE_FLAGS_SPARSE | NVN_TEXTURE_FLAGS_LINEAR_RENDER_TARGET | NVN_TEXTURE_FLAGS_MINIMAL_LAYOUT | NVN_TEXTURE_FLAGS_IMAGE;
    pthread_mutex_lock(&g_img_mu);
    vkimg_t *im=rs_exact_owner(&k);
    // Recheck cache identity after reacquiring the lock. No lookup or owner
    // creation is allowed between publication and the caller's clear.
    if (!guest || !rs_is_scaled(im) || im->rs_guest_addr!=guest ||
        im->off!=t->off || im->size<=0 || (uint64_t)im->size!=(uint64_t)t->L.size ||
        im->pool!=k.pool || im->w!=(uint32_t)t->d.width || im->h!=(uint32_t)t->d.height ||
        im->nvn_format!=t->d.format || im->aspect!=VK_IMAGE_ASPECT_COLOR_BIT ||
        im->novl || im->packaged || im->lin_mem || im->family_closed || im->full ||
        im->rs_backing->image!=im->img || !rs_discard_native_locked(im,1)) im=NULL;
    if (im) { extern uint64_t g_tex_frame; im->used=g_tex_frame; }
    pthread_mutex_unlock(&g_img_mu);
    if (im && vk_rs_tracking()) vk_rs_note(im,NX_RO_RAW);
    return im;
}
int vk_rs_discard_target(const tex_t *t) {
    if (!t || !t->serial || t->magic!=MAGIC('T','E','X',0) ||
        !vk_sub_open() || !vk_rs_pass_idle() || !vk_rs_cache_has_scaled() ||
        t->d.target!=NVN_TEXTURE_TARGET_2D || t->d.levels!=1 || t->d.depth!=1 || t->d.samples>1) return 0;
    const vkfmt_t *fmt=vk_fmt(t->d.format);
    if (!fmt || fmt->compressed || fmt->is_int) return 0;
    nx_rs_draw_extent extent;
    if (vk_rs_preflight_view(t,NULL,1,&extent)!=1) return 0;
    uint64_t guest=rs_guest_address(t);
    akey k={0}; k.pool=(uint64_t)(uintptr_t)t->pool; k.off=t->off; k.d=t->d;
    k.d.flags &= NVN_TEXTURE_FLAGS_LINEAR | NVN_TEXTURE_FLAGS_SPARSE | NVN_TEXTURE_FLAGS_LINEAR_RENDER_TARGET | NVN_TEXTURE_FLAGS_MINIMAL_LAYOUT | (fmt->depth || fmt->stencil ? 0 : NVN_TEXTURE_FLAGS_IMAGE);
    pthread_mutex_lock(&g_img_mu);
    vkimg_t *im=rs_exact_owner(&k);
    int ok=guest && rs_is_scaled(im) && im->rs_guest_addr==guest &&
        im->off==t->off && im->size>0 && (uint64_t)im->size==(uint64_t)t->L.size &&
        im->pool==k.pool && im->w==(uint32_t)t->d.width && im->h==(uint32_t)t->d.height &&
        im->nvn_format==t->d.format && !im->novl && !im->packaged && !im->lin_mem &&
        !im->family_closed && !im->full && im->rs_backing->image==im->img &&
        rs_discard_native_locked(im,0);
    pthread_mutex_unlock(&g_img_mu);
    return ok;
}
// Stage a fresh owner using an unbound native description only for
// requirements. No native VkDeviceMemory is allocated or retained.
static vkimg_t *rs_fresh_owner(const tex_t *t,const vkfmt_t *fmt,uint32_t w,uint32_t h,uint64_t guest) {
    vkimg_t *im=img_new_description(t,fmt,1);
    if (!im) return NULL;
    im->pool=(uint64_t)(uintptr_t)t->pool; im->off=t->off;
    im->size=(int64_t)t->L.size; im->flags=t->d.flags; im->rs_guest_addr=guest;
    vk_rs_variant *v=vk_rs_variant_create(im,w,h);
    vkDestroyImage(V.dev,im->img,NULL);
    if (!v || !v->native_bytes || v->bytes>=v->native_bytes) {
        vk_rs_variant_destroy(v); free(im); return NULL;
    }
    im->img=v->image; im->mem=v->memory; im->layout=v->layout; im->rs_backing=v;
    return im;
}
// Prepare hash capacity in a private map, preserving the original on
// allocation failure. nvn_hm_put cannot grow while the load bound holds.
static int rs_stage_cache_capacity(const nvn_hmap *old,unsigned extra,nvn_hmap *next) {
    uint64_t count=(uint64_t)old->n+extra;
    if (count*4<=(uint64_t)old->cap*3) return 1;
    uint32_t cap=old->cap?old->cap:64;
    while (count*4>(uint64_t)cap*3) {
        if (cap>UINT32_MAX/2) return 0;
        cap*=2;
    }
    if (!old->klen || (size_t)cap>SIZE_MAX/old->klen) return 0;
    next->keys=malloc((size_t)cap*old->klen);
    next->vals=calloc(cap,sizeof *next->vals);
    if (!next->keys || !next->vals) {
        free(next->keys); free(next->vals); *next=(nvn_hmap){0}; return 0;
    }
    next->cap=cap; next->klen=old->klen;
    for (uint32_t i=0;i<old->cap;i++) if (old->vals[i])
        nvn_hm_put(next,old->keys+(size_t)i*old->klen,old->vals[i]);
    return 1;
}
// Native-to-physical replacement is permitted only at a certified complete
// overwrite. Native slabs are excluded: releasing one slot does not actually
// release device memory. Presentation owners can outlive the replay interval.
static int rs_native_overwrite_candidate(const vkimg_t *im) {
    return im && im->img && im->mem && !im->rs_backing && !im->sub &&
        !im->full && !im->rs_presented && !im->novl && !im->packaged &&
        !im->lin_mem && !im->bc_draw && !im->bc_of &&
        (im->layout==VK_IMAGE_LAYOUT_GENERAL || im->layout==VK_IMAGE_LAYOUT_UNDEFINED);
}
// The real cache allocation transaction. Caller MUST prove the full overwrite
// and all consumers through the end of the lifetime before using this entry.
// A clean observed graph or CPU_NO_ACCESS alone is not a lifetime certificate.
int vk_rs_allocate_fresh_group(const tex_t *const *targets,unsigned count,uint32_t w,uint32_t h) {
    if (!targets || !count || count>NX_RS_SCOPE_OWNER_MAX || !vk_sub_open() || !vk_rs_pass_idle() || !vk_rs_provenance() || !w || !h) return 0;
    akey keys[NX_RS_SCOPE_OWNER_MAX]={0};
    vkimg_t *fresh[NX_RS_SCOPE_OWNER_MAX]={0},*reuse[NX_RS_SCOPE_OWNER_MAX]={0};
    vk_rs_variant *replacement[NX_RS_SCOPE_OWNER_MAX]={0};
    uint64_t addr[NX_RS_SCOPE_OWNER_MAX]={0};
    const vkfmt_t *formats[NX_RS_SCOPE_OWNER_MAX]={0};
    nx_rs_draw_extent first={0};
    unsigned n=0;
    int nvn_ft_written(const tex_t *);
    for (unsigned i=0;i<count;i++) {
        const tex_t *t=targets[i]; if (!t) continue;
        if (!t->serial || t->magic!=MAGIC('T','E','X',0) || !t->pool ||
            !(t->pool->flags & NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS) ||
            t->has_layout || t->has_pkg_data || t->d.target!=NVN_TEXTURE_TARGET_2D ||
            t->d.levels!=1 || t->d.depth!=1 || t->d.samples>1 ||
            (t->d.flags & (NVN_TEXTURE_FLAGS_DISPLAY | NVN_TEXTURE_FLAGS_LINEAR | NVN_TEXTURE_FLAGS_SPARSE | NVN_TEXTURE_FLAGS_LINEAR_RENDER_TARGET)) ||
            t->L.size>INT64_MAX || !(addr[i]=rs_guest_address(t)) ||
            packaged_tex(t) || nvn_ft_written(t)) return 0;
        const vkfmt_t *fmt=vk_fmt(t->d.format);
        // Only canonical UNORM family members are admitted. Any later
        // sRGB/X reinterpretation still requires native preflight.
        unsigned family=texture_family_bit(t->d.format);
        if (!fmt || fmt->compressed || fmt->dec || fmt->is_int || (family && family!=1) ||
            ((t->d.flags&NVN_TEXTURE_FLAGS_IMAGE) && !fmt->depth && !fmt->stencil && !nx_rs_storage_spv_format(t->d.format))) return 0;
        formats[i]=fmt;
        nx_rs_draw_extent ext={t->d.width,t->d.height,w,h,1};
        if (w>=ext.lw || h>=ext.lh) return 0;
        if (!n) {
            if (!nx_rs_draw_group(&ext,1,&first)) return 0;
        } else if (first.lw!=ext.lw || first.lh!=ext.lh) return 0;
        keys[i].pool=(uint64_t)(uintptr_t)t->pool; keys[i].off=t->off; keys[i].d=t->d; keys[i].family=family!=0;
        keys[i].d.flags &= NVN_TEXTURE_FLAGS_LINEAR | NVN_TEXTURE_FLAGS_SPARSE | NVN_TEXTURE_FLAGS_LINEAR_RENDER_TARGET | NVN_TEXTURE_FLAGS_MINIMAL_LAYOUT | (fmt->depth || fmt->stencil ? 0 : NVN_TEXTURE_FLAGS_IMAGE);
        for (unsigned j=0;j<i;j++) if (targets[j] &&
            nx_rs_range_overlap(addr[i],t->L.size,addr[j],targets[j]->L.size)) return 0;
        n++;
    }
    if (!n) return 0;
    pthread_mutex_lock(&g_img_mu);
    nx_rs_active staged={0},*allocated=NULL, dormant=g_rs_dormant;
    nvn_hmap next_map={0};
    int ok=0;
    if (g_rs_live) staged=*g_rs_live;
    if (staged.n>NX_RS_ACTIVE_MAX || n>NX_RS_ACTIVE_MAX-staged.n) goto done;
    uint64_t needed=atomic_load(&g_rs_restore_budget.bytes);
    for (unsigned i=0;i<staged.n;i++) {
        vkimg_t *im=(vkimg_t *)staged.owner[i];
        if (!rs_is_scaled(im) || !im->rs_backing->native_bytes ||
            needed>NX_RS_RESTORE_BUDGET_BYTES ||
            im->rs_backing->native_bytes>NX_RS_RESTORE_BUDGET_BYTES-needed) goto done;
        needed+=im->rs_backing->native_bytes;
    }
    unsigned added=0;
    for (unsigned i=0;i<count;i++) if (targets[i]) {
        vkimg_t *existing=nvn_hm_get(&g_by_key,&keys[i]);
        if (existing) {
            if (existing->rs_guest_addr!=addr[i] || existing->size<=0 ||
                (uint64_t)existing->size!=targets[i]->L.size ||
                existing->novl || existing->packaged || existing->lin_mem || existing->family_closed ||
                existing->rs_presented || (keys[i].family && existing->family_formats!=1)) goto done;
            if (rs_is_scaled(existing)) {
                if (!nx_rs_active_contains(&dormant,(uintptr_t)existing) ||
                    existing->rs_backing->native_only ||
                    existing->rs_backing->layout!=existing->layout) goto done;
            } else if (!rs_native_overwrite_candidate(existing)) goto done;
        }
        for (uint32_t j=0;j<g_by_key.cap;j++) {
            vkimg_t *o=g_by_key.vals[j]; if (!o || o==existing) continue;
            if (!o->rs_guest_addr || o->size<=0 ||
                nx_rs_range_overlap(addr[i],targets[i]->L.size,o->rs_guest_addr,(uint64_t)o->size)) goto done;
        }
        if (existing) {
            reuse[i]=existing;
            if (rs_is_scaled(existing) &&
                !nx_rs_active_update(&dormant,(uintptr_t)existing,0)) goto done;
            // Presentation may still read the last contents after replay
            // discards them. Reuse only when the cache is the sole owner.
            if (!rs_is_scaled(existing) || existing->rs_backing->physical_w!=w ||
                existing->rs_backing->physical_h!=h || atomic_load(&existing->rs_backing->refs)!=1) {
                replacement[i]=vk_rs_variant_create(existing,w,h);
                if (!replacement[i] || !replacement[i]->native_bytes ||
                    replacement[i]->bytes>=replacement[i]->native_bytes) goto done;
            }
        } else {
            fresh[i]=rs_fresh_owner(targets[i],formats[i],w,h,addr[i]);
            if (!fresh[i]) goto done;
            if (keys[i].family) fresh[i]->family_formats=1;
            added++;
        }
        vkimg_t *im=existing?existing:fresh[i];
        const vk_rs_variant *backing=replacement[i]?replacement[i]:im->rs_backing;
        if (needed>NX_RS_RESTORE_BUDGET_BYTES ||
            backing->native_bytes>NX_RS_RESTORE_BUDGET_BYTES-needed ||
            !nx_rs_active_update(&staged,(uintptr_t)im,1)) goto done;
        needed+=backing->native_bytes;
    }
    // Reserve room for this complete group to become dormant at discard.
    if (dormant.n>NX_RS_ACTIVE_MAX || staged.n>NX_RS_ACTIVE_MAX-dormant.n ||
        !rs_stage_cache_capacity(&g_by_key,added,&next_map)) goto done;
    if (!g_rs_live && !(allocated=malloc(sizeof *allocated))) goto done;
    if (next_map.cap) {
        free(g_by_key.keys); free(g_by_key.vals);
        g_by_key=next_map; next_map=(nvn_hmap){0};
    }
    // All physical allocations and index capacity validated. Publication
    // retires old native backings and views by fence, without a reverse copy.
    for (unsigned i=0;i<count;i++) if (fresh[i]) {
        vkimg_t *im=fresh[i];
        im->used=g_tex_frame; g_img_n++; g_img_bytes+=(uint64_t)im->size;
        nvn_hm_put(&g_by_key,&keys[i],im); fresh[i]=NULL;
    }
    for (unsigned i=0;i<count;i++) if (reuse[i]) {
        if (replacement[i]) {
            rs_publish_backing(reuse[i],replacement[i]); replacement[i]=NULL;
        }
        reuse[i]->used=g_tex_frame;
    }
    g_rs_dormant=dormant;
    if (allocated) g_rs_live=allocated;
    *g_rs_live=staged; rs_active_publish();
    ok=1;
done:
    free(next_map.keys); free(next_map.vals);
    for (unsigned i=0;i<count;i++) vk_rs_variant_destroy(replacement[i]);
    for (unsigned i=0;i<count;i++) if (fresh[i]) {
        vk_rs_variant_destroy(fresh[i]->rs_backing); free(fresh[i]);
    }
    pthread_mutex_unlock(&g_img_mu);
    return ok;
}
static vkimg_t *vk_tex_lookup(const tex_t *t, int sampled) {
    if (!t || !t->serial || t->magic != MAGIC('T', 'E', 'X', 0)) return NULL;
    const vkfmt_t *f = vk_fmt(t->d.format);
    if (!f) { VKRED_ONCE(14, "texture format %#x has no Vulkan counterpart here (first case %ux%u target %d)", t->d.format, t->d.width, t->d.height, t->d.target); return NULL; }
    if (t->d.target == NVN_TEXTURE_TARGET_BUFFER) return NULL;   // buffer textures: texel buffer views of the pool (vk_tex_view)
    akey k; memset(&k, 0, sizeof k); k.pool = (uint64_t)(uintptr_t)t->pool; k.off = t->off; k.d = t->d;
    // flags that do not change the storage's layout are not part of the key: on the Switch two textures over the same memory that differ only in them see the
    // same texels. Xenoblade Chronicles 3 renders its shadow maps and depth-stencil buffer through textures with COMPRESSIBLE and samples them through others with
    // IMAGE added; as separate images those were never written, the shadows were total and the stencil's material tags 0, and its lit scene black. IMAGE stays
    // in the key for colour formats (it gives the image storage usage), not for depth and stencil, which cannot be storage images
    k.d.flags &= NVN_TEXTURE_FLAGS_LINEAR | NVN_TEXTURE_FLAGS_SPARSE | NVN_TEXTURE_FLAGS_LINEAR_RENDER_TARGET | NVN_TEXTURE_FLAGS_MINIMAL_LAYOUT | (f->depth || f->stencil ? 0 : NVN_TEXTURE_FLAGS_IMAGE);
    int keyed = t->pool != NULL;
    // CPU-write marks affect uploads below, never storage identity. Packaged
    // initialization metadata may close a family but cannot split existing peers.
    pthread_mutex_lock(&g_img_mu);
    // Outside a certified scope, discarded physical content is never
    // returned as native. Recreate on demand, with no lossy reverse copy.
    if (g_rs_dormant.n) rs_evict_dormant_overlaps(rs_guest_address(t),t->L.size);
    // Last-line guard for callers that bypass draw preflight. In particular,
    // never create/evict an overlapping owner or mutate a format family
    // while its storage is represented by a scaled backing.
    vkimg_t *physical_peer=NULL;
    if (g_rs_live) {
        uint64_t guest=rs_guest_address(t);
        int restore=!guest;
        vkimg_t *exact=rs_exact_owner(&k);
        if (!exact && sampled) exact=physical_peer=rs_sampled_peer(&k,t);
        for (unsigned i=0;!restore && i<g_rs_live->n;i++) {
            vkimg_t *o=(vkimg_t *)g_rs_live->owner[i];
            if (!rs_is_scaled(o)) { restore=1; break; }
            if (!o->rs_guest_addr || o->off<0 || o->size<=0) { restore=1; break; }
            if (!nx_rs_range_overlap(guest,t->L.size,o->rs_guest_addr,(uint64_t)o->size)) continue;
            if (o!=exact || o->pool!=k.pool || o->rs_guest_addr!=guest || o->off!=t->off || (uint64_t)o->size!=(uint64_t)t->L.size ||
                o->family_closed || t->has_layout || t->has_pkg_data) restore=1;
        }
        if (restore && (!vk_sub_open() || !vk_rs_pass_idle() || !rs_restore_all_locked())) {
            pthread_mutex_unlock(&g_img_mu);
            die("[vk] resolution: overlapping storage lookup requires native backing before cache mutation");
            return NULL;
        }
    }
    vkimg_t *im = physical_peer && rs_is_scaled(physical_peer)?physical_peer:
        keyed ? texture_storage_lookup(&k, t) : nvn_hm_get(&g_by_serial, &t->serial);
    // A sampled wrapper can use an already-written storage image when the
    // guest describes exactly the same layout with only IMAGE cleared.
    // Keep existing owners and independently initialized textures separate.
    if (!im && keyed && !(k.d.flags & NVN_TEXTURE_FLAGS_IMAGE) &&
        !f->depth && !f->stencil && !f->compressed && !t->has_layout && !t->has_pkg_data) {
        akey producer = k;
        producer.d.flags |= NVN_TEXTURE_FLAGS_IMAGE;
        vkimg_t *peer = nvn_hm_get(&g_by_key, &producer);
        if (peer && peer->gpu_w && !peer->packaged && peer->size == (int64_t)t->L.size &&
            (!k.family || (peer->family_formats & texture_family_bit(t->d.format)))) im = peer;
    }
    if (!im) {
        if (keyed) evict_overlaps(k.pool, t->off, (int64_t)t->L.size);
        im = img_new(t, vk_fmt(k.d.format)); if (!im) { pthread_mutex_unlock(&g_img_mu); return NULL; }
        if (k.family) im->family_formats = texture_family_bit(t->d.format);
        im->pool = k.pool; im->off = t->off; im->size = (int64_t)t->L.size; im->flags = t->d.flags; g_img_n++; g_img_bytes += t->L.size;
        if (vk_rs_provenance()) im->rs_guest_addr=rs_guest_address(t);
        if (keyed) { alias_link(im); nvn_hm_put(&g_by_key, &k, im); } else nvn_hm_put(&g_by_serial, &t->serial, im);
    }
    im->used = g_tex_frame;
    pthread_mutex_unlock(&g_img_mu);
    if (!vk_sub_open()) return __atomic_load_n(&im->looked, __ATOMIC_ACQUIRE) ? im : NULL;   // looked up outside a submission (present): nothing can be recorded, a new image has no contents yet
    int first = !__atomic_exchange_n(&im->looked, 1, __ATOMIC_ACQ_REL);   // its first use is the first lookup inside a submission, not its creation (which may have been outside one)
    // a texture's first use: written by the CPU since it was registered (profile tex_dirty_upload), it is CPU-written from now on, as at nvn_mtl.m's first resolution;
    // otherwise only a later nvn_ft_poll pass finds it, and its first draws read zeros
    int nvn_ft_written(const tex_t *);
    // A streamed BC texture can be registered under a new serial while
    // keeping the same cached image. Its CPU-write mark belonged to the
    // old serial, but the image still tracks this exact CPU storage range.
    // A prior CopyBufferToTexture does not end CPU tracking. Unchanged
    // guest bytes still preserve its GPU result in the hash check below.
    int material = sampled && keyed && uncached_material_storage(t);
    int tracked_cpu_storage = im->packaged && im->pkg_mem == t->mem &&
        im->pkg_len == t->L.size && (((f->compressed || f->dec) && !(t->d.flags & NVN_TEXTURE_FLAGS_IMAGE)) || material);
    if (rs_is_scaled(im) && t->mem && (tracked_cpu_storage || packaged_tex(t) || nvn_ft_written(t) ||
        (t->d.flags & (NVN_TEXTURE_FLAGS_LINEAR | NVN_TEXTURE_FLAGS_LINEAR_RENDER_TARGET | NVN_TEXTURE_FLAGS_SPARSE)))) {
        if (vk_rs_tracking()) vk_rs_note(im,NX_RO_HOST);
        if (!vk_rs_require_native(im)) die("[vk] resolution: host upload requires native backing");
    }
    if (t->mem && lin_copy(im, t, f)) {}
    else if (t->mem && (tracked_cpu_storage || packaged_tex(t) ||
                        (first && (material || nvn_ft_written(t) || cpu_content_tex(t, f))))) {
        im->pkg_used = g_tex_frame;
        if (!im->packaged) {
            im->packaged = 1;
            pkg_upload(im, t, f);
            if (material) {
                static unsigned reported;
                if (reported++ < 24)
                    VKLOG("uncached material upload: present %llu serial %llu %ux%u format %#x levels %u pool %p+%#llx bytes %llu CPU hash %016llx",
                          (unsigned long long)g_tex_frame, (unsigned long long)t->serial, t->d.width, t->d.height,
                          t->d.format, t->d.levels, (void *)t->pool, (unsigned long long)t->off,
                          (unsigned long long)t->L.size, (unsigned long long)im->pkg_hash);
            }
        }
        else {
            uint64_t now = nvn_dirty_now();
            if (im->pkg_sparse) { if (nvn_sparse_since(t->pool, (uint64_t)(uintptr_t)t->mem, t->L.size, im->pkg_vgen, im->pkg_stamp)) pkg_upload(im, t, f); }   // mapped or written since
            else if (now != im->pkg_seen && nvn_dirty_since_span(im->pkg_span, t->mem, t->L.size, im->pkg_stamp)) {   // pages written since the upload
                // The GPU wrote the image since (a compute shader's image stores, a copy, a render) and its storage still holds the bytes of the upload: the writes were around
                // it on shared pages, and an upload would put the CPU's bytes over the GPU's. Monster Hunter Rise's spot light profiles (512x1x64 R16F, written by a compute
                // shader into memory the game had zeroed) were uploaded again as zeros at presents 60 and 1079: every spot light's profile read 0 and the faces lit only by
                // those lights (the character maker, cutscenes) came out black. A sparse texture has no memory at t->mem to hash (it takes the branch above)
                if (im->gpu_w && pkg_hash(t->mem, t->L.size) == im->pkg_hash) { nvn_dirty_protect(t->mem, t->L.size); im->pkg_stamp = nvn_dirty_now(); im->pkg_released = 0;
                    static int nl; if (nl++ < 16) VKLOG("texture %ux%u nvn format %#x at %p: its pages were written, its bytes are as uploaded and the GPU wrote it: kept", t->d.width, t->d.height, t->d.format, (const void *)t->mem); }
                else pkg_upload(im, t, f);
            }
            im->pkg_seen = now;
        }
    } else if (first) vk_tex_prepare(vk_upload_cmd(), im);
    return im;
}
// Packaged textures give up their write protection (nvn_dirty_release; drawn again, they are uploaded and protected again) when not drawn for PKG_IDLE presents, and the
// least recently drawn ones beyond that while the protected runs, each splitting its pool's mapping, are more than PKG_RUNS (textures stay protected until the game writes them).
// Textures drawn in the last PKG_KEEP presents keep theirs (released, they would be uploaded again at once). Swept every 300 presents, with the pools whose mapping the
// protection has split most mapped again in one piece (nvn_dirty_defrag: Xenoblade Chronicles 3 went from 43,000 mappings after 25 minutes of walking, toward the kernel's
// limit of 65,530, to about 17,500)
enum { PKG_IDLE = 1800, PKG_KEEP = 60, PKG_RUNS = 8000 };   // about 60 s and 2 s at 30 fps; at most about 16,000 mappings for the protection
uint64_t g_tex_frame;
typedef struct { uint64_t used; vkimg_t *im; } pkg_age;
static int pkg_age_cmp(const void *a, const void *b) { uint64_t x = ((const pkg_age *)a)->used, y = ((const pkg_age *)b)->used; return x < y ? -1 : x > y; }
void vk_tex_sweep(void) {
    if (++g_tex_frame % 300) return;
    if (vk_rs_tracking()) ro_report();
    uint64_t idle = 0, lru = 0, before = nvn_dirty_released();
    pthread_mutex_lock(&g_img_mu);
    pkg_age *v = NULL; size_t nv = 0, cap = 0;
    for (int m = 0; m < 2; m++) {
        nvn_hmap *h = m ? &g_by_serial : &g_by_key;
        for (uint32_t i = 0; i < h->cap; i++) {
            vkimg_t *im = h->vals[i];
            if (!im || !im->packaged || im->pkg_released || !im->pkg_mem) continue;
            if (g_tex_frame - im->pkg_used >= PKG_IDLE) { nvn_dirty_release(im->pkg_mem, im->pkg_len); im->pkg_released = 1; idle++; continue; }
            if (g_tex_frame - im->pkg_used < PKG_KEEP) continue;
            if (nv == cap) { cap = cap ? cap * 2 : 4096; pkg_age *nw = realloc(v, cap * sizeof *v); if (!nw) break; v = nw; }
            v[nv++] = (pkg_age){im->pkg_used, im};
        }
    }
    { int vk_pool_remap_ro(void *mem, size_t len); int nvn_dirty_defrag(int (*)(void *, size_t)); struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0); int d = nvn_dirty_defrag(vk_pool_remap_ro); clock_gettime(CLOCK_MONOTONIC, &t1); static uint64_t nd;
        if (d && (nd += d) <= 8) VKLOG("dirty-page tracking: %d pool(s) mapped again in one piece in %.1f ms (%llu so far)", d, (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6, (unsigned long long)nd); }
    int runs = nvn_dirty_runs();
    if (runs > PKG_RUNS && nv) {   // the least recently drawn first, in batches, until the runs are well under the budget
        qsort(v, nv, sizeof *v, pkg_age_cmp);
        for (size_t i = 0; i < nv && runs > PKG_RUNS * 3 / 4; ) {
            for (size_t e = i + 256; i < nv && i < e; i++) { nvn_dirty_release(v[i].im->pkg_mem, v[i].im->pkg_len); v[i].im->pkg_released = 1; lru++; }
            runs = nvn_dirty_runs();
        }
    }
    pthread_mutex_unlock(&g_img_mu);
    free(v);
    static uint64_t total; total += idle + lru;
    if (g_tex_frame % 3000 == 0) VKLOG("images: %llu live, %.0f MB of textures, %llu evicted so far (a newer texture over their memory)", (unsigned long long)g_img_n, g_img_bytes / 1048576.0, (unsigned long long)g_img_evicted);
    if ((idle || lru) && (g_tex_frame % 3000 == 0 || lru)) VKLOG("packaged textures: %llu not drawn for %d presents and %llu least recently drawn released their write protection (%llu pages; %llu so far); %d protected runs",
                    (unsigned long long)idle, PKG_IDLE, (unsigned long long)lru, (unsigned long long)(nvn_dirty_released() - before), (unsigned long long)total, runs);
}
// The keys are gathered first, 64 a pass, then deleted (a delete rehashes the table): a rescan from the start after each delete went over the whole table once per image,
// under the lock the replay's texture lookups take, for each of the pools Pokémon Scarlet drops while a capture loads
void vk_tex_drop_pool(const void *pool) {   // the pool is finalized: its textures' images go once the GPU is done with them
    pthread_mutex_lock(&g_img_mu);
    for (int n = 64; n == 64;) {
        akey ks[64]; n = 0;
        for (uint32_t i = 0; i < g_by_key.cap && n < 64; i++) {
            if (!g_by_key.vals[i]) continue;
            memcpy(&ks[n], g_by_key.keys + (size_t)i * g_by_key.klen, sizeof ks[n]);
            if (ks[n].pool == (uint64_t)(uintptr_t)pool) n++;
        }
        for (int j = 0; j < n; j++) { vkimg_t *im = nvn_hm_get(&g_by_key, &ks[j]); if (!im) continue; nvn_hm_del(&g_by_key, &ks[j]); img_drop(im); }
    }
    pthread_mutex_unlock(&g_img_mu);
}
// CPU-written textures (WriteTexels / a dirty first use): packaged from now on
static nvn_hmap g_cpu_written = {.klen = sizeof(uint64_t)}; static pthread_mutex_t g_cw_mu = PTHREAD_MUTEX_INITIALIZER;
void mtl_tex_cpu_written(const tex_t *t) { if (!t->serial) return; pthread_mutex_lock(&g_cw_mu); nvn_hm_put(&g_cpu_written, &t->serial, (void *)1); pthread_mutex_unlock(&g_cw_mu); }
// Linear textures (LINEAR) in a pool are their storage on the Switch: the CPU writes the pool memory and the GPU reads it (Metal's tex_linear makes them on the pool buffer).
// Hades II rasterizes its glyphs on the CPU into a 1024x1216 R8 IMAGE|LINEAR atlas (pool CPU_UNCACHED|GPU_CACHED, no flush, no WriteTexels) and decodes its Bink movies
// into linear planes: as GPU-only images of zeros all text, the title art and the movies were blank. A Vulkan image cannot be bound to the pool's memory (a dedicated
// AHardwareBuffer import, bound to its buffer), so each upload command buffer that a submission's draws need it in copies it from the pool buffer on the GPU (lin_copy).
// The packaged path (CPU upload, re-upload on written pages) is the fallback; as the only path the page faults of the CPU's writes cost Hades II about 120 a frame and
// 2 million while loading. Not LINEAR_RENDER_TARGET: the GPU draws into those, and a copy from storage would replace what it drew
static int linear_tex(const tex_t *t) { return t->pool && (t->d.flags & (NVN_TEXTURE_FLAGS_LINEAR | NVN_TEXTURE_FLAGS_LINEAR_RENDER_TARGET)) == NVN_TEXTURE_FLAGS_LINEAR; }
// lin_copy takes LINEAR_RENDER_TARGET textures too: Super Mario Bros. Wonder's CPU writes its sky's four 64x1 RGBA16F gradients straight into such textures (NVN flags 0x40)
// that nothing draws into, and as GPU-only images of zeros the sky drew diagonal bands of saturated colour over every level. What the GPU draws into one (gpu_w) stays, as for a
// cleared or copied linear texture, until the CPU writes its storage again. The packaged path (linear_tex) still leaves them out: a re-upload there would replace a drawing
static int linear_storage(const tex_t *t) { return t->pool && (t->d.flags & (NVN_TEXTURE_FLAGS_LINEAR | NVN_TEXTURE_FLAGS_LINEAR_RENDER_TARGET)); }
extern uint64_t g_up_gen;
static int lin_copy(vkimg_t *im, const tex_t *t, const vkfmt_t *f) {   // 1 = the image holds its storage as of this upload buffer
    if (!linear_storage(t) || im->levels != 1 || im->d != 1 || im->layers != 1 || f->bw > 1 || f->bh > 1 || !f->bpb || im->aspect != VK_IMAGE_ASPECT_COLOR_BIT) return 0;
    size_t stride = t->d.stride ? t->d.stride : (size_t)im->w * f->bpb; VkBuffer b; VkDeviceSize o;
    if (stride % f->bpb || stride < (size_t)im->w * f->bpb || !vk_buf_at((uint64_t)(uintptr_t)t->mem, (uint64_t)stride * (im->h - 1) + (uint64_t)im->w * f->bpb, &b, &o) || o % f->bpb) return 0;
    // the GPU wrote it (a clear, a copy, a storage write): it holds that until the CPU writes its storage again, which its pages, set read-only, tell
    size_t len = stride * (im->h - 1) + (size_t)im->w * f->bpb;
    if (im->gpu_w && !im->lin_mem) { nvn_dirty_protect(t->mem, len); im->lin_stamp = nvn_dirty_now(); im->lin_span = nvn_dirty_span(t->mem, len); im->lin_mem = t->mem; im->lin_len = len; return 1; }
    if (im->gpu_w) { if (!nvn_dirty_since_span(im->lin_span, t->mem, len, im->lin_stamp)) return 1; nvn_dirty_release(im->lin_mem, im->lin_len); im->lin_mem = NULL; im->gpu_w = 0; im->lin_gen = 0; }
    VkCommandBuffer cb = vk_upload_cmd();
    if (im->lin_gen == g_up_gen) return 1;
    im->lin_gen = g_up_gen;
    im->guest_clear_write = 0;
    vk_img_barrier_all(cb);   // after the reads of earlier submissions
    if (im->layout != VK_IMAGE_LAYOUT_GENERAL) vk_tex_prepare(cb, im);
    VkBufferImageCopy r = {o, (uint32_t)(stride / f->bpb), 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {im->w, im->h, 1}};
    vkCmdCopyBufferToImage(cb, b, im->img, VK_IMAGE_LAYOUT_GENERAL, 1, &r);
    return 1;
}
// A texture the CPU filled before its first use, as nxnative's tex_cpu_content (nvn_mtl.m) takes it: a sampled block-compressed texture (BC and ASTC are not colour-renderable, so
// data in its storage at first use is the CPU's), or an uncompressed 3D texture with none of the flags below (upstream 6a16b7d3, #1050: Xenoblade Chronicles 3's particle steering
// fields, 10x10x10 RGBA8 and RGBA16F, flags 0; as GPU textures of zeros every send-off particle moved along (-1, -1, -1)). It is made CPU-written: uploaded now, again when its pages
// change. The compressed case was missing here: Xenoblade Chronicles 3's particle sprites (the husks' 128x128 BC7 sRGB, flags 0) were images of zeros, so the particle
// pixel shader read alpha 0 and discarded every pixel (no red husk particles, no send-off ones). A pool the CPU cannot access is filled from its storage once in the same way, as the
// Mac's tex_init_fill does (the game cannot write it again). DM_NVN_STORAGE_FIRST_USE=0 turns this off
static int mem_any(const uint8_t *p, uint64_t n) {   // whether any byte is non-zero (stops at the first)
    uint64_t i = 0, v;
    for (; i + 8 <= n; i += 8) { memcpy(&v, p + i, 8); if (v) return 1; }
    for (; i < n; i++) if (p[i]) return 1;
    return 0;
}
// Some CPU-uncached material textures are filled in place after creation
// without packaged metadata. Recognize plain R8 mip chains in profiles
// that opt in. Track an initially empty chain too so late streaming is visible.
static int uncached_material_storage(const tex_t *t) {
    static int enabled = -1;
    if (enabled < 0) {
        const char *value = getenv("DM_VK_UNCACHED_MIP_INIT");
        enabled = value && atoi(value) != 0;
    }
    const unsigned excluded = NVN_TEXTURE_FLAGS_DISPLAY | NVN_TEXTURE_FLAGS_VIDEO_DECODE |
        NVN_TEXTURE_FLAGS_IMAGE | NVN_TEXTURE_FLAGS_COMPRESSIBLE | NVN_TEXTURE_FLAGS_LINEAR |
        NVN_TEXTURE_FLAGS_LINEAR_RENDER_TARGET | NVN_TEXTURE_FLAGS_SPARSE;
    return enabled && t->d.format == NVN_FORMAT_R8 && !t->has_layout &&
        t->d.target == NVN_TEXTURE_TARGET_2D && t->d.levels > 1 && t->d.samples <= 1 &&
        !(t->d.flags & excluded) && !(t->pool->flags & NVN_MEMORY_POOL_FLAGS_VIRTUAL) &&
        (t->pool->flags & NVN_MEMORY_POOL_FLAGS_CPU_UNCACHED);
}
static int cpu_content_tex(const tex_t *t, const vkfmt_t *f) {
    static int on = -1; if (on < 0) { const char *e = getenv("DM_NVN_STORAGE_FIRST_USE"); on = !e || atoi(e) != 0; }
    if (!on || !t->pool || t->has_layout || t->d.samples > 1 ||
        (t->pool->flags & NVN_MEMORY_POOL_FLAGS_VIRTUAL)) return 0;
    // f->compressed describes the Vulkan backing. A CPU-decoded texture
    // still has BC blocks in guest storage and needs the same first upload.
    int guest_compressed = f->compressed || f->dec;
    if (!guest_compressed && t->d.target != NVN_TEXTURE_TARGET_3D) return 0;
    // COMPRESSIBLE leaves out what the GPU may render into, which a block-compressed format never is: there the flag says nothing about who wrote the storage. With the
    // profile's tex_cpu_uncached it does not leave such a texture out: Cardfight!! Vanguard Dear Days 2 sets the flag on every texture and copies its BC7 font arrays
    // (1024x1024, 4 to 18 layers) into CPU-uncached pools with no flush; left out, they were images of zeros and no text was drawn anywhere
    if (t->d.flags & ((DM_GAME_TEX_CPU_UNCACHED && guest_compressed ? 0 : NVN_TEXTURE_FLAGS_COMPRESSIBLE) | NVN_TEXTURE_FLAGS_LINEAR | NVN_TEXTURE_FLAGS_LINEAR_RENDER_TARGET | NVN_TEXTURE_FLAGS_SPARSE)) return 0;
    // IMAGE (a render target or storage image) does not exclude a compressed texture: BC and ASTC are neither, so its bytes at first use are the CPU's.
    if (!guest_compressed && (t->d.flags & NVN_TEXTURE_FLAGS_IMAGE)) return 0;
    if (!mem_any(t->mem, t->L.size)) return 0;
    static int n; if (n++ < 8) VKLOG("texture %ux%ux%u nvn format %#x at %p (pool flags %#x): the CPU filled it before its first use: uploaded from its storage", t->d.width, t->d.height, t->d.depth, t->d.format, (const void *)t->mem, t->pool->flags);
    mtl_tex_cpu_written(t);
    return 1;
}
static int packaged_tex(const tex_t *t) {
    if (t->has_layout || linear_tex(t) || sparse_tex(t)) return 1;
    pthread_mutex_lock(&g_cw_mu); int r = nvn_hm_get(&g_cpu_written, &t->serial) != NULL; pthread_mutex_unlock(&g_cw_mu);
    return r;
}
void mtl_tex_init(const tex_t *t) { (void)t; }
// A released serial's mark goes once no queued job can still bind it: under asynchronous replay (nvn_jobs_queued, hle_nvn.c) a texture Finalized between submit and replay is still
// drawn by that submission, whose first use of it decides its content, and a mark deleted at once would have made it a GPU-only image of zeros there. The deletion waits for the
// replay of the first job queued after the release (vk_tex_release_drain, from vk_exec_reset); without a replay thread it is at once
uint64_t nvn_jobs_queued(void); uint64_t nvn_replaying_job(void);
typedef struct { uint64_t serial, seq; } cwrel_t;
static cwrel_t *g_cwrel; static int g_ncwrel, g_capcwrel;   // under g_cw_mu
void mtl_tex_release(tex_t *t) {   // images belong to their storage key, dropped with the pool; the serial's CPU-written mark goes (serials are never reused: kept, the table grew by one entry per CPU-written texture made)
    if (!t->serial) return;
    uint64_t seq = nvn_jobs_queued();
    pthread_mutex_lock(&g_cw_mu);
    if (!seq) nvn_hm_del(&g_cpu_written, &t->serial);
    else if (nvn_hm_get(&g_cpu_written, &t->serial)) {
        if (g_ncwrel == g_capcwrel) { g_capcwrel = g_capcwrel ? 2 * g_capcwrel : 256; g_cwrel = realloc(g_cwrel, sizeof *g_cwrel * (size_t)g_capcwrel); if (!g_cwrel) die("[vk] texture release list allocation failed"); }
        g_cwrel[g_ncwrel++] = (cwrel_t){t->serial, seq};
    }
    pthread_mutex_unlock(&g_cw_mu);
}
void vk_tex_release_drain(void) {   // the replay thread, at each submission (vk_exec_reset): the releases made before the job being replayed was queued
    uint64_t job = nvn_replaying_job(); if (!job) return;
    pthread_mutex_lock(&g_cw_mu);
    int o = 0; for (int i = 0; i < g_ncwrel; i++) { if (g_cwrel[i].seq < job) nvn_hm_del(&g_cpu_written, &g_cwrel[i].serial); else g_cwrel[o++] = g_cwrel[i]; }
    g_ncwrel = o;
    pthread_mutex_unlock(&g_cw_mu);
}
int mtl_tex_gpu_owned(uint64_t serial) { (void)serial; return 0; }

// ---------------- views ----------------
typedef struct vview { VkImageView v; VkImageViewType type; VkFormat fmt; uint32_t lv0, nlv, ly0, nly; VkComponentMapping swz; VkImageAspectFlags aspect; int attach; struct vview *next; } vview;   // attach: an attachment view (its usage differs from a sampling view's)
static nvn_hmap g_views = {.klen = sizeof(vkimg_t *)};   // vkimg_t* → vview list
static pthread_mutex_t g_view_mu = PTHREAD_MUTEX_INITIALIZER;
extern uint64_t g_tex_gen;
static void vk_tex_view_drop_all(vkimg_t *im) {
    pthread_mutex_lock(&g_view_mu);
    vview *v = nvn_hm_get(&g_views, &im); g_tex_gen++;
    while (v) { vview *n = v->next; vk_defer_free_view(v->v); free(v); v = n; }
    nvn_hm_del(&g_views, &im);
    pthread_mutex_unlock(&g_view_mu);
}
static VkComponentSwizzle swz_comp(int nvn) {   // NVN swizzle enum: 0 zero 1 one 2 R 3 G 4 B 5 A
    switch (nvn) { case 0: return VK_COMPONENT_SWIZZLE_ZERO; case 1: return VK_COMPONENT_SWIZZLE_ONE; case 2: return VK_COMPONENT_SWIZZLE_R; case 3: return VK_COMPONENT_SWIZZLE_G; case 4: return VK_COMPONENT_SWIZZLE_B; default: return VK_COMPONENT_SWIZZLE_A; }
}
static VkComponentSwizzle apply(VkComponentSwizzle outer, const VkComponentMapping *inner, VkComponentSwizzle self) {   // outer picks a channel of what inner produces
    if (outer == VK_COMPONENT_SWIZZLE_IDENTITY) outer = self;
    switch (outer) {
    case VK_COMPONENT_SWIZZLE_R: return inner->r == VK_COMPONENT_SWIZZLE_IDENTITY ? VK_COMPONENT_SWIZZLE_R : inner->r;
    case VK_COMPONENT_SWIZZLE_G: return inner->g == VK_COMPONENT_SWIZZLE_IDENTITY ? VK_COMPONENT_SWIZZLE_G : inner->g;
    case VK_COMPONENT_SWIZZLE_B: return inner->b == VK_COMPONENT_SWIZZLE_IDENTITY ? VK_COMPONENT_SWIZZLE_B : inner->b;
    case VK_COMPONENT_SWIZZLE_A: return inner->a == VK_COMPONENT_SWIZZLE_IDENTITY ? VK_COMPONENT_SWIZZLE_A : inner->a;
    default: return outer;
    }
}
static VkComponentMapping sample_swizzle(const VkComponentMapping *format, const tex_t *t, const view_t *v, int attachment) {
    if (attachment) return (VkComponentMapping){0};
    // NVN view selectors override texture selectors; both refer to the NVN
    // format's channels. Applying the texture first composes two selectors
    // and can turn a requested mask channel into a different component.
    int sw[4];
    if (v && v->set_swizzle) for (int c=0;c<4;c++) sw[c]=v->swz[c];
    else if (t->set_swz) for (int c=0;c<4;c++) sw[c]=(t->swz>>(3*c))&7;
    else return *format;
    return (VkComponentMapping){
        apply(swz_comp(sw[0]),format,VK_COMPONENT_SWIZZLE_R),
        apply(swz_comp(sw[1]),format,VK_COMPONENT_SWIZZLE_G),
        apply(swz_comp(sw[2]),format,VK_COMPONENT_SWIZZLE_B),
        apply(swz_comp(sw[3]),format,VK_COMPONENT_SWIZZLE_A)};
}
__thread VkFormat vk_view_fmt; __thread uint32_t vk_view_layers;
__thread VkImageSubresourceRange vk_view_range;
__thread VkComponentMapping vk_view_swizzle;
VkImageView vk_tex_view(const tex_t *t, const view_t *vw, int dim, int arrayed, int as_attachment, vkimg_t **img_out) {
    return vk_tex_view_as(t, vw, dim, arrayed, as_attachment, img_out, 0);
}
// Whether a block-compressed image drawn into through a view of format vf (one texel a block) takes a stand-in (vk_tex_view_as, DM_VK_BC_DRAW = mode), and the
// stand-in's size in blocks. Only where every level is whole in blocks of the stand-in's own chain, and its levels fit that chain: a 32x32x16 BC4 image of 6 levels
// is an 8x8x16 stand-in of 5 at most (Vulkan's mipLevels limit, max(width, height, depth)), so it keeps the direct path
static int bc_standin_ok(const vkimg_t *im, const vkfmt_t *cf, const vkfmt_t *vf, int mode, uint32_t *bw_out, uint32_t *bh_out) {
    if (!mode || !(mode > 1 || im->vtype == VK_IMAGE_VIEW_TYPE_3D) || !cf || !vf || !cf->compressed || cf->dec || vf->compressed || !cf->bw || !cf->bh || cf->bpb != vf->bpb || im->samples > 1) return 0;
    uint32_t bw = (im->w + cf->bw - 1) / cf->bw, bh = (im->h + cf->bh - 1) / cf->bh;
    for (uint32_t l = 0; l < im->levels; l++) { uint32_t w = im->w >> l ? im->w >> l : 1, h = im->h >> l ? im->h >> l : 1;
        if ((w + cf->bw - 1) / cf->bw != (bw >> l ? bw >> l : 1) || (h + cf->bh - 1) / cf->bh != (bh >> l ? bh >> l : 1)) return 0; }
    uint32_t m = bw > bh ? bw : bh, full = 1; if (im->vtype == VK_IMAGE_VIEW_TYPE_3D && im->d > m) m = im->d;
    while (m >> full) full++;
    if (im->levels > full) return 0;
    *bw_out = bw; *bh_out = bh; return 1;
}
VkImageView vk_tex_view_as(const tex_t *t, const view_t *vw, int dim, int arrayed, int as_attachment, vkimg_t **img_out, VkFormat vfmt) {
    vkimg_t *im = vk_tex_get(t, !as_attachment && !vfmt); if (img_out) *img_out = im;
    if (!im) return VK_NULL_HANDLE;
    if (as_attachment) vk_tex_mark_written(im); else if (im->novl) alias_check(im);
    vk_watch(im, as_attachment ? "attachment" : "sampled or image");
    if (!as_attachment) { void vk_texdump_note(vkimg_t *im); vk_texdump_note(im); }
    const vkfmt_t *f = vk_fmt(vw && vw->set_format ? vw->format : t->d.format); if (!f) f = vk_fmt(t->d.format);
    uint32_t lv0 = vw && vw->set_levels ? vw->base_level : 0, nlv = vw && vw->set_levels ? vw->nlevels : im->levels - lv0;
    if (lv0 >= im->levels) lv0 = im->levels - 1; if (!nlv || lv0 + nlv > im->levels) nlv = im->levels - lv0;
    // a 3D image drawn into: the view's layers are depth slices of its level (the image is 2D-array compatible). Xenoblade Chronicles 3 renders its 16x16x16 colour grading
    // LUT one slice per pass through 2D views at layers 0-15; clamped to the image's one layer, every slice went to slice 0, and the graded title sky came out flat blue
    uint32_t nlay = as_attachment && im->vtype == VK_IMAGE_VIEW_TYPE_3D ? (im->d >> lv0 ? im->d >> lv0 : 1) : im->layers;
    uint32_t ly0 = vw && vw->set_layers ? vw->min_layer : 0, nly = vw && vw->set_layers ? vw->nlayers : nlay - ly0;
    if (ly0 >= nlay) ly0 = nlay - 1; if (!nly || ly0 + nly > nlay) nly = nlay - ly0;
    // A block-compressed image drawn into through an uncompressed view of its block size (the game compresses on the GPU: one texel a block). Breath of the Wild
    // makes its 32x32x32 BC4 noise this way, each depth slice of each level through an RGBA16UI view; on Adreno 830 with Turnip 26.2.3 the image read back as zeros
    // in every slice and level afterwards, and the shaders that dissolve Link in a teleport by that noise discarded every pixel at once. With the stand-in it
    // holds noise (18,542 of its 18,744 bytes not zero) and he dissolves and comes back gradually. So such a pass draws into a stand-in in the view's format, the
    // image's size in blocks (bc_standin_ok): gfx_vk_exec.c bc_fill copies the level and layers it draws from the image into it before the pass, and bc_draw_back
    // copies them back as blocks after it, each through a buffer. DM_VK_BC_DRAW (start-up): 0 (default) draws into the image itself as before, 1 stands in for
    // 3D images, 2 for every block-compressed image. The same encoder is in other games (Super Mario 3D World + Bowser's Fury, Animal Crossing: New Horizons,
    // Captain Toad), so a profile turns it on where it was seen to help (Breath of the Wild's android.tsv); a player can try it on another with an env.txt line
    { static int mode = -1; if (mode < 0) { const char *e = getenv("DM_VK_BC_DRAW"); mode = e ? atoi(e) : 0; }
      const vkfmt_t *cf = vk_fmt(im->nvn_format); uint32_t bw = 0, bh = 0;
      if (as_attachment && vw && vw->set_format && bc_standin_ok(im, cf, f, mode, &bw, &bh)) {
          if (!im->bc_draw && !im->bc_tried) {
              im->bc_tried = 1;
              tex_t ct = *t; ct.d.format = vw->format; ct.d.width = (int)bw; ct.d.height = (int)bh;
              vkimg_t *d = img_new(&ct, f);
              if (d) { d->bc_of = im; im->bc_draw = d; }
              static int n; if (n++ < 8) VKLOG("block-compressed image %ux%ux%u, %u levels (nvn format %#x), drawn into through views of format %#x: %s", im->w, im->h, im->d, im->levels, im->nvn_format, vw->format,
                                               d ? "its passes draw into a stand-in in blocks, filled from it before each and copied into it after (DM_VK_BC_DRAW)" : "the stand-in could not be made: drawn into itself"); }
          if (im->bc_draw) { im = im->bc_draw; im->bc_lv = lv0; im->bc_ly = ly0; im->bc_nly = nly; im->gpu_w = 1; if (img_out) *img_out = im; } } }
    int target = vw && vw->set_target ? vw->target : im->target;
    VkImageViewType type = vtype_of(target);
    // what the shader declares wins over the object's target (a 2D render target sampled as an array, a cube face read as 2D)
    if (dim != 0xff && !as_attachment) {
        VkImageViewType want = dim == 0 ? (arrayed ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D) : dim == 1 ? (arrayed ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D) :
                               dim == 2 ? VK_IMAGE_VIEW_TYPE_3D : dim == 3 ? (arrayed ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : VK_IMAGE_VIEW_TYPE_CUBE) : type;
        if (want == VK_IMAGE_VIEW_TYPE_2D && type == VK_IMAGE_VIEW_TYPE_2D_ARRAY) nly = 1;
        if (want == VK_IMAGE_VIEW_TYPE_CUBE || want == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY) {
            // Returning a 2D view for a cube shader bypassed tex_desc_for's typed dummy. The saved creation flag also requires a square,
            // single-sample 2D image; having six layers alone is insufficient. Check the selected range before either view cache can keep a wrong type.
            if (!(im->cflags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) || nly < 6 || (want == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY && nly % 6)) {
                VKRED_ONCE(106, "cube view refused: image %ux%ux%u layers %u cflags %#x, levels %u+%u range %u+%u type %d", im->w, im->h, im->d, im->layers, im->cflags, lv0, nlv, ly0, nly, want);
                return VK_NULL_HANDLE;
            }
            if (want == VK_IMAGE_VIEW_TYPE_CUBE) nly = 6;
        } else {
            if (want == VK_IMAGE_VIEW_TYPE_3D && im->d == 1) want = type;
            if ((want == VK_IMAGE_VIEW_TYPE_1D || want == VK_IMAGE_VIEW_TYPE_1D_ARRAY) != (type == VK_IMAGE_VIEW_TYPE_1D || type == VK_IMAGE_VIEW_TYPE_1D_ARRAY)) want = type;
        }
        type = want;
    }
    if (as_attachment) { type = im->layers > 1 && nly > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : (im->vtype == VK_IMAGE_VIEW_TYPE_3D ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D); nlv = 1; }
    if (type == VK_IMAGE_VIEW_TYPE_2D || type == VK_IMAGE_VIEW_TYPE_1D || type == VK_IMAGE_VIEW_TYPE_3D) nly = 1;
    VkImageAspectFlags aspect = im->aspect;
    if (aspect == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) && !as_attachment)
        aspect = vk_view_selects_stencil(f, t, vw) ? VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_DEPTH_BIT;   // (STENCIL8 alone may be stored combined)
    VkComponentMapping swz = sample_swizzle(&f->swz,t,vw,as_attachment);
    VkFormat fmt = f->vk;
    const vkfmt_t *imf = vk_fmt(im->nvn_format);   // (a BC format decoded on the CPU counts as compressed: its view keeps the decoded image's format)
    // A block-compressed image drawn into through an uncompressed view of its block size (RGBA32UI over BC3: Mario Kart 8 Deluxe's reflection cubes, Tomodachi Life's Mii
    // face pictures) keeps the view's format: the image is BLOCK_TEXEL_VIEW_COMPATIBLE (img_new) for exactly that, one level and one layer a view. Given the image's own
    // BC3 format by the rule below, the attachment was a compressed view, which nothing can draw into: the Mii maker's alternative-face tiles stayed blank (Odin 3, 2026-11-14)
    int block_view = as_attachment && (im->cflags & VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT) && imf && imf->compressed && !imf->dec &&
                     !f->compressed && !f->dec && f->bpb == imf->bpb && nlv == 1 && nly == 1;
    if (!block_view && ((im->aspect & ~VK_IMAGE_ASPECT_COLOR_BIT) || (f->compressed || f->dec) != (imf->compressed || imf->dec) || (f->dec && f->dbpp != imf->dbpp))) fmt = im->fmt;   // depth views and block-size changes keep the image's format
    // a storage view in the integer class of the shader's declaration (vfmt, the caller's texel size): identity swizzle, as an image takes no channel mapping. Only where the
    // image allows it: its format list (fixed_fmt) names it, else the image is MUTABLE_FORMAT
    if (vfmt && !as_attachment && im->aspect == VK_IMAGE_ASPECT_COLOR_BIT && (!im->fixed_fmt || vfmt == im->fmt_cls[0] || vfmt == im->fmt_cls[1])) { fmt = vfmt; swz = (VkComponentMapping){0}; }
    if (im->fixed_fmt && fmt != im->fmt && fmt != srgb_twin(im->fmt) && fmt != im->fmt_cls[0] && fmt != im->fmt_cls[1]) { fmt = im->fmt; static int n; if (n++ < 8) VKLOG("[RED] view format %d of a format-listed image (%d, DM_VK_UBWC): the image's own used", f->vk, im->fmt); }
    if (vk_rs_tracking()) {
        uint32_t flags=as_attachment ? NX_RO_ATTACH : 0;
        if (type!=VK_IMAGE_VIEW_TYPE_2D || lv0 || ly0 || nlv!=1 || nly!=1 ||
            fmt!=im->fmt || block_view || vfmt || swz.r || swz.g || swz.b || swz.a) flags|=NX_RO_VIEW;
        vk_rs_note(im,flags);
    }
    pthread_mutex_lock(&g_view_mu);
    vview *head = nvn_hm_get(&g_views, &im);
    for (vview *v = head; v; v = v->next)
        if (v->type == type && v->fmt == fmt && v->lv0 == lv0 && v->nlv == nlv && v->ly0 == ly0 && v->nly == nly && v->aspect == aspect && v->attach == (as_attachment != 0) && !memcmp(&v->swz, &swz, sizeof swz)) { pthread_mutex_unlock(&g_view_mu); vk_view_fmt = fmt; vk_view_layers = nly; vk_view_range = (VkImageSubresourceRange){aspect, lv0, nlv, ly0, nly}; vk_view_swizzle = swz; return v->v; }
    VkImageViewUsageCreateInfo uci = {VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO, NULL, 0};
    VkFormatProperties fp; vkGetPhysicalDeviceFormatProperties(V.pd, fmt, &fp);
    uci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (as_attachment) uci.usage = (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : 0) | (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : 0);
    else if ((t->d.flags & NVN_TEXTURE_FLAGS_IMAGE) && (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) uci.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    VkImageViewCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, &uci, 0, im->img, type, fmt, swz, {aspect, lv0, nlv, ly0, nly}};
    VkImageView view; VkResult r = vkCreateImageView(V.dev, &ci, NULL, &view);
    if (r) { pthread_mutex_unlock(&g_view_mu); VKRED_ONCE(15, "vkCreateImageView failed (%d): type %d format %d levels %u+%u layers %u+%u of image %ux%ux%u", r, type, fmt, lv0, nlv, ly0, nly, im->w, im->h, im->layers); return VK_NULL_HANDLE; }
    vview *nv = calloc(1, sizeof *nv); *nv = (vview){view, type, fmt, lv0, nlv, ly0, nly, swz, aspect, as_attachment != 0, head};
    nvn_hm_put(&g_views, &im, nv);
    pthread_mutex_unlock(&g_view_mu);
    vk_view_fmt = fmt; vk_view_layers = nly; vk_view_range = ci.subresourceRange;
    vk_view_swizzle = swz;
    return view;
}

// ---------------- texture / sampler ID tables ----------------
// A registration as the replay of each submission sees it. NVN's texture pool is memory the GPU reads as it runs, and a game rewrites an ID once the work
// that used it is done; with the replay on its own thread (DM_NVN_ASYNC) a submission is replayed after the game has gone on, and Xenoblade Chronicles 3
// rewrites about 90 IDs a second with other textures: a frame replayed late drew with the next frame's textures (whole-screen garbage while the camera
// turned). So each registration keeps the number of replay jobs queued when it was made, and a job sees the newest one made before it was queued
// (nvn_jobs_queued / nvn_replaying_job, gfx/nvn/hle_nvn.c); without a replay thread that is always the newest. TREG_OLD older ones per ID, kept for IDs rewritten
enum { TREG_OLD = 3 };
typedef struct { tex_t t; view_t v; uint8_t ok, has_v; uint64_t seq; } treg_ver;
typedef struct { treg_ver cur; treg_ver *old; } treg_t;   // old: TREG_OLD earlier versions, newest first (allocated at the first rewrite)
static treg_t *g_treg; static int g_ntreg;
uint64_t g_tex_gen = 1;   // changes whenever a texture ID is registered again or views go (gfx_vk_exec.c's per-submission view cache)
uint64_t g_samp_gen = 1;  // changes whenever a sampler ID is registered with other parameters (gfx_vk_exec.c samp_cached and the descriptor set memo)
static samp_par *g_sreg; static uint8_t *g_sreg_ok; static VkSampler *g_samp; static int g_nsreg;
// Samplers as a bind saw them. Super Mario Odyssey hands out sampler IDs from a cache of slots and registers a slot again with other parameters while a command
// buffer it has not submitted yet still binds that ID: the first pass of its bloom (a 400x228 chain, the level read chosen by the sampler's LOD range 0..0) bound a
// sampler that was then registered again as LOD 1..1 to 3..3, and read the chain's previous upsampled level. The bloom came out 2.7 times too strong for that frame:
// the glow around the moon flashed while the camera turned, in about 1% of frames (3 of 332 first passes; issue #42). Every registration that changes an ID's
// parameters counts (g_sregs), each bind records the count when it was made (mtl_samp_seq, hle_nvn.c), and a bind resolves its sampler to the newest version
// registered before it (vk_samp_at): SREG_OLD earlier versions are kept per ID. DM_NVN_SAMP_SNAP=0 (live, gfx_vk_exec.c): the newest, as before
enum { SREG_OLD = 3 };
typedef struct { samp_par p; uint64_t seq; } sreg_ver;
static uint64_t *g_sseq; static sreg_ver *g_sold; static uint64_t g_sregs;   // the current version's count, SREG_OLD older ones per ID (newest first)
uint64_t mtl_samp_seq(void) { return __atomic_load_n(&g_sregs, __ATOMIC_ACQUIRE); }
static pthread_mutex_t g_rmu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_rs_sync_cv=PTHREAD_COND_INITIALIZER;
static int g_rs_sync_freeze;
__attribute__((weak)) uint64_t nvn_jobs_queued(void) { return 0; }
__attribute__((weak)) uint64_t nvn_replaying_job(void) { return 0; }
__attribute__((weak)) uint64_t nvn_rs_window_last_job(void) {return 0;}
void mtl_reg_tex(int tid, const tex_t *t, const view_t *v) {
    if (tid < 0) return;
    pthread_mutex_lock(&g_rmu);
    while (g_rs_sync_freeze) pthread_cond_wait(&g_rs_sync_cv,&g_rmu);
    uint64_t seq=nvn_jobs_queued();
    if (tid >= g_ntreg) { int n = tid + 1024; g_treg = realloc(g_treg, n * sizeof *g_treg); memset(g_treg + g_ntreg, 0, (n - g_ntreg) * sizeof *g_treg); g_ntreg = n; }
    g_tex_gen++; treg_t *r = &g_treg[tid];
    if (r->cur.ok && seq) {   // keep the one replaced for the jobs queued before this registration
        if (!r->old) r->old = calloc(TREG_OLD, sizeof *r->old);
        if (r->old) { memmove(r->old + 1, r->old, sizeof *r->old * (TREG_OLD - 1)); r->old[0] = r->cur; }
    }
    r->cur.ok = t != NULL; if (t) r->cur.t = *t; r->cur.has_v = v != NULL; if (v) r->cur.v = *v; r->cur.seq = seq;
    pthread_mutex_unlock(&g_rmu);
}
void mtl_set_tex_pool_n(int n) { (void)n; }
static __thread treg_ver t_treg;   // the version a lookup found, copied (the table may be rewritten or grow meanwhile)
// Only the replay thread uses this bounded scratch. Snapshot all shader
// texture IDs used by a candidate scope before publishing scaled owners.
enum { RS_FREEZE_MAX=96 };
typedef struct { unsigned n; int active,sealed,sync_guard; struct { int tid; treg_ver ver; } e[RS_FREEZE_MAX]; } rs_freeze_table;
static __thread rs_freeze_table *t_rs_freeze;
int vk_rs_scope_freeze_begin(void) {
    if (!t_rs_freeze) t_rs_freeze=calloc(1,sizeof *t_rs_freeze);
    if (!t_rs_freeze || t_rs_freeze->active) return 0;
    t_rs_freeze->sync_guard=0;
    if (!nvn_replaying_job()) {
        pthread_mutex_lock(&g_rmu);
        if (g_rs_sync_freeze) {pthread_mutex_unlock(&g_rmu);return 0;}
        g_rs_sync_freeze=1;t_rs_freeze->sync_guard=1;
        pthread_mutex_unlock(&g_rmu);
    }
    t_rs_freeze->n=0;t_rs_freeze->sealed=0;t_rs_freeze->active=1;return 1;
}
void vk_rs_scope_freeze_seal(void) {
    t_rs_freeze->sealed=1;
    pthread_mutex_lock(&g_rmu); g_tex_gen++; pthread_mutex_unlock(&g_rmu);
}
void vk_rs_scope_freeze_end(void) {
    if (!t_rs_freeze || !t_rs_freeze->active) return;
    if (t_rs_freeze->sealed || t_rs_freeze->sync_guard) {
        pthread_mutex_lock(&g_rmu);
        if (t_rs_freeze->sealed) g_tex_gen++;
        if (t_rs_freeze->sync_guard) {g_rs_sync_freeze=0;pthread_cond_broadcast(&g_rs_sync_cv);}
        pthread_mutex_unlock(&g_rmu);
    }
    t_rs_freeze->active=t_rs_freeze->sealed=t_rs_freeze->sync_guard=0;t_rs_freeze->n=0;
}
vkimg_t *vk_tex_by_id(int tid, const view_t **view_out, const tex_t **tex_out) {
    rs_freeze_table *freeze=t_rs_freeze;
    if (freeze && freeze->active) {
        for (unsigned i=0;i<freeze->n;i++) if (freeze->e[i].tid==tid) {
            t_treg=freeze->e[i].ver;
            if (view_out) *view_out=t_treg.has_v?&t_treg.v:NULL;
            if (tex_out) *tex_out=&t_treg.t;
            return (vkimg_t *)1;
        }
        if (freeze->sealed || freeze->n==RS_FREEZE_MAX) return NULL;
    }
    uint64_t job = nvn_replaying_job();
    pthread_mutex_lock(&g_rmu);
    const treg_ver *r = tid > 0 && tid < g_ntreg ? &g_treg[tid].cur : NULL;
    if (r && job && r->seq >= job && g_treg[tid].old)   // registered after this job was queued: the newest earlier one
        for (int k = 0; k < TREG_OLD; k++) { const treg_ver *o = &g_treg[tid].old[k]; if (!o->ok) break; r = o; if (o->seq < job) break; }
    int ok = r && r->ok;
    if (ok && freeze && freeze->active &&
        !nx_rs_window_descriptor(job,nvn_rs_window_last_job(),r->seq,
                                 r==&g_treg[tid].cur,freeze->sync_guard)) ok=0;
    if (ok) t_treg = *r;
    pthread_mutex_unlock(&g_rmu);
    if (ok && freeze && freeze->active) {
        unsigned i=freeze->n++; freeze->e[i].tid=tid; freeze->e[i].ver=t_treg;
    }
    if (!ok) return NULL;
    if (view_out) *view_out = t_treg.has_v ? &t_treg.v : NULL; if (tex_out) *tex_out = &t_treg.t;
    return (vkimg_t *)1;   // presence only: the caller makes the view with vk_tex_view
}
static VkSamplerAddressMode wrap(int w) {
    switch (w) { case 1: return VK_SAMPLER_ADDRESS_MODE_REPEAT; case 6: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT; case 2: case 3: return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
                 case 4: case 5: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER; default: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; }
}
static VkCompareOp cmp_op(int f) { return f >= 1 && f <= 8 ? (VkCompareOp)(f - 1) : VK_COMPARE_OP_ALWAYS; }   // NVN NEVER..ALWAYS = 1..8, Vulkan 0..7 in the same order
static VkSampler make_sampler(const samp_par *p) {
    VkSamplerCustomBorderColorCreateInfoEXT cb = {VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT};
    VkSamplerReductionModeCreateInfo rd = {VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO, NULL, p->reduction == 1 ? VK_SAMPLER_REDUCTION_MODE_MIN : p->reduction == 2 ? VK_SAMPLER_REDUCTION_MODE_MAX : VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE};
    VkSamplerCreateInfo ci = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, p->reduction ? &rd : NULL};
    ci.magFilter = p->magf ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    ci.minFilter = p->minf == 1 || p->minf == 3 || p->minf == 5 ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    ci.mipmapMode = p->minf == 4 || p->minf == 5 ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    ci.addressModeU = wrap(p->ws); ci.addressModeV = wrap(p->wt); ci.addressModeW = wrap(p->wr);
    // Vulkan requires |mipLodBias| <= maxSamplerLodBias, maxLod >= minLod,
    // and the samplerAnisotropy feature for anisotropy (gated by V.max_aniso).
    // NVN's bias range is -16..15.99; no mipmap filter: level 0 only (GL's rule).
    float bias_limit = V.props.limits.maxSamplerLodBias;
    ci.mipLodBias = fminf(fminf(15.99f, bias_limit), fmaxf(fmaxf(-16, -bias_limit), p->lod_bias));
    ci.maxAnisotropy = fminf(V.max_aniso, fmaxf(1, floorf(p->aniso)));
    ci.anisotropyEnable = ci.maxAnisotropy > 1;
    ci.compareEnable = p->cmp_mode == 1; ci.compareOp = cmp_op(p->cmp_func);
    ci.minLod = p->minf >= 2 ? fmaxf(0, p->lod_min) : 0;
    ci.maxLod = p->minf >= 2 ? fmaxf(ci.minLod, p->lod_max) : 0.25f;
    // DM_VK_MIP_MAX=<level> (A/B): cap every sampler's chosen mip at this level (0 = always level 0). A ground plane seen at a shallow angle picks a very high mip;
    // if a material reads flat while its neighbours are detailed, this says whether the mip choice or the texture/UVs is at fault
    { static int mm = -2; if (mm < -1) { const char *e = getenv("DM_VK_MIP_MAX"); mm = e && *e ? atoi(e) : -1; }   // -2: not read, -1: unset
      if (mm >= 0) { if (ci.maxLod > (float)mm) ci.maxLod = (float)mm; if (ci.minLod > ci.maxLod) ci.minLod = ci.maxLod; if (ci.mipLodBias < 0) ci.mipLodBias = 0; } }   // (maxLod < minLod is invalid Vulkan usage)
    int border = ci.addressModeU == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER || ci.addressModeV == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER || ci.addressModeW == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    ci.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    if (border) {
        const float *b = p->border;
        if (b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 1) ci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        else if (b[0] == 1 && b[1] == 1 && b[2] == 1 && b[3] == 1) ci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        else if ((b[0] || b[1] || b[2] || b[3]) && V.has_custom_border) {
            ci.borderColor = VK_BORDER_COLOR_FLOAT_CUSTOM_EXT; memcpy(cb.customBorderColor.float32, b, 16); cb.format = VK_FORMAT_UNDEFINED;
            cb.pNext = (void *)ci.pNext; ci.pNext = &cb;
        }
    }
    VkSampler s; VkResult sr = vkCreateSampler(V.dev, &ci, NULL, &s);
    if (sr) {   // silent before: a sampler the driver refuses made the material that uses it sample with whatever the default binding gives (a flat colour)
        VKRED_ONCE(90, "vkCreateSampler failed (%d): filters %d/%d, mip %d, wrap %d/%d/%d, border %d, custom border %d, aniso %.1f (max %.1f), compare %d, lod %g..%g bias %g",
                   sr, ci.magFilter, ci.minFilter, ci.mipmapMode, ci.addressModeU, ci.addressModeV, ci.addressModeW, ci.borderColor, ci.pNext == &cb, p->aniso, V.max_aniso, ci.compareOp, ci.minLod, ci.maxLod, ci.mipLodBias);
        return VK_NULL_HANDLE; }
    return s;
}
volatile int g_vk_samp_log;   // DM_VK_SAMP_LOG=<n> (live, gfx_vk_exec.c x_read): the next n sampler registrations, in time order with the replay's lines
volatile int g_vk_samp_log_boot;   // the start-up value mtl_reg_samp took below, which x_read's first read must not arm a second time
void mtl_reg_samp(int sid, const samp_par *sp) {
    if (sid < 0) return;
    { static int once; if (!once) { once = 1; const char *e = getenv("DM_VK_SAMP_LOG"); if (e && atoi(e) > 0) g_vk_samp_log_boot = g_vk_samp_log = atoi(e); } }   // (games register samplers at start, before the replay's live switches are read)
    if (g_vk_samp_log > 0) { extern uint64_t g_tex_frame; g_vk_samp_log--; LOG("[vk] sampler registered: id %d lod %g..%g min %d wrap %d/%d/%d aniso %.1f compare %d border %.2f,%.2f,%.2f,%.2f (present %llu, thread %d)",
        sid, sp->lod_min, sp->lod_max, sp->minf, sp->ws, sp->wt, sp->wr, sp->aniso, sp->cmp_mode, sp->border[0], sp->border[1], sp->border[2], sp->border[3], (unsigned long long)g_tex_frame, (int)gettid()); }
    pthread_mutex_lock(&g_rmu);
    if (sid >= g_nsreg) { int n = sid + 256; g_sreg = realloc(g_sreg, n * sizeof *g_sreg); g_sreg_ok = realloc(g_sreg_ok, n); g_samp = realloc(g_samp, n * sizeof *g_samp);
        g_sseq = realloc(g_sseq, n * sizeof *g_sseq); g_sold = realloc(g_sold, (size_t)n * SREG_OLD * sizeof *g_sold);
        memset(g_sreg_ok + g_nsreg, 0, n - g_nsreg); memset(g_samp + g_nsreg, 0, (n - g_nsreg) * sizeof *g_samp); memset(g_sseq + g_nsreg, 0, (n - g_nsreg) * sizeof *g_sseq);
        memset(g_sold + (size_t)g_nsreg * SREG_OLD, 0, (size_t)(n - g_nsreg) * SREG_OLD * sizeof *g_sold); g_nsreg = n; }
    if (!g_sreg_ok[sid] || memcmp(&g_sreg[sid], sp, sizeof *sp)) {
        sreg_ver *o = g_sold + (size_t)sid * SREG_OLD;
        if (g_sreg_ok[sid]) { memmove(o + 1, o, sizeof *o * (SREG_OLD - 1)); o[0] = (sreg_ver){g_sreg[sid], g_sseq[sid]}; }
        g_sreg[sid] = *sp; g_sreg_ok[sid] = 1; g_samp[sid] = VK_NULL_HANDLE; g_sseq[sid] = __atomic_add_fetch(&g_sregs, 1, __ATOMIC_RELEASE); __atomic_fetch_add(&g_samp_gen, 1, __ATOMIC_RELEASE); }   // samplers are cached by parameters below
    pthread_mutex_unlock(&g_rmu);
}
static nvn_hmap g_samp_cache = {.klen = sizeof(samp_par)};   // samp_par → VkSampler (VkSamplers are never destroyed: few distinct parameter sets)
static const samp_par *samp_at(int sid, uint64_t seq);
void vk_samp_describe_at(int sid, uint64_t seq, char *out, size_t n) {   // the registered parameters a bind made after seq registrations sees (traces; 0: the newest)
    pthread_mutex_lock(&g_rmu);
    if (sid > 0 && sid < g_nsreg && g_sreg_ok[sid]) { const samp_par *p = samp_at(sid, seq); snprintf(out, n, "sampler %d: wrap %d %d %d min %d mag %d lod %g..%g bias %g aniso %g cmp %d/%d red %d border %g %g %g %g", sid, p->ws, p->wt, p->wr,
        p->minf, p->magf, p->lod_min, p->lod_max, p->lod_bias, p->aniso, p->cmp_mode, p->cmp_func, p->reduction, p->border[0], p->border[1], p->border[2], p->border[3]); }
    else snprintf(out, n, "sampler %d: not registered (dummy)", sid);
    pthread_mutex_unlock(&g_rmu);
}
void vk_samp_describe(int sid, char *out, size_t n) { vk_samp_describe_at(sid, 0, out, n); }
static const samp_par *samp_at(int sid, uint64_t seq) {   // under g_rmu: the version a bind made after seq registrations sees; NULL when not registered
    if (sid <= 0 || sid >= g_nsreg || !g_sreg_ok[sid]) return NULL;
    if (!seq || g_sseq[sid] <= seq) return &g_sreg[sid];
    const sreg_ver *o = g_sold + (size_t)sid * SREG_OLD;
    for (int k = 0; k < SREG_OLD; k++) if (o[k].seq && o[k].seq <= seq) return &o[k].p;
    return &g_sreg[sid];   // older than what is kept: the newest
}
VkSampler vk_samp_at(int sid, uint64_t seq, int nearest, char *description, size_t size, int *linear) {   // the sampler a bind made after seq registrations sees (gfx_vk_exec.c samp_for)
    pthread_mutex_lock(&g_rmu);
    VkSampler s = VK_NULL_HANDLE; const samp_par *q = samp_at(sid, seq);
    if (q) { samp_par p = *q;
        if (nearest) { p.magf = 0; p.minf = p.minf == 1 ? 0 : p.minf == 3 || p.minf == 4 || p.minf == 5 ? 2 : p.minf; p.aniso = 1; p.reduction = 0; }
        if (!(s = nvn_hm_get(&g_samp_cache, &p)) && (s = make_sampler(&p))) nvn_hm_put(&g_samp_cache, &p, s);
        if (s && description) snprintf(description, size,
            "wrap=%d,%d,%d;min=%d;mag=%d;lod=%.9g,%.9g;bias=%.9g;aniso=%.9g;compare=%d,%d;reduction=%d;border=%.9g,%.9g,%.9g,%.9g",
            p.ws, p.wt, p.wr, p.minf, p.magf, p.lod_min, p.lod_max, p.lod_bias, p.aniso,
            p.cmp_mode, p.cmp_func, p.reduction, p.border[0], p.border[1], p.border[2], p.border[3]);
    }
    if (linear) {
        // Keep the certificate unchanged. Report the actual sampler resolved
        // at the bind's sequence while still holding the registration lock.
        unsigned refusal=(!s?1u:0u)|(!q?2u:0u)|(nearest?4u:0u);
        if(q) {
            if(q->magf!=1)refusal|=8u;
            if(q->minf!=1&&q->minf!=3&&q->minf!=5)refusal|=16u;
            if(wrap(q->ws)!=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE ||
               wrap(q->wt)!=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)refusal|=32u;
            if(!(q->aniso<2))refusal|=64u;
            if(q->cmp_mode)refusal|=128u;
            if(q->reduction)refusal|=256u;
        }
        *linear=refusal==0;
        if(refusal) {
            static unsigned logged;
            static struct {int sid,nearest;unsigned refusal;uint64_t seq;} seen[16];
            unsigned i;
            for(i=0;i<logged;i++)
                if(seen[i].sid==sid&&seen[i].nearest==nearest&&seen[i].refusal==refusal&&seen[i].seq==seq)break;
            if(i==logged&&logged<16) {
                seen[logged++]=(typeof(seen[0])){sid,nearest,refusal,seq};
                if(q) VKLOG("[upscale] guest sampler refusal %#x: id %d seq %llu nearest %d wrap %d/%d/%d min %d mag %d lod %.3f..%.3f bias %.3f aniso %.3f compare %d reduction %d",
                    refusal,sid,(unsigned long long)seq,nearest,q->ws,q->wt,q->wr,q->minf,q->magf,
                    q->lod_min,q->lod_max,q->lod_bias,q->aniso,q->cmp_mode,q->reduction);
                else VKLOG("[upscale] guest sampler refusal %#x: id %d seq %llu has no registration",refusal,sid,(unsigned long long)seq);
            }
        }
    }
    if (!s && description) snprintf(description, size, "dummy");
    pthread_mutex_unlock(&g_rmu);
    return s;
}
VkSampler vk_samp_by_id(int sid) {
    pthread_mutex_lock(&g_rmu);
    VkSampler s = VK_NULL_HANDLE;
    if (sid > 0 && sid < g_nsreg && g_sreg_ok[sid]) {
        s = g_samp[sid];
        if (!s) { s = nvn_hm_get(&g_samp_cache, &g_sreg[sid]); if (!s) { s = make_sampler(&g_sreg[sid]); if (s) nvn_hm_put(&g_samp_cache, &g_sreg[sid], s); } g_samp[sid] = s; }
    }
    pthread_mutex_unlock(&g_rmu);
    return s;
}
// The same sampler without filtering, for integer views: Maxwell reads integer texels unfiltered whatever the sampler says, while Vulkan forbids linear filtering of
// formats without SAMPLED_IMAGE_FILTER_LINEAR (Pokémon Legends: Arceus samples R8_UINT and R32_UINT views with trilinear samplers; the validation layer named them)
VkSampler vk_samp_by_id_nearest(int sid) {   // cached by its parameters, as the samplers themselves
    pthread_mutex_lock(&g_rmu);
    VkSampler s = VK_NULL_HANDLE;
    if (sid > 0 && sid < g_nsreg && g_sreg_ok[sid]) {
        samp_par p = g_sreg[sid]; p.magf = 0; p.minf = p.minf == 1 ? 0 : p.minf == 3 || p.minf == 4 || p.minf == 5 ? 2 : p.minf; p.aniso = 1; p.reduction = 0;
        if (!(s = nvn_hm_get(&g_samp_cache, &p)) && (s = make_sampler(&p))) nvn_hm_put(&g_samp_cache, &p, s);
    }
    pthread_mutex_unlock(&g_rmu);
    return s;
}
VkSampler vk_dummy_sampler(void) {
    static VkSampler s; if (s) return s;
    samp_par p = {0}; p.magf = 1; p.minf = 1; p.lod_max = 1000; p.aniso = 1; s = make_sampler(&p); return s;
}
// ---------------- dummies: what an unbound slot reads (zeros) ----------------
VkImageView vk_dummy_view(int dim, int arrayed, int ms, int storage, int is_int) {
    static VkImageView cache[4][2][2][2][2]; static vkimg_t *imgs[4][2][2][2]; static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    int d = dim == 0 ? 0 : dim == 2 ? 2 : dim == 3 ? 3 : 1; arrayed = !!arrayed; ms = !!ms && d == 1; storage = !!storage; is_int = !!is_int;
    pthread_mutex_lock(&mu);
    VkImageView v = cache[d][arrayed][ms][storage][is_int];
    if (!v) {
        vkimg_t **pi = &imgs[d][ms][storage][is_int];
        if (!*pi) {
            tex_t t; memset(&t, 0, sizeof t); t.magic = MAGIC('T', 'E', 'X', 0); t.serial = 1;
            t.d.format = is_int ? 0x27 : 0x25; t.d.width = t.d.height = 1; t.d.depth = d == 3 ? 12 : 6; t.d.levels = 1; t.d.samples = ms ? 4 : 0;
            t.d.target = d == 0 ? 3 : d == 2 ? 2 : d == 3 ? 9 : ms ? 6 : 4; t.d.flags = storage ? NVN_TEXTURE_FLAGS_IMAGE : 0;
            if (d == 2) t.d.depth = 1;
            *pi = img_new(&t, vk_fmt(t.d.format));
            VkCommandBuffer cb = vk_upload_cmd(); vk_tex_prepare(cb, *pi);
            VkClearColorValue z = {{0}}; VkImageSubresourceRange rg = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, VK_REMAINING_ARRAY_LAYERS};
            if (!ms) vkCmdClearColorImage(cb, (*pi)->img, VK_IMAGE_LAYOUT_GENERAL, &z, 1, &rg);
        }
        vkimg_t *im = *pi;
        VkImageViewType type = d == 0 ? (arrayed ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D) : d == 2 ? VK_IMAGE_VIEW_TYPE_3D : d == 3 ? (arrayed ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : VK_IMAGE_VIEW_TYPE_CUBE) : (arrayed ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D);
        uint32_t nly = type == VK_IMAGE_VIEW_TYPE_CUBE ? 6 : type == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY ? 12 : arrayed ? im->layers : 1;
        VkImageViewCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, im->img, type, im->fmt, {0}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, nly}};
        VKCHK(vkCreateImageView(V.dev, &ci, NULL, &v)); cache[d][arrayed][ms][storage][is_int] = v;
    }
    pthread_mutex_unlock(&mu);
    return v;
}
VkBufferView vk_dummy_tbuf(int storage) {
    static VkBufferView v[2]; static VkBuffer b; static VkDeviceMemory m; storage = !!storage;
    if (v[storage]) return v[storage];
    if (!b) {
        VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, 65536, VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_SHARING_MODE_EXCLUSIVE};
        VKCHK(vkCreateBuffer(V.dev, &bi, NULL, &b)); VkMemoryRequirements req; vkGetBufferMemoryRequirements(V.dev, b, &req);
        VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, req.size, vk_mem_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0)};
        VKCHK(vkAllocateMemory(V.dev, &ai, NULL, &m)); VKCHK(vkBindBufferMemory(V.dev, b, m, 0));
        void *p; VKCHK(vkMapMemory(V.dev, m, 0, VK_WHOLE_SIZE, 0, &p)); memset(p, 0, 65536);
    }
    VkBufferViewCreateInfo ci = {VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO, NULL, 0, b, VK_FORMAT_R32_UINT, 0, 65536};
    VKCHK(vkCreateBufferView(V.dev, &ci, NULL, &v[storage]));
    return v[storage];
}
// Uniform and storage texel views share pools. Any direct view must meet
// both alignments because the backing VkBuffer advertises both usages.
static VkDeviceSize texel_alignment(const vkfmt_t *f) {
    const VkPhysicalDeviceTexelBufferAlignmentProperties *p = &V.texel_alignment;
    if (!p->uniformTexelBufferOffsetAlignmentBytes || !p->storageTexelBufferOffsetAlignmentBytes)
        return V.props.limits.minTexelBufferOffsetAlignment;
    VkDeviceSize texel = f->bpb % 3 == 0 ? f->bpb / 3 : f->bpb;
    VkDeviceSize uniform = p->uniformTexelBufferOffsetAlignmentBytes;
    VkDeviceSize storage = p->storageTexelBufferOffsetAlignmentBytes;
    if (p->uniformTexelBufferOffsetSingleTexelAlignment && texel < uniform) uniform = texel;
    if (p->storageTexelBufferOffsetSingleTexelAlignment && texel < storage) storage = texel;
    return uniform > storage ? uniform : storage;
}

static int texel_range(const tex_t *t, const vkfmt_t **format, VkBuffer *buf, VkDeviceSize *off, VkDeviceSize *n) {
    const vkfmt_t *f = vk_fmt(t->d.format);
    if (!f || !f->bpb || !t->mem || !t->d.width ||
        t->d.width > V.props.limits.maxTexelBufferElements) return 0;
    *format = f;
    *n = (VkDeviceSize)t->d.width * f->bpb;
    return vk_buf_at((uint64_t)(uintptr_t)t->mem, *n, buf, off);
}

// Record before descriptor assembly, outside a render pass. A CPU copy can
// predate an earlier compute write even when both use the same mapped pool.
VkBufferView vk_tbuf_snapshot(const tex_t *t, int *direct) {
    const vkfmt_t *f;
    VkBuffer src, dst;
    VkDeviceSize off, aligned, n;
    if (!texel_range(t, &f, &src, &off, &n)) return VK_NULL_HANDLE;
    if (off % texel_alignment(f) == 0) {
        *direct = 1;
        return VK_NULL_HANDLE;
    }
    vk_end_pass_land();
    vk_scratch(n, V.props.limits.minTexelBufferOffsetAlignment, &dst, &aligned);
    VkBufferView view = VK_NULL_HANDLE;
    VkBufferViewCreateInfo ci = {VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO, NULL, 0, dst, f->vk, aligned, n};
    if (vkCreateBufferView(V.dev, &ci, NULL, &view) != VK_SUCCESS) return VK_NULL_HANDLE;
    VkCommandBuffer cb = vk_cmd();
    VkBufferCopy copy = {off, aligned, n};
    vk_img_barrier_all(cb);
    vkCmdCopyBuffer(cb, src, dst, 1, &copy);
    vk_img_barrier_all(cb);
    vk_defer_free_bview(view);
    return view;
}
typedef struct { uint64_t mem, pool; uint32_t fmt, n; } tbkey;   // pool: the chunk's serial (vkpool_t.serial)
static nvn_hmap g_tbuf_cache = {.klen = sizeof(tbkey)}; static pthread_mutex_t g_tbuf_mu = PTHREAD_MUTEX_INITIALIZER;
// A pool chunk goes (gfx_vk_core.c mtl_pool_del): the views over its buffer leave the cache and are destroyed once the GPU is past the submission open or next (vk_defer_free_bview,
// listed before the buffer itself). Keyed by the serial they were never handed out again (a pool made at the same address has another serial), but never destroyed either: one
// VkBufferView and one entry per buffer texture bound from every pool ever made stayed for the life of the process (a game that makes and drops pools as it streams kept growing)
void vk_tbuf_drop_pool(uint64_t serial) {
    for (int n = 64; n == 64;) {   // 64 keys a pass (nvn_hm_del rehashes), the views destroyed outside the lock
        tbkey ks[64]; VkBufferView vs[64]; n = 0;
        pthread_mutex_lock(&g_tbuf_mu);
        for (uint32_t i = 0; i < g_tbuf_cache.cap && n < 64; i++) {
            if (!g_tbuf_cache.vals[i]) continue;
            memcpy(&ks[n], g_tbuf_cache.keys + (size_t)i * g_tbuf_cache.klen, sizeof ks[n]);
            if (ks[n].pool == serial) n++;
        }
        int nv = 0; for (int j = 0; j < n; j++) { VkBufferView v = nvn_hm_del(&g_tbuf_cache, &ks[j]); if (v) vs[nv++] = v; }
        pthread_mutex_unlock(&g_tbuf_mu);
        for (int j = 0; j < nv; j++) vk_defer_free_bview(vs[j]);
    }
}
VkBufferView vk_tbuf_view(const tex_t *t, int storage) {
    // keyed by the pool too: a pool finalized and another made at the same address has a new VkBuffer, and the old one's memory is freed once the GPU is past it. Keyed by address
    // alone, the view over the old buffer was handed out again and shaders read and wrote freed memory, which the kernel gives to newer buffers (Xenoblade Chronicles 3: the
    // allocator kept inside one of its memory pools had its tree bins overwritten during the opening movie, and malloc crashed at the next scene)
    vkpool_t *pl = t->mem ? vk_pool_of((uint64_t)(uintptr_t)t->mem, 1) : NULL;
    tbkey k = {(uint64_t)(uintptr_t)t->mem, pl ? pl->serial : 0, (uint32_t)t->d.format, t->d.width};
    pthread_mutex_lock(&g_tbuf_mu);
    VkBufferView v = nvn_hm_get(&g_tbuf_cache, &k);
    if (!v) {
        const vkfmt_t *f;
        VkBuffer b;
        VkDeviceSize off, n;
        if (texel_range(t, &f, &b, &off, &n)) {
            VkDeviceSize align = texel_alignment(f);
            if (off % align == 0) {
                VkBufferViewCreateInfo ci = {VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO, NULL, 0, b, f->vk, off, n};
                if (vkCreateBufferView(V.dev, &ci, NULL, &v) == VK_SUCCESS) nvn_hm_put(&g_tbuf_cache, &k, v);
                else v = VK_NULL_HANDLE;
            } else {
                VKRED_ONCE(16, "%s texel buffer at unsupported offset %llu (requires %llu alignment, format %#x)",
                    storage ? "storage" : "sampled", (unsigned long long)off, (unsigned long long)align, t->d.format);
            }
        } else {
            VKRED_ONCE(16, "invalid texel buffer extent or pool (format %#x, %u elements)", t->d.format, t->d.width);
        }
    }
    pthread_mutex_unlock(&g_tbuf_mu);
    return v;
}
// A depth / stencil image read back by the CPU (mtl_read_texels) is copied aspect by aspect: a buffer copy of the depth aspect holds 2 bytes a texel (D16) or 4 (D32F; D24
// in the low 24 bits of 4) and one of the stencil aspect 1, tightly packed, whatever the combined texel's size. The guest's texels (bpb bytes each) are rebuilt from them:
// DEPTH32F_STENCIL8 the float, then the stencil byte, then 3 zero bytes (Maxwell's ZF32_X24S8); DEPTH24_STENCIL8 the depth's low 24 bits with the stencil in the top byte
// (the depth where the copy puts it, as before); STENCIL8 the stencil alone (also where it is kept in a combined image); DEPTH24 kept as D32F (a device without X8_D24
// attachments) back to 24-bit unorm; DEPTH16 and DEPTH32F as copied. Before, the depth aspect alone was copied at the guest's pitch: DEPTH32F_STENCIL8's 8-byte rows over
// 4-byte texels misread every row after the first (the rest stale scratch bytes), and STENCIL8 in a combined image read depth
static void ds_texels(int nvn_format, VkFormat host, const uint8_t *dep, const uint8_t *sten, size_t n, uint8_t *out) {
    for (size_t i = 0; i < n; i++) {
        uint32_t d = 0; if (dep) memcpy(&d, dep + (host == VK_FORMAT_D16_UNORM ? 2 : 4) * i, host == VK_FORMAT_D16_UNORM ? 2 : 4);
        uint8_t st = sten ? sten[i] : 0;
        if ((nvn_format == 0x33 || nvn_format == 0x35) &&
            (host == VK_FORMAT_D32_SFLOAT || host == VK_FORMAT_D32_SFLOAT_S8_UINT)) {
            float depth;
            memcpy(&depth, &d, 4);
            d = vk_d24_copy_float_to_unorm(depth);
        }
        switch (nvn_format) {
        case 0x31: out[i] = st; break;   // STENCIL8
        case 0x32: memcpy(out + 2 * i, &d, 2); break;   // DEPTH16
        case 0x33: d &= 0xffffffu; memcpy(out + 4 * i, &d, 4); break;   // DEPTH24
        case 0x35: d = (d & 0xffffffu) | (uint32_t)st << 24; memcpy(out + 4 * i, &d, 4); break;   // DEPTH24_STENCIL8
        case 0x36: memcpy(out + 8 * i, &d, 4); out[8 * i + 4] = st; memset(out + 8 * i + 5, 0, 3); break;   // DEPTH32F_STENCIL8
        default: memcpy(out + 4 * i, &d, 4); break;   // DEPTH32F
        }
    }
}
// CPU readback of texels (ReadTexels): the image copied to scratch memory by a submission of its own, waited for
void mtl_read_texels(const tex_t *t, unsigned level, unsigned layer0, const NVNcopyRegion *r, uint8_t *dst, size_t row, size_t img) {
    void mtl_sub_begin(void); uint64_t mtl_sub_commit(void); int mtl_wait(uint64_t, uint64_t);
    mtl_sub_begin();
    vkimg_t *im = vk_tex(t); const vkfmt_t *f = vk_fmt(t->d.format);
    if (vk_rs_tracking()) vk_rs_note(im,NX_RO_HOST);
    size_t depth = r->depth ? r->depth : 1;
    if (!im || !f) { mtl_sub_commit(); for (size_t z = 0; z < depth; z++) for (int y = 0; y < r->height; y++) memset(dst + z * img + (size_t)y * row, 0, row); return; }
    if (f->dec) { VKRED_ONCE(17, "ReadTexels of a BC texture decoded on the CPU: zeros returned"); mtl_sub_commit(); for (size_t z = 0; z < depth; z++) for (int y = 0; y < (r->height + 3) / 4; y++) memset(dst + z * img + (size_t)y * row, 0, row); return; }
    unsigned bw = f->bw ? f->bw : 1, bh = f->bh ? f->bh : 1;
    size_t pitch = (size_t)((r->width + bw - 1) / bw) * f->bpb, rows = (r->height + bh - 1) / bh;
    int is3d = im->d > 1;
    if (!(im->aspect & VK_IMAGE_ASPECT_COLOR_BIT)) {   // depth / stencil: each aspect copied apart, then the guest's texels (ds_texels above)
        size_t n = (size_t)r->width * rows * depth, db = im->fmt == VK_FORMAT_D16_UNORM ? 2 : 4;
        int want_d = (im->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) && t->d.format != 0x31, want_s = (im->aspect & VK_IMAGE_ASPECT_STENCIL_BIT) && (t->d.format == 0x31 || t->d.format == 0x35 || t->d.format == 0x36);
        VkBuffer db_b = VK_NULL_HANDLE, sb_b = VK_NULL_HANDLE; VkDeviceSize db_o = 0, sb_o = 0; uint8_t *dp = NULL, *sp = NULL;
        if (want_d) dp = vk_scratch(n * db, 16, &db_b, &db_o);
        if (want_s) sp = vk_scratch(n, 16, &sb_b, &sb_o);
        VkCommandBuffer cb = vk_cmd(); vk_tex_prepare(cb, im); vk_img_barrier_all(cb);
        for (int a = 0; a < 2; a++) { if (!(a ? sp : dp)) continue;
            VkBufferImageCopy c = {a ? sb_o : db_o, 0, 0, {a ? VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_DEPTH_BIT, level, is3d ? 0 : layer0 + (uint32_t)r->zoffset, is3d ? 1 : (uint32_t)depth},
                                   {r->xoffset, r->yoffset, is3d ? r->zoffset : 0}, {(uint32_t)r->width, (uint32_t)r->height, is3d ? (uint32_t)depth : 1}};
            vkCmdCopyImageToBuffer(cb, im->img, VK_IMAGE_LAYOUT_GENERAL, a ? sb_b : db_b, 1, &c); }
        mtl_wait(mtl_sub_commit(), ~0ull);
        // Normalize only the observed nonconforming Xclipse D24S8 copy
        // before passing bytes to the original tested NVN ds_texels
        // decoder. This preserves its ABI and all other depth formats.
        const char *opt = getenv("DM_VK_D24_FLOAT_COPY");
        if (dp && im->fmt == VK_FORMAT_D24_UNORM_S8_UINT &&
            (t->d.format == 0x33 || t->d.format == 0x35) &&
            opt && atoi(opt) != 0) {
            for (size_t i = 0; i < n; ++i) {
                float depthValue;
                memcpy(&depthValue, dp + 4 * i, sizeof depthValue);
                uint32_t packed = vk_d24_copy_float_to_unorm(depthValue);
                memcpy(dp + 4 * i, &packed, sizeof packed);
            }
            static int logged; if (!logged++)
                VKLOG("[d24] converted nonconforming Xclipse float32 D24 transfer to packed NVN D24 ReadTexels");
        }
        uint8_t *line = malloc(pitch); if (!line) { for (size_t z = 0; z < depth; z++) for (size_t y = 0; y < rows; y++) memset(dst + z * img + y * row, 0, row); return; }
        for (size_t z = 0; z < depth; z++) for (size_t y = 0; y < rows; y++) { size_t k = (z * rows + y) * (size_t)r->width;
            ds_texels(t->d.format, im->fmt, dp ? dp + k * db : NULL, sp ? sp + k : NULL,
                      (size_t)r->width, line);
            memcpy(dst + z * img + y * row, line, pitch < row ? pitch : row); }
        free(line); return;
    }
    VkBuffer sb; VkDeviceSize so; uint8_t *p = vk_scratch(pitch * rows * depth, 16, &sb, &so);
    VkCommandBuffer cb = vk_cmd(); vk_tex_prepare(cb, im); vk_img_barrier_all(cb);
    VkBufferImageCopy c = {so, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, level, is3d ? 0 : layer0 + (uint32_t)r->zoffset, is3d ? 1 : (uint32_t)depth},
                           {r->xoffset, r->yoffset, is3d ? r->zoffset : 0}, {(uint32_t)r->width, (uint32_t)r->height, is3d ? (uint32_t)depth : 1}};
    vkCmdCopyImageToBuffer(cb, im->img, VK_IMAGE_LAYOUT_GENERAL, sb, 1, &c);
    mtl_wait(mtl_sub_commit(), ~0ull);
    for (size_t z = 0; z < depth; z++) for (size_t y = 0; y < rows; y++) memcpy(dst + z * img + y * row, p + (z * rows + y) * pitch, pitch < row ? pitch : row);
}
