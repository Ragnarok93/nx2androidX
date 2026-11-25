// Qualcomm Snapdragon GSR 2 compute and fragment passes for normalized replay inputs.
#ifndef SGSR2_HOST_PROBE
#include "gfx_vk.h"
#endif
#include "gfx_vk_gsr2.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "sgsr2_2fs_convert_frag.spv.h"
#include "sgsr2_2fs_upscale_frag.spv.h"
#include "sgsr2_fullscreen_vert.spv.h"
#include "sgsr2_2cs_convert_comp.spv.h"
#include "sgsr2_2cs_upscale_comp.spv.h"
#include "sgsr2_3cs_convert_comp.spv.h"
#include "sgsr2_3cs_activate_comp.spv.h"
#include "sgsr2_3cs_upscale_comp.spv.h"

typedef struct {
    uint32_t render[2], output[2];
    float render_rcp[2], output_rcp[2], jitter[2], pad[2];
    float clip_to_prev[16];
    float exposure, tan_half_fov_x, near_plane, min_lerp;
    uint32_t same_camera, reset, tail[2];
} compute_params;

typedef struct {
    float clip_to_prev[16];
    float render[2], output[2], render_rcp[2], output_rcp[2], jitter[2], scale_ratio[2];
    float tan_half_fov_x, min_lerp, reset;
    uint32_t same_camera;
} fragment_params;

_Static_assert(sizeof(compute_params) == 144 && offsetof(compute_params, clip_to_prev) == 48 &&
               offsetof(compute_params, exposure) == 112 && offsetof(compute_params, reset) == 132,
               "Qualcomm compute std140 UBO");
_Static_assert(sizeof(fragment_params) == 128 && offsetof(fragment_params, render) == 64 &&
               offsetof(fragment_params, scale_ratio) == 104 && offsetof(fragment_params, reset) == 120,
               "Qualcomm fragment std140 UBO");

enum { G2_CLIP, G2_MOTION, G2_YCOCG, G2_RESULT, G2_HISTORY0, G2_HISTORY1, G2_LUMA0, G2_LUMA1, G2_IMAGES };
enum { G2_COLOR = G2_IMAGES, G2_DEPTH, G2_VELOCITY, G2_OPAQUE, G2_PREV, G2_NEXT, G2_PREV_LUMA, G2_NEXT_LUMA };
enum { G2_PASS_MAX = 3, G2_BINDINGS_MAX = 7 };

typedef struct {
    uint32_t binding, image;
    VkDescriptorType type;
    int linear;
} pass_binding;

typedef struct {
    const uint32_t *code;
    size_t code_size;
    unsigned count;
    pass_binding binding[G2_BINDINGS_MAX];
    unsigned target;
} pass_spec;

#define G2_UBO {0, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0}
#define G2_READ(b, image) {b, image, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0}
#define G2_LINEAR(b, image) {b, image, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}
#define G2_WRITE(b, image) {b, image, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0}
#define G2_SHADER(name) name, sizeof name

