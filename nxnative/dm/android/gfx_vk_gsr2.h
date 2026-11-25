#pragma once

#include <vulkan/vulkan.h>
#include "gfx_vk_temporal_policy.h"

enum { VK_GSR2_SLOTS = 3 };
enum { VK_GSR2_DEPTH_STANDARD, VK_GSR2_DEPTH_REVERSED };
enum { VK_GSR2_MOTION_UE, VK_GSR2_MOTION_NDC_A, VK_GSR2_MOTION_UV_A, VK_GSR2_MOTION_PIXELS_A };
enum { VK_GSR2_MOTION_Y_DOWN = 1, VK_GSR2_MOTION_PREV_MINUS_CURRENT = 2 };

// An adapter supplies sampleable, one-mip, single-sample views at iw x ih.
// Scene color is linear. Depth is a depth-aspect or scalar color view in [0,1].
// Motion excludes jitter. The default is UE packed NDC with zero X for camera
// reprojection. The signed float formats use alpha > 0 for valid object motion
// (including zero vectors), and alpha <= 0 for static geometry.
typedef struct {
    VkImageView view;
    VkImageLayout layout;
} vk_gsr2_input;

typedef struct {
    vk_gsr2_input color, depth, motion, opaque;
    float clip_to_prev[16]; // column-major, excluding sampling jitter
    float jitter[2];       // signed input pixels
    float pre_exposure;    // compute color compression/expansion scale
    float tan_half_fov_x, near_plane, min_lerp;
    int same_camera, exposure_changed;
    unsigned depth_convention, motion_format, motion_flags;
} vk_gsr2_inputs;

typedef struct {
    float world_to_clip[16]; // column-major, includes the declared projection jitter
    float jitter_ndc[2];    // actual clip.xy / clip.w offset in the source axes
} vk_gsr2_camera;

// The two matrices must use the same world origin and a zero-to-one clip depth.
// Derives unjittered positive-up, standard-Z reprojection and top-left pixel jitter.
// The adapter supplies near plane, FOV and exposure separately from verified inputs.
int vk_gsr2_set_camera(vk_gsr2_inputs *input, VkExtent2D render,
                      const vk_gsr2_camera *current, const vk_gsr2_camera *previous,
                      int clip_y_down, unsigned clip_depth_convention);

// Effective values of shader uniforms whose roles replay has verified. Both
// transform pairs use the same world origin. Each unjittered matrix describes
// the same camera as its paired jittered matrix. The projection describes
// the current view, in the same zero-to-one clip space as the transforms.
// The depth view must store that projected Z with a 0-to-1 viewport range.
typedef struct {
    float jittered[16], unjittered[16]; // column-major world-to-clip
} vk_gsr2_replay_camera;

typedef struct {
    vk_gsr2_replay_camera current, previous;
    float projection[16]; // column-major perspective view-to-clip
    float current_pre_exposure, previous_pre_exposure;
    int clip_y_down;
} vk_gsr2_replay_constants;

// Derive camera, jitter, perspective and exposure-change inputs from recorded
// uniforms. Reject ambiguous layouts, depth conventions or jitter pairs.
// This validates numeric relationships. Replay must establish uniform roles,
// the shared origin, and same-frame image bindings before calling it.
int vk_gsr2_set_replay_constants(vk_gsr2_inputs *input, VkExtent2D render,
                                 const vk_gsr2_replay_constants *uniforms);

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
