// Present-thread-owned spatial reconstruction. All work uses the graphics queue.
int vk_pass_dump_user_active(void);

#if !defined(SPATIAL_HOST_PROBE) || defined(SGSR2_HOST_PROBE)
#include "gfx_vk_gsr2.h"
#define SP_TEMPORAL 1
#else
#define SP_TEMPORAL 0
#endif

enum { SP_EASU, SP_RCAS, SP_GSR1, SP_VIVID, SP_PASS_COUNT };
enum { SP_SOURCE, SP_INTERMEDIATE, SP_DISPLAY, SP_OUTPUT, SP_FLOW0, SP_FLOW1, SP_IMAGE_COUNT };
enum { SP_SLOTS = 8, SP_MAX_PIXELS = 3840 * 1080 };

typedef struct {
    vkimg_t texture;
    VkImageView view;
    VkImageUsageFlags usage;
    VkFormat view_fmt;
    VkFence last_use;
} spatial_image;

static struct {
    VkDescriptorSetLayout dsl;
    VkPipelineLayout layout;
    VkDescriptorPool pool;
    VkDescriptorSet set[3], direct_set[SP_SLOTS];
    VkSampler sampler;
    VkPipeline pipeline[SP_PASS_COUNT];
    VkFormat fmt;
    unsigned failed_passes;
    int setup_failed;
    spatial_image image[SP_IMAGE_COUNT];
    VkCommandPool commands;
    VkCommandBuffer cb[SP_SLOTS];
    VkFence fence[SP_SLOTS];
    vk_rs_variant *source[SP_SLOTS];
    unsigned live_sources;
    VkQueryPool queries;
    uint32_t timestamp_bits;
    unsigned slot, encoding;
    float flow_budget_ms;
    uint64_t ticket, retry_after, measured_ticket;
    uint64_t query_ticket[SP_SLOTS];
    double gpu_ms;
    int submit_failed, report_physical;
    VkExtent2D report_input;
} g_spatial;

#if SP_TEMPORAL
_Static_assert(SP_SLOTS == VK_GSR2_SLOTS, "shared reconstruction fence ring");
static struct {
    vk_gsr2 *ctx;
    VkFence fence;
    VkFormat format, source_format;
    VkExtent2D render, output, candidate_render, candidate_output;
    unsigned stable_frames;
    int crop[4], flip_y, physical;
    unsigned next, slow_frames;
    uint64_t ticket, retry_after, last_source_ns;
    float budget_ms[SP_SLOTS];
} g_flow;
#endif

static void spatial_image_drop(spatial_image *s) {
    if (s->view) vkDestroyImageView(V.dev, s->view, NULL);
    if (s->texture.img) vkDestroyImage(V.dev, s->texture.img, NULL);
    if (s->texture.mem) vkFreeMemory(V.dev, s->texture.mem, NULL);
    *s = (spatial_image){0};
}

static void spatial_forget_fence(VkFence fence) {
#if SP_TEMPORAL
    if (g_flow.fence == fence) g_flow.fence = VK_NULL_HANDLE;
#endif
    for (int i = 0; i < SP_IMAGE_COUNT; i++)
        if (g_spatial.image[i].last_use == fence) g_spatial.image[i].last_use = VK_NULL_HANDLE;
}

static void spatial_release_source(unsigned slot) {
    vk_rs_variant_destroy(g_spatial.source[slot]);
    g_spatial.source[slot] = NULL;
    g_spatial.live_sources &= ~(1u << slot);
}

static void spatial_collect_sources(void) {
    for (unsigned mask = g_spatial.live_sources; mask; mask &= mask - 1) {
        unsigned slot = (unsigned)__builtin_ctz(mask);
        if (vkWaitForFences(V.dev, 1, &g_spatial.fence[slot], VK_TRUE, 0) == VK_SUCCESS)
            spatial_release_source(slot);
    }
}

static void spatial_drop_images(void) {
#if SP_TEMPORAL
    if (g_flow.fence && vkWaitForFences(V.dev, 1, &g_flow.fence, VK_TRUE, UINT64_MAX)) return;
    vk_gsr2_destroy(g_flow.ctx);
    memset(&g_flow, 0, sizeof g_flow);
#endif
    for (unsigned i = 0; i < SP_SLOTS; i++) spatial_release_source(i);
    for (int i = 0; i < SP_IMAGE_COUNT; i++) spatial_image_drop(&g_spatial.image[i]);
    g_spatial.ticket = g_spatial.retry_after = 0;
}