static const pass_spec passes_2fs[] = {
    {G2_SHADER(sgsr2_2fs_convert_frag_spv), 3,
        {G2_UBO, G2_READ(1, G2_DEPTH), G2_READ(2, G2_VELOCITY)}, G2_CLIP},
    {G2_SHADER(sgsr2_2fs_upscale_frag_spv), 4,
        {G2_UBO, G2_LINEAR(1, G2_PREV), G2_READ(2, G2_CLIP), G2_READ(3, G2_COLOR)}, G2_NEXT},
};
static const pass_spec passes_2cs[] = {
    {G2_SHADER(sgsr2_2cs_convert_comp_spv), 6,
        {G2_UBO, G2_READ(1, G2_COLOR), G2_READ(2, G2_DEPTH), G2_READ(3, G2_VELOCITY),
         G2_WRITE(4, G2_CLIP), G2_WRITE(5, G2_YCOCG)}, G2_CLIP},
    {G2_SHADER(sgsr2_2cs_upscale_comp_spv), 6,
        {G2_UBO, G2_WRITE(4, G2_RESULT), G2_WRITE(5, G2_NEXT),
         G2_LINEAR(7, G2_PREV), G2_READ(8, G2_CLIP), G2_READ(9, G2_YCOCG)}, G2_RESULT},
};
static const pass_spec passes_3cs[] = {
    {G2_SHADER(sgsr2_3cs_convert_comp_spv), 7,
        {G2_UBO, G2_READ(1, G2_OPAQUE), G2_READ(2, G2_COLOR), G2_READ(3, G2_DEPTH),
         G2_READ(4, G2_VELOCITY), G2_WRITE(5, G2_YCOCG), G2_WRITE(6, G2_MOTION)}, G2_MOTION},
    {G2_SHADER(sgsr2_3cs_activate_comp_spv), 6,
        {G2_UBO, G2_READ(1, G2_PREV_LUMA), G2_READ(2, G2_MOTION), G2_READ(3, G2_YCOCG),
         G2_WRITE(4, G2_CLIP), G2_WRITE(5, G2_NEXT_LUMA)}, G2_CLIP},
    {G2_SHADER(sgsr2_3cs_upscale_comp_spv), 6,
        {G2_UBO, G2_LINEAR(1, G2_PREV), G2_READ(2, G2_CLIP), G2_READ(3, G2_YCOCG),
         G2_WRITE(4, G2_NEXT), G2_WRITE(5, G2_RESULT)}, G2_RESULT},
};

typedef struct {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkExtent2D extent;
} temporal_image;

typedef struct {
    VkDescriptorSetLayout set_layout;
    VkPipelineLayout layout;
    VkPipeline pipeline;
    VkDescriptorSet sets[VK_GSR2_SLOTS];
} temporal_pass;

struct vk_gsr2 {
    uint32_t variant, pass_count, next;
    VkExtent2D render, output;
    int fragment, pending, initialized;
    VkDeviceSize bytes, budget, ubo_stride;
    VkDescriptorPool descriptors;
    VkSampler samplers[2];
    VkBuffer ubo;
    VkDeviceMemory ubo_memory;
    void *ubo_mapped;
    const pass_spec *spec;
    temporal_pass passes[G2_PASS_MAX];
    temporal_image images[G2_IMAGES];
    dm_temporal_history history;
    dm_temporal_frame pending_frame;
    dm_temporal_plan pending_plan;
};

static const VkImageSubresourceRange temporal_color = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

static int gsr2_format_supported(VkFormat format, VkFormatFeatureFlags flags) {
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(V.pd, format, &props);
    return (props.optimalTilingFeatures & flags) == flags;
}

uint32_t vk_gsr2_variants(void) {
    VkFormatFeatureFlags sample = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    VkFormatFeatureFlags f16 = sample | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
                              VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    uint32_t variants = 0;
    if (V.has_dynrender && gsr2_format_supported(VK_FORMAT_R16G16B16A16_SFLOAT,
                                                f16 | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT))
        variants |= DM_TEMPORAL_2FS;
    if (gsr2_format_supported(VK_FORMAT_R16G16B16A16_SFLOAT, f16 | VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) &&
        gsr2_format_supported(VK_FORMAT_R32_UINT, sample | VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
                                                  VK_FORMAT_FEATURE_TRANSFER_DST_BIT)) {
        variants |= DM_TEMPORAL_2CS;
        VkPhysicalDeviceFeatures2 features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        vkGetPhysicalDeviceFeatures2(V.pd, &features);
        if (features.features.shaderImageGatherExtended) variants |= DM_TEMPORAL_3CS;
    }
    return variants;
}

