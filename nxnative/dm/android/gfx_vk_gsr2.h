#pragma once

#include <vulkan/vulkan.h>
#include "gfx_vk_temporal_policy.h"

enum { VK_GSR2_SLOTS = 3 };

// An adapter supplies sampleable, one-mip, single-sample views at iw x ih.
// Scene color is linear. Depth is normalized standard-Z, with 1 at the far
// plane. Encoded motion is current minus previous clip-space position, with
// positive-up Y (zero texel only for static geometry handled by reprojection).
typedef struct {
    VkImageView view;
    VkImageLayout layout;
} vk_gsr2_input;

typedef struct {
    vk_gsr2_input color, depth, motion, opaque;
    float clip_to_prev[16]; // column-major, excluding sampling jitter
    float jitter[2];       // signed input pixels
    float pre_exposure;    // previous/current exposure
    float tan_half_fov_x, near_plane, min_lerp;
    int same_camera;
} vk_gsr2_inputs;

typedef struct vk_gsr2 vk_gsr2;
typedef struct {
    VkImage image;
    VkImageView view;
    VkImageLayout layout;
    VkExtent2D extent;
} vk_gsr2_output;
// Optional, caller-owned range of six timestamp queries in this GPU slot.
typedef struct {
    VkQueryPool pool;
    uint32_t first;
} vk_gsr2_timing;

// One context per replay stream. The input/output size and variant are fixed
// for its lifetime. Destroy it after every recorded read or write has retired.
uint32_t vk_gsr2_variants(void);
vk_gsr2 *vk_gsr2_create(uint32_t variant, VkExtent2D render, VkExtent2D output,
                       VkDeviceSize budget);
void vk_gsr2_destroy(vk_gsr2 *ctx);

// Invoke on the GPU replay queue, outside a render pass. Slot must be retired
// by the caller before reuse. Finish/commit in the same submitting thread.
// An accepted output is ready to read in GENERAL on the same Vulkan queue.
// Consumers must record those reads before a later dispatch can overwrite it.
int vk_gsr2_record(vk_gsr2 *ctx, VkCommandBuffer cb, unsigned slot,
                   const dm_temporal_frame *frame, const vk_gsr2_inputs *inputs,
                   const vk_gsr2_timing *timing, vk_gsr2_output *output);
void vk_gsr2_commit(vk_gsr2 *ctx, int submitted);