static VkFormat spatial_unorm_format(VkFormat fmt) {
    switch (fmt) {
        case VK_FORMAT_R8G8B8A8_SRGB: return VK_FORMAT_R8G8B8A8_UNORM;
        case VK_FORMAT_B8G8R8A8_SRGB: return VK_FORMAT_B8G8R8A8_UNORM;
        case VK_FORMAT_A8B8G8R8_SRGB_PACK32: return VK_FORMAT_A8B8G8R8_UNORM_PACK32;
        default: return fmt;
    }
}

static int spatial_pipeline_ready(int pass) {
    if (!V.has_dynrender || g_spatial.setup_failed) return 0;
    VkResult r = VK_SUCCESS;
    if (!g_spatial.dsl) {
        VkDescriptorSetLayoutBinding binding = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL};
        VkDescriptorSetLayoutCreateInfo dc = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, 1, &binding};
        r = vkCreateDescriptorSetLayout(V.dev, &dc, NULL, &g_spatial.dsl);
        VkPushConstantRange pc = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, 32};
        VkPipelineLayoutCreateInfo lc = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 1, &g_spatial.dsl, 1, &pc};
        if (!r) r = vkCreatePipelineLayout(V.dev, &lc, NULL, &g_spatial.layout);
        VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 + SP_SLOTS};
        VkDescriptorPoolCreateInfo pool = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, 3 + SP_SLOTS, 1, &ps};
        if (!r) r = vkCreateDescriptorPool(V.dev, &pool, NULL, &g_spatial.pool);
        VkDescriptorSetLayout ls[3] = {g_spatial.dsl, g_spatial.dsl, g_spatial.dsl};
        VkDescriptorSetAllocateInfo ai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, g_spatial.pool, 3, ls};
        if (!r) r = vkAllocateDescriptorSets(V.dev, &ai, g_spatial.set);
        VkSamplerCreateInfo si = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, NULL, 0, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
        if (!r) r = vkCreateSampler(V.dev, &si, NULL, &g_spatial.sampler);
    }
    if (r) {
        g_spatial.setup_failed = 1;
        return 0;
    }
    if (g_spatial.fmt != g_sfmt) {
        // A swapchain format change retires its pending submissions before this point.
        for (int i = 0; i < SP_PASS_COUNT; i++) {
            if (g_spatial.pipeline[i]) vkDestroyPipeline(V.dev, g_spatial.pipeline[i], NULL);
            g_spatial.pipeline[i] = VK_NULL_HANDLE;
        }
        g_spatial.fmt = g_sfmt;
        g_spatial.failed_passes = 0;
    }
    if (g_spatial.pipeline[pass]) return 1;
    if (g_spatial.failed_passes & (1u << pass)) return 0;
    static const uint32_t *const words[] = {
        fsr_easu_frag_spv, fsr_rcas_frag_spv, gsr1_frag_spv, vivid_frag_spv
    };
    static const size_t sizes[] = {
        sizeof fsr_easu_frag_spv, sizeof fsr_rcas_frag_spv, sizeof gsr1_frag_spv, sizeof vivid_frag_spv
    };
    VkShaderModule module[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkShaderModuleCreateInfo vert = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, sizeof fsr_vert_spv, fsr_vert_spv};
    VkShaderModuleCreateInfo frag = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, sizes[pass], words[pass]};
    r = vkCreateShaderModule(V.dev, &vert, NULL, &module[0]);
    if (!r) r = vkCreateShaderModule(V.dev, &frag, NULL, &module[1]);
    if (!r) {
        VkPipelineShaderStageCreateInfo ss[2] = {
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, module[0], "main", NULL},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, module[1], "main", NULL}};
        VkPipelineVertexInputStateCreateInfo vi = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, NULL, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0};
        VkPipelineViewportStateCreateInfo vs = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, NULL, 0, 1, NULL, 1, NULL};
        VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, NULL, 0, 0, 0, VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0, 0, 0, 0, 1};
        VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, NULL, 0, VK_SAMPLE_COUNT_1_BIT};
        VkPipelineColorBlendAttachmentState ba = {0, 0, 0, 0, 0, 0, 0, 0xf};
        VkPipelineColorBlendStateCreateInfo blend = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, NULL, 0, 0, 0, 1, &ba};
        VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, NULL, 0, 2, states};
        VkFormat attachment = pass == SP_VIVID ? g_sfmt : spatial_unorm_format(g_sfmt);
        VkPipelineRenderingCreateInfo rendering = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, NULL, 0, 1, &attachment};
        VkGraphicsPipelineCreateInfo gc = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &rendering, 0, 2, ss,
            &vi, &ia, NULL, &vs, &rs, &ms, NULL, &blend, &dynamic, g_spatial.layout, VK_NULL_HANDLE};
        r = vkCreateGraphicsPipelines(V.dev, V.pcache, 1, &gc, NULL, &g_spatial.pipeline[pass]);
    }
    for (int i = 0; i < 2; i++)
        if (module[i]) vkDestroyShaderModule(V.dev, module[i], NULL);
    if (r) {
        g_spatial.failed_passes |= 1u << pass;
        VKLOG("[upscale] raster pipeline %d unavailable (%d)", pass, r);
        return 0;
    }
    vk_pipeline_cache_changed();
    return 1;
}