static int gsr2_image(vk_gsr2 *ctx, unsigned index, VkExtent2D extent, VkFormat format) {
    temporal_image *image = &ctx->images[index];
    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                              (ctx->fragment ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : VK_IMAGE_USAGE_STORAGE_BIT);
    // Only the exposed result needs transfer source usage.
    if (index == G2_RESULT || (ctx->fragment && (index == G2_HISTORY0 || index == G2_HISTORY1)))
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    VkImageFormatProperties properties;
    if (vkGetPhysicalDeviceImageFormatProperties(V.pd, format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
          usage, 0, &properties) ||
        extent.width > properties.maxExtent.width || extent.height > properties.maxExtent.height) return 0;
    VkImageCreateInfo create = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, 0, VK_IMAGE_TYPE_2D, format,
        {extent.width, extent.height, 1}, 1, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, usage};
    if (vkCreateImage(V.dev, &create, NULL, &image->image)) return 0;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(V.dev, image->image, &req);
    if (req.size > ctx->budget - ctx->bytes) return 0;
    uint32_t type = vk_mem_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
    if (type == UINT32_MAX) return 0;
    VkMemoryAllocateInfo alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, req.size, type};
    if (vkAllocateMemory(V.dev, &alloc, NULL, &image->memory) ||
        vkBindImageMemory(V.dev, image->image, image->memory, 0)) return 0;
    ctx->bytes += req.size;
    VkImageViewCreateInfo view = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, image->image,
        VK_IMAGE_VIEW_TYPE_2D, format, {0}, temporal_color};
    image->extent = extent;
    return vkCreateImageView(V.dev, &view, NULL, &image->view) == VK_SUCCESS;
}

static int gsr2_images(vk_gsr2 *ctx) {
    VkFormat rgba = VK_FORMAT_R16G16B16A16_SFLOAT, integer = VK_FORMAT_R32_UINT;
    if (!gsr2_image(ctx, G2_CLIP, ctx->render, rgba) ||
        !gsr2_image(ctx, G2_HISTORY0, ctx->output, rgba) ||
        !gsr2_image(ctx, G2_HISTORY1, ctx->output, rgba)) return 0;
    if (ctx->fragment) return 1;
    if (!gsr2_image(ctx, G2_YCOCG, ctx->render, integer) ||
        !gsr2_image(ctx, G2_RESULT, ctx->output, rgba)) return 0;
    if (ctx->variant != DM_TEMPORAL_3CS) return 1;
    return gsr2_image(ctx, G2_MOTION, ctx->render, rgba) &&
           gsr2_image(ctx, G2_LUMA0, ctx->render, integer) &&
           gsr2_image(ctx, G2_LUMA1, ctx->render, integer);
}

static int gsr2_uniform(vk_gsr2 *ctx) {
    VkDeviceSize align = V.props.limits.minUniformBufferOffsetAlignment;
    ctx->ubo_stride = (sizeof(compute_params) + align - 1) & ~(align - 1);
    VkBufferCreateInfo create = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0,
        ctx->ubo_stride * VK_GSR2_SLOTS, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT};
    if (vkCreateBuffer(V.dev, &create, NULL, &ctx->ubo)) return 0;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(V.dev, ctx->ubo, &req);
    uint32_t type = vk_mem_type(req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0);
    if (type == UINT32_MAX || req.size > ctx->budget - ctx->bytes) return 0;
    VkMemoryAllocateInfo alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, req.size, type};
    if (vkAllocateMemory(V.dev, &alloc, NULL, &ctx->ubo_memory) ||
        vkBindBufferMemory(V.dev, ctx->ubo, ctx->ubo_memory, 0)) return 0;
    ctx->bytes += req.size;
    return vkMapMemory(V.dev, ctx->ubo_memory, 0, VK_WHOLE_SIZE, 0, &ctx->ubo_mapped) == VK_SUCCESS;
}

static VkResult gsr2_fragment_pipeline(VkPipelineLayout layout, VkShaderModule vertex,
                                       VkShaderModule pixel, VkPipeline *output) {
    VkPipelineShaderStageCreateInfo stages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, vertex, "main"},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, pixel, "main"},
    };
    VkPipelineVertexInputStateCreateInfo vi = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        NULL, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportStateCreateInfo vp = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, NULL, 0, 1, NULL, 1};
    VkPipelineRasterizationStateCreateInfo raster = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, NULL, 0, VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState write = {.colorWriteMask = 0xf};
    VkPipelineColorBlendStateCreateInfo blend = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        NULL, 0, VK_FALSE, VK_LOGIC_OP_COPY, 1, &write};
    VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, NULL, 0, 2, states};
    VkFormat rgba = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkPipelineRenderingCreateInfo rendering = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, NULL, 0, 1, &rgba};
    VkGraphicsPipelineCreateInfo create = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &rendering,
        0, 2, stages, &vi, &ia, NULL, &vp, &raster, &ms, NULL, &blend, &dynamic, layout};
    return vkCreateGraphicsPipelines(V.dev, V.pcache, 1, &create, NULL, output);
}

