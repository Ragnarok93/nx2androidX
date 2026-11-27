#pragma once

#include <vulkan/vulkan.h>
#include "gfx_vk_temporal_policy.h"

enum { VK_GSR2_SLOTS = 8 };
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
    vk_gsr2_input color, depth, motion, opaque, previous_color;
    float clip_to_prev[16]; // column-major, excluding sampling jitter
    float jitter[2];       // signed input pixels
    float pre_exposure;    // previous/current scene pre-exposure
    float tan_half_fov_x, near_plane, min_lerp;
    int same_camera;
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

// Effective values of shader uniforms whose roles replay has verified.
// The depth view must store projected Z with a zero-to-one viewport range.
typedef struct {
    float projection[16]; // column-major perspective view-to-clip
    float current_pre_exposure, previous_pre_exposure;
    int clip_y_down;
} vk_gsr2_replay_view;

typedef struct {
    float jittered[16], unjittered[16]; // column-major world-to-clip
} vk_gsr2_replay_camera;

typedef struct {
    vk_gsr2_replay_view view;
    vk_gsr2_replay_camera current, previous;
} vk_gsr2_replay_constants;

// Derive camera, jitter, perspective and exposure inputs from recorded
// uniforms. Both matrix pairs must use the same world origin and clip space.
int vk_gsr2_set_replay_constants(vk_gsr2_inputs *input, VkExtent2D render,
                                 const vk_gsr2_replay_constants *uniforms);

// The shader may already expose a current-to-previous clip transform. Supply
// its column-major matrix and both frames' NDC jitter, and indicate whether
// the matrix maps jittered clip coordinates or unjittered clip coordinates.
int vk_gsr2_set_replay_clip(vk_gsr2_inputs *input, VkExtent2D render,
                            const vk_gsr2_replay_view *view, const float clip_to_prev[16],
                            const float current_jitter[2], const float previous_jitter[2],
                            int jittered);

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
// Image-driven two-pass variant. The previous source must be the prior real
// game frame at the same size and orientation, or the current image on a cut.
vk_gsr2 *vk_gsr2_create_image(VkExtent2D render, VkExtent2D output, VkDeviceSize budget);
int vk_gsr2_record_image(vk_gsr2 *ctx, VkCommandBuffer cb, unsigned slot,
                         const dm_temporal_frame *frame, vk_gsr2_input current,
                         vk_gsr2_input previous, vk_gsr2_output *output);
// Owns a budgeted pair of scene snapshots. Replay must prove that scene is
// a fully defined RGBA8 UNORM color image in GENERAL, with one mip, one layer,
// one sample, transfer source usage and exactly the context's render extent.
vk_gsr2 *vk_gsr2_create_replay(VkExtent2D render, VkExtent2D output, VkDeviceSize budget);
int vk_gsr2_record_replay(vk_gsr2 *ctx, VkCommandBuffer cb, unsigned slot,
                          const dm_temporal_frame *frame, VkImage scene,
                          const vk_gsr2_timing *timing, vk_gsr2_output *output);
// The caller owns the layout. The context owns its sampling sets and pool.
VkDescriptorSetLayout vk_gsr2_create_replay_layout(void);
int vk_gsr2_replay_bindings(vk_gsr2 *ctx, VkDescriptorSetLayout layout);
// Update the sampling descriptor in the same retired slot used to record result.
VkDescriptorSet vk_gsr2_replay_sample(vk_gsr2 *ctx, unsigned slot,
                                      const vk_gsr2_output *result);
void vk_gsr2_destroy(vk_gsr2 *ctx);

// Invoke on the GPU replay queue, outside a render pass. Slot must be retired
// by the caller before reuse. Finish/commit in the same submitting thread.
// An accepted output is ready to read in GENERAL on the same Vulkan queue.
// Consumers must record those reads before a later dispatch can overwrite it.
int vk_gsr2_record(vk_gsr2 *ctx, VkCommandBuffer cb, unsigned slot,
                   const dm_temporal_frame *frame, const vk_gsr2_inputs *inputs,
                   const vk_gsr2_timing *timing, vk_gsr2_output *output);
void vk_gsr2_commit(vk_gsr2 *ctx, int submitted);