static VkFormatFeatureFlags spatial_features(VkFormat fmt) {
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(V.pd, fmt, &props);
    return props.optimalTilingFeatures;
}

static int spatial_image_ready(int index, uint32_t w, uint32_t h, VkImageUsageFlags usage, VkFormat view_fmt) {
    spatial_image *s = &g_spatial.image[index];
    if (s->texture.img && s->texture.w == w && s->texture.h == h &&
        s->texture.fmt == g_sfmt && s->view_fmt == view_fmt && (s->usage & usage) == usage) return 1;
    if (!w || !h || (uint64_t)w * h > SP_MAX_PIXELS) return 0;

    // Descriptor writes and image retirement wait only for the last user of this
    // particular allocation, on resize or a backend change. In steady state GPU
    // barriers order all reads and writes, with immutable descriptors.
    if (s->last_use && vkWaitForFences(V.dev, 1, &s->last_use, VK_TRUE, UINT64_MAX)) return 0;
    spatial_image_drop(s);
    vkimg_t *t = &s->texture;
    VkImageCreateFlags flags = view_fmt != g_sfmt ? VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT : 0;
    VkImageCreateInfo ic = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, flags, VK_IMAGE_TYPE_2D, g_sfmt,
        {w, h, 1}, 1, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, usage,
        VK_SHARING_MODE_EXCLUSIVE, 0, NULL, VK_IMAGE_LAYOUT_UNDEFINED};
    VkResult r = vkCreateImage(V.dev, &ic, NULL, &t->img);
    if (!r) {
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(V.dev, t->img, &req);
        uint32_t type = vk_mem_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
        if (type == UINT32_MAX) r = VK_ERROR_OUT_OF_DEVICE_MEMORY;
        else {
            VkMemoryAllocateInfo ma = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, req.size, type};
            r = vkAllocateMemory(V.dev, &ma, NULL, &t->mem);
        }
    }
    if (!r) r = vkBindImageMemory(V.dev, t->img, t->mem, 0);
    if (!r && (usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT))) {
        VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, t->img,
            VK_IMAGE_VIEW_TYPE_2D, view_fmt, {0}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        r = vkCreateImageView(V.dev, &vi, NULL, &s->view);
    }
    if (r) {
        spatial_image_drop(s);
        VKLOG("[upscale] image %ux%u (usage %#x) unavailable: %d", w, h, usage, r);
        return 0;
    }
    t->fmt = g_sfmt; t->w = w; t->h = h;
    t->samples = t->layers = t->levels = t->d = 1;
    t->aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    t->layout = VK_IMAGE_LAYOUT_GENERAL;
    s->usage = usage;
    s->view_fmt = view_fmt;
    if (index < SP_OUTPUT && (usage & VK_IMAGE_USAGE_SAMPLED_BIT)) {
        VkDescriptorImageInfo info = {g_spatial.sampler, s->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, g_spatial.set[index],
            0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &info};
        vkUpdateDescriptorSets(V.dev, 1, &write, 0, NULL);
    }
    return 1;
}

typedef struct { float sizes[4], origin[2], sharpness, padding; } spatial_constants;
static void spatial_draw(VkCommandBuffer cb, int pass, VkDescriptorSet set, VkImageView view,
                     int x, int y, uint32_t w, uint32_t h, spatial_constants c) {
    VkRenderingAttachmentInfo ca = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, NULL, view,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, NULL, VK_IMAGE_LAYOUT_UNDEFINED,
        pass == SP_VIVID ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        VK_ATTACHMENT_STORE_OP_STORE};
    VkRenderingInfo ri = {VK_STRUCTURE_TYPE_RENDERING_INFO, NULL, 0,
        {{x, y}, {w, h}}, 1, 0, 1, &ca, NULL, NULL};
    vkCmdBeginRendering(cb, &ri);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_spatial.pipeline[pass]);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_spatial.layout, 0, 1, &set, 0, NULL);
    vkCmdPushConstants(cb, g_spatial.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof c, &c);
    VkViewport vp = {(float)x, (float)y, (float)w, (float)h, 0, 1};
    VkRect2D sc = {{x, y}, {w, h}};
    vkCmdSetViewport(cb, 0, 1, &vp);
    vkCmdSetScissor(cb, 0, 1, &sc);
    vkCmdDraw(cb, 3, 1, 0, 0);
    vkCmdEndRendering(cb);
}