static int gsr2_pass(vk_gsr2 *ctx, unsigned index, VkShaderModule vertex) {
    const pass_spec *spec = &ctx->spec[index];
    temporal_pass *pass = &ctx->passes[index];
    VkShaderStageFlagBits stage = ctx->fragment ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutBinding bindings[G2_BINDINGS_MAX];
    for (unsigned i = 0; i < spec->count; i++)
        bindings[i] = (VkDescriptorSetLayoutBinding){spec->binding[i].binding, spec->binding[i].type, 1, stage};
    VkDescriptorSetLayoutCreateInfo set = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0,
        spec->count, bindings};
    if (vkCreateDescriptorSetLayout(V.dev, &set, NULL, &pass->set_layout)) return 0;
    VkPipelineLayoutCreateInfo layout = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 1, &pass->set_layout};
    if (vkCreatePipelineLayout(V.dev, &layout, NULL, &pass->layout)) return 0;
    VkDescriptorSetLayout layouts[VK_GSR2_SLOTS];
    for (unsigned i = 0; i < VK_GSR2_SLOTS; i++) layouts[i] = pass->set_layout;
    VkDescriptorSetAllocateInfo alloc = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL,
        ctx->descriptors, VK_GSR2_SLOTS, layouts};
    if (vkAllocateDescriptorSets(V.dev, &alloc, pass->sets)) return 0;
    VkShaderModuleCreateInfo shader = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, spec->code_size, spec->code};
    VkShaderModule program;
    if (vkCreateShaderModule(V.dev, &shader, NULL, &program)) return 0;
    VkResult result;
    if (ctx->fragment) result = gsr2_fragment_pipeline(pass->layout, vertex, program, &pass->pipeline);
    else {
        VkComputePipelineCreateInfo create = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, NULL, 0,
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, stage, program, "main"}, pass->layout};
        result = vkCreateComputePipelines(V.dev, V.pcache, 1, &create, NULL, &pass->pipeline);
    }
    vkDestroyShaderModule(V.dev, program, NULL);
    return result == VK_SUCCESS;
}

static int gsr2_pipelines(vk_gsr2 *ctx) {
    VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 * VK_GSR2_SLOTS},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10 * VK_GSR2_SLOTS},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 6 * VK_GSR2_SLOTS},
    };
    VkDescriptorPoolCreateInfo pool = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0,
        ctx->pass_count * VK_GSR2_SLOTS, sizeof sizes / sizeof sizes[0], sizes};
    if (vkCreateDescriptorPool(V.dev, &pool, NULL, &ctx->descriptors)) return 0;

    VkShaderModule vertex = VK_NULL_HANDLE;
    if (ctx->fragment) {
        VkShaderModuleCreateInfo code = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0,
            sizeof sgsr2_fullscreen_vert_spv, sgsr2_fullscreen_vert_spv};
        if (vkCreateShaderModule(V.dev, &code, NULL, &vertex)) return 0;
    }
    int ready = 1;
    for (unsigned i = 0; i < ctx->pass_count && ready; i++) ready = gsr2_pass(ctx, i, vertex);
    if (vertex) vkDestroyShaderModule(V.dev, vertex, NULL);
    if (ready) vk_pipeline_cache_changed();
    return ready;
}

vk_gsr2 *vk_gsr2_create(uint32_t variant, VkExtent2D render, VkExtent2D output, VkDeviceSize budget) {
    if (!render.width || !render.height || render.width > output.width || render.height > output.height ||
        (variant != DM_TEMPORAL_2FS && variant != DM_TEMPORAL_2CS && variant != DM_TEMPORAL_3CS) ||
        !(vk_gsr2_variants() & variant)) return NULL;
    vk_gsr2 *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;
    ctx->variant = variant;
    ctx->render = render;
    ctx->output = output;
    const VkDeviceSize limit = 96u * 1024 * 1024;
    ctx->budget = budget < limit ? budget : limit;
    ctx->fragment = variant == DM_TEMPORAL_2FS;
    ctx->pass_count = variant == DM_TEMPORAL_3CS ? 3 : 2;
    ctx->spec = ctx->fragment ? passes_2fs : variant == DM_TEMPORAL_2CS ? passes_2cs : passes_3cs;
    if (!gsr2_images(ctx) || !gsr2_uniform(ctx) || !gsr2_pipelines(ctx)) goto fail;
    for (unsigned i = 0; i < 2; i++) {
        VkFilter filter = i ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        VkSamplerCreateInfo create = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, NULL, 0, filter, filter,
            VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
        if (vkCreateSampler(V.dev, &create, NULL, &ctx->samplers[i])) goto fail;
    }
    return ctx;
fail:
    vk_gsr2_destroy(ctx);
    return NULL;
}