static void spatial_blit(VkCommandBuffer cb, VkImage src, VkImage dst, const int rect[4],
                         uint32_t w, uint32_t h, int flip_y, VkFilter filter) {
    VkImageBlit blit = {
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        {{rect[0], rect[1], 0}, {rect[0] + rect[2], rect[1] + rect[3], 1}},
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {{0, 0, 0}, {(int)w, (int)h, 1}}
    };
    if (flip_y) {
        blit.srcOffsets[0].y = blit.srcOffsets[1].y;
        blit.srcOffsets[1].y = rect[1];
    }
    vkCmdBlitImage(cb, src, VK_IMAGE_LAYOUT_GENERAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, filter);
}

static int spatial_commands_ready(void) {
    if (g_spatial.submit_failed) return 0;
    if (g_spatial.commands) return 1;
    VkCommandPoolCreateInfo pool = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL,
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, V.qf};
    VkResult r = vkCreateCommandPool(V.dev, &pool, NULL, &g_spatial.commands);
    VkCommandBufferAllocateInfo alloc = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL,
        g_spatial.commands, VK_COMMAND_BUFFER_LEVEL_PRIMARY, SP_SLOTS};
    if (!r) r = vkAllocateCommandBuffers(V.dev, &alloc, g_spatial.cb);
    VkFenceCreateInfo fence = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, NULL, VK_FENCE_CREATE_SIGNALED_BIT};
    for (int i = 0; i < SP_SLOTS && !r; i++) r = vkCreateFence(V.dev, &fence, NULL, &g_spatial.fence[i]);
    if (r) {
        g_spatial.submit_failed = 1;
        return 0;
    }

    uint32_t families = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(V.pd, &families, NULL);
    if (V.qf < families) {
        VkQueueFamilyProperties *props = calloc(families, sizeof(*props));
        if (props) {
            vkGetPhysicalDeviceQueueFamilyProperties(V.pd, &families, props);
            if (V.qf < families) g_spatial.timestamp_bits = props[V.qf].timestampValidBits;
            free(props);
        }
    }
    VkQueryPoolCreateInfo queries = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, NULL, 0,
        VK_QUERY_TYPE_TIMESTAMP, 2 * SP_SLOTS};
    if (g_spatial.timestamp_bits &&
        vkCreateQueryPool(V.dev, &queries, NULL, &g_spatial.queries)) g_spatial.queries = VK_NULL_HANDLE;
    return 1;
}