void vk_gsr2_destroy(vk_gsr2 *ctx) {
    if (!ctx) return;
    if (ctx->descriptors) vkDestroyDescriptorPool(V.dev, ctx->descriptors, NULL);
    for (unsigned i = 0; i < ctx->pass_count; i++) {
        temporal_pass *pass = &ctx->passes[i];
        if (pass->pipeline) vkDestroyPipeline(V.dev, pass->pipeline, NULL);
        if (pass->layout) vkDestroyPipelineLayout(V.dev, pass->layout, NULL);
        if (pass->set_layout) vkDestroyDescriptorSetLayout(V.dev, pass->set_layout, NULL);
    }
    for (unsigned i = 0; i < G2_IMAGES; i++) {
        temporal_image *image = &ctx->images[i];
        if (image->view) vkDestroyImageView(V.dev, image->view, NULL);
        if (image->image) vkDestroyImage(V.dev, image->image, NULL);
        if (image->memory) vkFreeMemory(V.dev, image->memory, NULL);
    }
    for (unsigned i = 0; i < 2; i++) if (ctx->samplers[i]) vkDestroySampler(V.dev, ctx->samplers[i], NULL);
    if (ctx->ubo_mapped) vkUnmapMemory(V.dev, ctx->ubo_memory);
    if (ctx->ubo) vkDestroyBuffer(V.dev, ctx->ubo, NULL);
    if (ctx->ubo_memory) vkFreeMemory(V.dev, ctx->ubo_memory, NULL);
    free(ctx);
}

static int gsr2_sampleable(vk_gsr2_input input) {
    return input.view && (input.layout == VK_IMAGE_LAYOUT_GENERAL ||
                          input.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
                          input.layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL ||
                          input.layout == VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL ||
                          input.layout == VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
}

static int gsr2_inputs_valid(const vk_gsr2_inputs *in, int opaque) {
    if (!in || !gsr2_sampleable(in->color) || !gsr2_sampleable(in->depth) ||
        !gsr2_sampleable(in->motion) || (opaque && !gsr2_sampleable(in->opaque))) return 0;
    for (unsigned i = 0; i < 16; i++) if (!isfinite(in->clip_to_prev[i])) return 0;
    return fabsf(in->jitter[0]) <= 0.5001f && fabsf(in->jitter[1]) <= 0.5001f &&
           isfinite(in->pre_exposure) && in->pre_exposure > 0 &&
           isfinite(in->tan_half_fov_x) && in->tan_half_fov_x > 0 &&
           isfinite(in->near_plane) && in->near_plane > 0 &&
           isfinite(in->min_lerp) && in->min_lerp >= 0 && in->min_lerp <= 1;
}

static void gsr2_params(vk_gsr2 *ctx, unsigned slot, const vk_gsr2_inputs *in, dm_temporal_plan plan) {
    float iw = (float)ctx->render.width, ih = (float)ctx->render.height;
    float ow = (float)ctx->output.width, oh = (float)ctx->output.height;
    char *dst = (char *)ctx->ubo_mapped + slot * ctx->ubo_stride;
    if (ctx->fragment) {
        fragment_params p = {.render = {iw, ih}, .output = {ow, oh},
            .render_rcp = {1 / iw, 1 / ih}, .output_rcp = {1 / ow, 1 / oh},
            .jitter = {in->jitter[0], in->jitter[1]},
            .scale_ratio = {fminf(ow / iw, 1.99f), fminf(20.0f, powf((ow / iw) * (oh / ih), 3.0f))},
            .tan_half_fov_x = in->tan_half_fov_x, .min_lerp = in->min_lerp,
            .reset = (float)plan.reset, .same_camera = in->same_camera != 0};
        memcpy(p.clip_to_prev, in->clip_to_prev, sizeof p.clip_to_prev);
        memcpy(dst, &p, sizeof p);
        return;
    }
    compute_params p = {.render = {ctx->render.width, ctx->render.height},
        .output = {ctx->output.width, ctx->output.height},
        .render_rcp = {1 / iw, 1 / ih}, .output_rcp = {1 / ow, 1 / oh},
        .jitter = {in->jitter[0], in->jitter[1]}, .exposure = in->pre_exposure,
        .tan_half_fov_x = in->tan_half_fov_x, .near_plane = in->near_plane,
        .min_lerp = in->min_lerp, .same_camera = in->same_camera != 0, .reset = plan.reset != 0};
    memcpy(p.clip_to_prev, in->clip_to_prev, sizeof p.clip_to_prev);
    memcpy(dst, &p, sizeof p);
}

static unsigned gsr2_image_index(const vk_gsr2 *ctx, unsigned image) {
    switch (image) {
        case G2_PREV: return G2_HISTORY0 + (ctx->next ^ 1u);
        case G2_NEXT: return G2_HISTORY0 + ctx->next;
        case G2_PREV_LUMA: return G2_LUMA0 + (ctx->next ^ 1u);
        case G2_NEXT_LUMA: return G2_LUMA0 + ctx->next;
        default: return image;
    }
}

static void gsr2_descriptors(vk_gsr2 *ctx, unsigned slot, unsigned pass, const vk_gsr2_inputs *input) {
    const pass_spec *spec = &ctx->spec[pass];
    VkDescriptorSet set = ctx->passes[pass].sets[slot];
    VkDescriptorImageInfo images[G2_BINDINGS_MAX];
    VkDescriptorBufferInfo ubo = {ctx->ubo, slot * ctx->ubo_stride,
        ctx->fragment ? sizeof(fragment_params) : sizeof(compute_params)};
    VkWriteDescriptorSet writes[G2_BINDINGS_MAX];
    const vk_gsr2_input external[] = {input->color, input->depth, input->motion, input->opaque};
    for (unsigned i = 0; i < spec->count; i++) {
        const pass_binding *b = &spec->binding[i];
        writes[i] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL,
            set, b->binding, 0, 1, b->type};
        if (b->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
            writes[i].pBufferInfo = &ubo;
        } else {
            unsigned image = gsr2_image_index(ctx, b->image);
            vk_gsr2_input read = image < G2_IMAGES
                ? (vk_gsr2_input){ctx->images[image].view, VK_IMAGE_LAYOUT_GENERAL}
                : external[image - G2_IMAGES];
            VkSampler sampler = b->type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ? VK_NULL_HANDLE : ctx->samplers[b->linear];
            images[i] = (VkDescriptorImageInfo){sampler, read.view, read.layout};
            writes[i].pImageInfo = &images[i];
        }
    }
    vkUpdateDescriptorSets(V.dev, spec->count, writes, 0, NULL);
}

static void gsr2_prepare_images(vk_gsr2 *ctx, VkCommandBuffer cb, int reset) {
    if (ctx->initialized && !reset) return;
    VkClearColorValue black = {{0}};
    for (unsigned i = 0; i < G2_IMAGES; i++) {
        VkImage image = ctx->images[i].image;
        if (!image) continue;
        if (!ctx->initialized) {
            VkImageMemoryBarrier transition = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, NULL, 0,
                VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image, temporal_color};
            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                0, 0, NULL, 0, NULL, 1, &transition);
        }
        if (reset && (i == gsr2_image_index(ctx, G2_PREV) || i == gsr2_image_index(ctx, G2_PREV_LUMA)))
            vkCmdClearColorImage(cb, image, VK_IMAGE_LAYOUT_GENERAL, &black, 1, &temporal_color);
    }
    vk_img_barrier_all(cb);
}