static void spatial_read_timing(unsigned slot) {
    uint64_t stamp[2];
    if (!g_spatial.queries || !g_spatial.query_ticket[slot]) return;
    if (vkGetQueryPoolResults(V.dev, g_spatial.queries, 2 * slot, 2, sizeof stamp, stamp,
                              sizeof stamp[0], VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
        uint32_t bits = g_spatial.timestamp_bits;
        uint64_t mask = bits >= 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1;
        g_spatial.gpu_ms = ((stamp[1] - stamp[0]) & mask) * (double)V.props.limits.timestampPeriod / 1e6;
        g_spatial.measured_ticket = g_spatial.query_ticket[slot];
#if SP_TEMPORAL
        if (g_flow.budget_ms[slot] > 0) {
            if (g_spatial.gpu_ms <= g_flow.budget_ms[slot]) g_flow.slow_frames = 0;
            else if (++g_flow.slow_frames == 3) {
                g_flow.retry_after = g_spatial.measured_ticket + 120;
                g_flow.slow_frames = 0;
            }
        }
#endif
    }
}

static int spatial_active, spatial_requested = -1;
static const char *spatial_reason;

static void spatial_report(int requested, int active, const char *reason, uint64_t ticket) {
    int changed = requested != spatial_requested || active != spatial_active || reason != spatial_reason;
    spatial_requested = requested;
    spatial_active = active;
    spatial_reason = reason;
    if (changed || (ticket % 120 == 0 && vk_pass_dump_user_active())) {
        if (g_spatial.measured_ticket)
            VKLOG("[upscale] real frame %llu, requested %s, active %s, %s, GPU %.3f ms (frame %llu)",
                  (unsigned long long)ticket, spatial_mode_name(requested), spatial_mode_name(active),
                  reason ? reason : "ready", g_spatial.gpu_ms, (unsigned long long)g_spatial.measured_ticket);
        else
            VKLOG("[upscale] real frame %llu, requested %s, active %s, %s",
                  (unsigned long long)ticket, spatial_mode_name(requested), spatial_mode_name(active),
                  reason ? reason : "ready");
    }
}

#if SP_TEMPORAL
static void spatial_flow_release(void) {
    if (g_flow.fence && vkWaitForFences(V.dev, 1, &g_flow.fence, VK_TRUE, 0) != VK_SUCCESS)
        return;
    vk_gsr2_destroy(g_flow.ctx);
    g_flow.ctx = NULL;
    g_flow.fence = VK_NULL_HANDLE;
    g_flow.ticket = 0;
    for (unsigned i = SP_FLOW0; i <= SP_FLOW1; ++i) spatial_image_drop(&g_spatial.image[i]);
}

static int spatial_flow_ready(uint32_t iw, uint32_t ih, uint32_t w, uint32_t h,
                              VkFormat format, const int crop[4], int flip_y,
                              int physical, uint64_t ticket) {
    if (ticket < g_flow.retry_after || iw > w || ih > h || iw < 16 || ih < 16 ||
        (uint64_t)iw * ih > 1920u * 1080u || (uint64_t)w * h > SP_MAX_PIXELS) return 0;
    if (g_flow.candidate_render.width != iw || g_flow.candidate_render.height != ih ||
        g_flow.candidate_output.width != w || g_flow.candidate_output.height != h) {
        g_flow.candidate_render = (VkExtent2D){iw, ih};
        g_flow.candidate_output = (VkExtent2D){w, h};
        g_flow.stable_frames = 0;
    }
    if (g_flow.stable_frames < 3) g_flow.stable_frames++;
    if (g_flow.ctx && (g_flow.render.width != iw || g_flow.render.height != ih ||
                      g_flow.output.width != w || g_flow.output.height != h ||
                      g_flow.format != g_sfmt)) {
        // Reallocating during an adaptive transition must not wait behind
        // an in-flight frame. The spatial path handles this frame instead.
        if (g_flow.stable_frames < 3) return 0;
        spatial_flow_release();
        if (g_flow.ctx) return 0;
    }
    if (!g_flow.ctx) {
        VkFormatFeatureFlags color = spatial_features(g_sfmt);
        const VkFormatFeatureFlags required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
        if ((color & required) != required ||
            !(spatial_features(VK_FORMAT_R16G16B16A16_SFLOAT) & VK_FORMAT_FEATURE_BLIT_SRC_BIT))
            goto unavailable;
        g_flow.render = (VkExtent2D){iw, ih};
        g_flow.output = (VkExtent2D){w, h};
        g_flow.format = g_sfmt;
        g_flow.ctx = vk_gsr2_create_image(g_flow.render, g_flow.output, 48u * 1024u * 1024u);
        if (!g_flow.ctx) goto unavailable;
    }
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (!spatial_image_ready(SP_FLOW0, iw, ih, usage, g_sfmt) ||
        !spatial_image_ready(SP_FLOW1, iw, ih, usage, g_sfmt)) goto unavailable;
    if (g_flow.source_format != format || g_flow.flip_y != flip_y ||
        g_flow.physical != physical || memcmp(g_flow.crop, crop, sizeof g_flow.crop))
        g_flow.ticket = 0;
    g_flow.source_format = format;
    g_flow.flip_y = flip_y;
    g_flow.physical = physical;
    memcpy(g_flow.crop, crop, sizeof g_flow.crop);
    return 1;

unavailable:
    spatial_flow_release();
    g_flow.retry_after = ticket + 120;
    return 0;
}

static int spatial_flow_record(VkCommandBuffer cb, unsigned slot, VkImage image,
                               const int crop[4], int flip_y, uint64_t ticket,
                               spatial_image *output) {
    spatial_image *current = &g_spatial.image[SP_FLOW0 + g_flow.next];
    const int cut = !g_flow.ticket || ticket - g_flow.ticket != 1;
    spatial_image *previous = cut ? current : &g_spatial.image[SP_FLOW0 + (g_flow.next ^ 1)];
    barrier(cb, current->texture.img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    spatial_blit(cb, image, current->texture.img, crop, g_flow.render.width, g_flow.render.height,
                 flip_y, VK_FILTER_NEAREST);
    barrier(cb, current->texture.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    dm_temporal_frame frame = {.ticket = ticket, .stream = 1, .epoch = 1,
        .iw = g_flow.render.width, .ih = g_flow.render.height,
        .ow = g_flow.output.width, .oh = g_flow.output.height,
        .inputs = DM_TEMPORAL_IMAGE_PAIR, .variants = DM_TEMPORAL_2FS,
        .real = 1, .memory_ok = 1, .cut = cut};
    vk_gsr2_output result;
    if (!vk_gsr2_record_image(g_flow.ctx, cb, slot, &frame,
            (vk_gsr2_input){current->view, VK_IMAGE_LAYOUT_GENERAL},
            (vk_gsr2_input){previous->view, VK_IMAGE_LAYOUT_GENERAL}, &result)) return 0;
    int full[4] = {0, 0, (int)result.extent.width, (int)result.extent.height};
    barrier(cb, output->texture.img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    spatial_blit(cb, result.image, output->texture.img, full, result.extent.width, result.extent.height,
                 0, VK_FILTER_NEAREST);
    barrier(cb, output->texture.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    return 1;
}
#endif

// A nonzero ticket identifies a unique rendered game frame. Generated frames
// and overlay-only presents never enter this function. Cached output remains
// readable by LSFG and the presenter until the next reconstruction.
static spatial_image *spatial_reconstruct(vkimg_t *src, const int crop[4], int flip_y,
                                         uint32_t w, uint32_t h, uint64_t ticket,
                                         const vk_reconstruction_input *physical) {
    if (!ticket) return NULL;
    spatial_image *output = &g_spatial.image[SP_OUTPUT];
    if (g_spatial.ticket == ticket) return output;
    g_spatial.ticket = 0;
    g_spatial.report_input = (VkExtent2D){crop[2] > 0 ? (uint32_t)crop[2] : 0,
                                         crop[3] > 0 ? (uint32_t)crop[3] : 0};
    g_spatial.report_physical = 0;
    int requested = dm_spatial_mode();
    int wants_flow = requested == SP_SGSR2;
    int mode = wants_flow ? SP_SGSR1 : requested;
    const char *reason = NULL;
#if SP_TEMPORAL
    if (!wants_flow && g_flow.ctx) spatial_flow_release();
#endif
    if (mode == SP_NATIVE) {
        spatial_report(requested, SP_NATIVE, NULL, ticket);
        return NULL;
    }
    VkImage image = src->img;
    VkFormat format = src->fmt;
    VkImageLayout layout = src->layout;
    uint32_t sw = src->w, sh = src->h;
    int valid[4];
    if (physical && physical->backing && physical->ticket == ticket && physical->layout == VK_IMAGE_LAYOUT_GENERAL &&
        physical->logical.width == sw && physical->logical.height == sh &&
        physical->format == format) {
        image = physical->backing->image;
        sw = physical->physical.width;
        sh = physical->physical.height;
        valid[0] = physical->valid.offset.x;
        valid[1] = physical->valid.offset.y;
        valid[2] = (int)physical->valid.extent.width;
        valid[3] = (int)physical->valid.extent.height;
        crop = valid;
        flip_y = physical->flip_y;
        g_spatial.report_input = physical->valid.extent;
        g_spatial.report_physical = 1;
    } else physical = NULL;
    if (crop[2] <= 0 || crop[3] <= 0 || crop[0] < 0 || crop[1] < 0 ||
        (uint64_t)crop[0] + crop[2] > sw || (uint64_t)crop[1] + crop[3] > sh ||
        !w || !h || (w <= (uint32_t)crop[2] && h <= (uint32_t)crop[3])) {
        spatial_report(requested, SP_NATIVE, "picture fits or exceeds output", ticket);
        return NULL;
    }
    if (ticket < g_spatial.retry_after || !spatial_commands_ready()) {
        spatial_report(requested, SP_NATIVE, "reconstruction resources unavailable", ticket);
        return NULL;
    }

    VkFormat sample_fmt = spatial_unorm_format(g_sfmt);
    VkFormatFeatureFlags src_features = spatial_features(format);
    VkFormatFeatureFlags dst_features = spatial_features(g_sfmt);
    VkFormatFeatureFlags raster_features = spatial_features(sample_fmt);
    VkFormatFeatureFlags sampling = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    uint32_t iw = (uint32_t)crop[2], ih = (uint32_t)crop[3];
    int flow = 0;
#if SP_TEMPORAL
    flow = wants_flow && (g_spatial.queries || g_spatial.flow_budget_ms == 0.0f) &&
        (src_features & VK_FORMAT_FEATURE_BLIT_SRC_BIT) &&
        (dst_features & VK_FORMAT_FEATURE_BLIT_SRC_BIT) &&
        spatial_flow_ready(iw, ih, w, h, format, crop, flip_y, physical != NULL, ticket);
    if (flow) {
        mode = SP_SGSR2;
        reason = "image-driven motion";
    } else if (wants_flow) reason = "temporal reconstruction unavailable or over budget";
#endif
    float sharpness = dm_fsr_sharpness() / 100.0f;
    int rcas = mode == SP_FSR1 && sharpness > 0.001f;
    int first_pass = mode == SP_FSR1 ? SP_EASU : SP_GSR1;
    if (!flow && mode != SP_BILINEAR &&
        (iw < 16 || ih < 16 || (raster_features & sampling) != sampling ||
         !(raster_features & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) ||
         !spatial_pipeline_ready(first_pass) || (rcas && !spatial_pipeline_ready(SP_RCAS)))) {
        mode = SP_BILINEAR;
        reason = "spatial shader unavailable";
    }
    if (!(src_features & VK_FORMAT_FEATURE_BLIT_SRC_BIT) ||
        (dst_features & (VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT)) !=
        (VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT) ||
        (mode == SP_BILINEAR && !((crop[0] || crop[1] || iw != sw || ih != sh
            ? dst_features : src_features) & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))) {
        spatial_report(requested, SP_NATIVE, "image format cannot be reconstructed", ticket);
        return NULL;
    }

    spatial_image *stage = &g_spatial.image[SP_SOURCE];
    spatial_image *intermediate = &g_spatial.image[SP_INTERMEDIATE];
    int raster = !flow && mode != SP_BILINEAR;
    int isolated = crop[0] || crop[1] || iw != sw || ih != sh;
    unsigned slot = g_spatial.slot % SP_SLOTS;
    // A matching encoded view can feed the raster filters directly. Cropped,
    // flipped and incompatible encodings still use a physical-size staging
    // image to preserve clamp, orientation and color behavior.
    // Blitting to a different swapchain encoding would convert the values
    // before filtering. Direct sampling is equivalent only in the same encoding.
    int direct = raster && physical && !isolated && !flip_y &&
        format == g_sfmt && format == sample_fmt &&
        physical->sampled && (src_features & sampling) == sampling;
    VkDescriptorSet *source_set = &g_spatial.direct_set[slot];
    if (direct && !*source_set) {
        VkDescriptorSetAllocateInfo ai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL,
            g_spatial.pool, 1, &g_spatial.dsl};
        if (vkAllocateDescriptorSets(V.dev, &ai, source_set)) direct = 0;
    }
    int stage_needed = !flow && ((raster && !direct) || isolated);
    VkImageUsageFlags stage_usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        (raster ? VK_IMAGE_USAGE_SAMPLED_BIT : VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    rcas = raster && rcas;
    VkImageUsageFlags out_usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
        (raster ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    if (!spatial_image_ready(SP_OUTPUT, w, h, out_usage, raster ? sample_fmt : g_sfmt) ||
        (stage_needed && !spatial_image_ready(SP_SOURCE, iw, ih, stage_usage, raster ? sample_fmt : g_sfmt)) ||
        (rcas && !spatial_image_ready(SP_INTERMEDIATE, w, h, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                     VK_IMAGE_USAGE_SAMPLED_BIT, sample_fmt))) {
        g_spatial.retry_after = ticket + 120;
        spatial_report(requested, SP_NATIVE, "image allocation unavailable", ticket);
        return NULL;
    }

    g_spatial.slot++;
    VkFence fence = g_spatial.fence[slot];
    VkCommandBuffer cb = g_spatial.cb[slot];
    if (vkWaitForFences(V.dev, 1, &fence, VK_TRUE, UINT64_MAX)) goto submit_error;
    spatial_forget_fence(fence);
    spatial_release_source(slot);
    spatial_read_timing(slot);
    if (direct) {
        VkDescriptorImageInfo info = {g_spatial.sampler, physical->sampled, VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, *source_set,
            0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &info};
        vkUpdateDescriptorSets(V.dev, 1, &write, 0, NULL);
    }
    VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    if (vkBeginCommandBuffer(cb, &begin)) goto submit_error;
    vk_img_barrier_all(cb);
    if (g_spatial.queries) {
        vkCmdResetQueryPool(cb, g_spatial.queries, 2 * slot, 2);
        vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g_spatial.queries, 2 * slot);
    }
    if (layout == VK_IMAGE_LAYOUT_UNDEFINED)
        barrier(cb, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    if (stage_needed) {
        barrier(cb, stage->texture.img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        spatial_blit(cb, image, stage->texture.img, crop, iw, ih, flip_y, VK_FILTER_NEAREST);
        barrier(cb, stage->texture.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                raster ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL);
    }
#if SP_TEMPORAL
    if (flow) {
        if (!spatial_flow_record(cb, slot, image, crop, flip_y, ticket, output)) {
            vkEndCommandBuffer(cb);
            g_flow.ticket = 0;
            spatial_report(requested, SP_NATIVE, "temporal recording rejected", ticket);
            return NULL;
        }
    } else
#endif
    if (raster) {
        spatial_constants params = {{(float)iw, (float)ih, (float)w, (float)h}, {0, 0}, sharpness, 0};
        spatial_image *first = rcas ? intermediate : output;
        barrier(cb, first->texture.img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        spatial_draw(cb, first_pass, direct ? *source_set : g_spatial.set[SP_SOURCE], first->view, 0, 0, w, h, params);
        if (rcas) {
            barrier(cb, first->texture.img, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            barrier(cb, output->texture.img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            spatial_draw(cb, SP_RCAS, g_spatial.set[SP_INTERMEDIATE], output->view, 0, 0, w, h, params);
        }
        barrier(cb, output->texture.img, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    } else {
        barrier(cb, output->texture.img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        // Vulkan may filter outside a blit's crop offsets. When cropped, scale
        // the isolated source so taps clamp to the picture and avoid guard pixels.
        int full[4] = {0, 0, (int)iw, (int)ih};
        spatial_blit(cb, isolated ? stage->texture.img : image, output->texture.img,
                     isolated ? full : crop, w, h, isolated ? 0 : flip_y, VK_FILTER_LINEAR);
        barrier(cb, output->texture.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    }
    if (g_spatial.queries)
        vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_spatial.queries, 2 * slot + 1);
    if (vkEndCommandBuffer(cb) || vkResetFences(V.dev, 1, &fence)) goto submit_error;
    VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 0, NULL, NULL, 1, &cb};
    extern pthread_mutex_t g_vk_queue_mu;
    pthread_mutex_lock(&g_vk_queue_mu);
    VkResult r = vkQueueSubmit(V.q, 1, &submit, fence);
    pthread_mutex_unlock(&g_vk_queue_mu);
    if (r) goto submit_error;

#if SP_TEMPORAL
    g_flow.budget_ms[slot] = flow ? g_spatial.flow_budget_ms : 0.0f;
    if (flow) {
        vk_gsr2_commit(g_flow.ctx, 1);
        g_flow.next ^= 1;
        g_flow.ticket = ticket;
        g_flow.fence = fence;
        for (unsigned i = SP_FLOW0; i <= SP_FLOW1; ++i) g_spatial.image[i].last_use = fence;
    }
#endif
    if (physical) {
        atomic_fetch_add(&physical->backing->refs, 1);
        g_spatial.source[slot] = physical->backing;
        g_spatial.live_sources |= 1u << slot;
        if (ticket % 120 == 0 && vk_pass_dump_user_active())
            VKLOG("[upscale] real frame %llu physical %ux%u, logical %ux%u, output %ux%u%s",
                  (unsigned long long)ticket, iw, ih, physical->logical.width, physical->logical.height,
                  w, h, stage_needed ? " (physical-size staging)" : "");
    } else src->layout = VK_IMAGE_LAYOUT_GENERAL;
    if (stage_needed) stage->last_use = fence;
    if (rcas) intermediate->last_use = fence;
    output->last_use = fence;
    g_spatial.encoding = (unsigned)mode * 128u + (raster ? (unsigned)(sharpness * 100.0f + 0.5f) : 0u);
    g_spatial.ticket = ticket;
    g_spatial.query_ticket[slot] = ticket;
    spatial_report(requested, mode, reason, ticket);
    return output;

submit_error:
#if SP_TEMPORAL
    if (flow) vk_gsr2_commit(g_flow.ctx, 0);
#endif
    g_spatial.submit_failed = 1;
    spatial_report(requested, SP_NATIVE, "reconstruction submission failed", ticket);
    return NULL;
}

static int vivid_record(VkCommandBuffer cb, VkImage src, const int crop[4], int flip_y,
                        VkImage tgt, VkImageView view, int dx, int dy, uint32_t dw, uint32_t dh) {
    if (!spatial_pipeline_ready(SP_VIVID) ||
        !spatial_image_ready(SP_DISPLAY, dw, dh, VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                             VK_IMAGE_USAGE_SAMPLED_BIT, g_sfmt)) return 0;
    spatial_image *stage = &g_spatial.image[SP_DISPLAY];
    barrier(cb, stage->texture.img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    spatial_blit(cb, src, stage->texture.img, crop, dw, dh, flip_y, VK_FILTER_LINEAR);
    barrier(cb, stage->texture.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    barrier(cb, tgt, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    int srgb = spatial_unorm_format(g_sfmt) != g_sfmt;
    spatial_constants params = {{0}, {(float)dx, (float)dy}, 0, srgb ? 2.0f : 1.0f};
    spatial_draw(cb, SP_VIVID, g_spatial.set[SP_DISPLAY], view, dx, dy, dw, dh, params);
    return 1;
}