static void gsr2_dispatch(vk_gsr2 *ctx, VkCommandBuffer cb, unsigned slot, unsigned index) {
    temporal_pass *pass = &ctx->passes[index];
    unsigned target = gsr2_image_index(ctx, ctx->spec[index].target);
    temporal_image *image = &ctx->images[target];
    VkExtent2D ext = image->extent;
    VkPipelineBindPoint point = ctx->fragment ? VK_PIPELINE_BIND_POINT_GRAPHICS : VK_PIPELINE_BIND_POINT_COMPUTE;
    if (ctx->fragment) {
        VkRenderingAttachmentInfo color = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, NULL, image->view,
            VK_IMAGE_LAYOUT_GENERAL, 0, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE};
        VkRenderingInfo rendering = {VK_STRUCTURE_TYPE_RENDERING_INFO, NULL, 0,
            {{0, 0}, ext}, 1, 0, 1, &color};
        vkCmdBeginRendering(cb, &rendering);
    }
    vkCmdBindPipeline(cb, point, pass->pipeline);
    vkCmdBindDescriptorSets(cb, point, pass->layout, 0, 1, &pass->sets[slot], 0, NULL);
    if (ctx->fragment) {
        VkViewport vp = {0, 0, (float)ext.width, (float)ext.height, 0, 1};
        VkRect2D scissor = {{0, 0}, ext};
        vkCmdSetViewport(cb, 0, 1, &vp);
        vkCmdSetScissor(cb, 0, 1, &scissor);
        vkCmdDraw(cb, 3, 1, 0, 0);
        vkCmdEndRendering(cb);
    } else {
        vkCmdDispatch(cb, (ext.width + 7) / 8, (ext.height + 7) / 8, 1);
    }
}

int vk_gsr2_record(vk_gsr2 *ctx, VkCommandBuffer cb, unsigned slot,
                   const dm_temporal_frame *frame, const vk_gsr2_inputs *input,
                   const vk_gsr2_timing *timing, vk_gsr2_output *output) {
    if (output) *output = (vk_gsr2_output){0};
    if (!ctx || !frame || !cb || !output || slot >= VK_GSR2_SLOTS || ctx->pending) return 0;
    dm_temporal_frame f = *frame;
    f.variants &= ctx->variant;
    if (f.iw != ctx->render.width || f.ih != ctx->render.height ||
        f.ow != ctx->output.width || f.oh != ctx->output.height ||
        !gsr2_inputs_valid(input, ctx->variant == DM_TEMPORAL_3CS)) f.inputs = 0;
    dm_temporal_plan plan = dm_temporal_choose(&ctx->history, &f, ctx->variant);
    if (!plan.variant) {
        dm_temporal_commit(&ctx->history, &f, plan, 0);
        return 0;
    }

    // The fragment shader has no exposure uniform. Start fresh after a change
    // instead of accumulating history that was rendered at another exposure.
    if (ctx->fragment && fabsf(input->pre_exposure - 1.0f) > 1e-4f) plan.reset = 1;
    gsr2_params(ctx, slot, input, plan);
    vk_img_barrier_all(cb);
    gsr2_prepare_images(ctx, cb, plan.reset);
    if (timing && timing->pool) vkCmdResetQueryPool(cb, timing->pool, timing->first, 2 * ctx->pass_count);
    for (unsigned i = 0; i < ctx->pass_count; i++) {
        gsr2_descriptors(ctx, slot, i, input);
        if (timing && timing->pool)
            vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timing->pool, timing->first + 2 * i);
        gsr2_dispatch(ctx, cb, slot, i);
        if (timing && timing->pool)
            vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timing->pool, timing->first + 2 * i + 1);
        vk_img_barrier_all(cb);
    }

    const temporal_image *result = &ctx->images[ctx->fragment ? G2_HISTORY0 + ctx->next : G2_RESULT];
    *output = (vk_gsr2_output){result->image, result->view, VK_IMAGE_LAYOUT_GENERAL, result->extent};
    ctx->pending_frame = f;
    ctx->pending_plan = plan;
    ctx->pending = 1;
    return 1;
}

void vk_gsr2_commit(vk_gsr2 *ctx, int submitted) {
    if (!ctx || !ctx->pending) return;
    dm_temporal_commit(&ctx->history, &ctx->pending_frame, ctx->pending_plan, submitted);
    if (submitted) {
        ctx->initialized = 1;
        ctx->next ^= 1;
    }
    ctx->pending = 0;
}
