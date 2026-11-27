// Present-thread-owned spatial reconstruction. All work uses the graphics queue.
int vk_pass_dump_user_active(void);
#include <time.h>
#include "gfx_vk_reconstruction_limits.h"

#if !defined(SPATIAL_HOST_PROBE) || defined(SGSR2_HOST_PROBE)
#include "gfx_vk_gsr2.h"
#include "gfx_vk_flow_budget.h"
#define SP_TEMPORAL 1
#else
#define SP_TEMPORAL 0
#endif

enum { SP_EASU, SP_RCAS, SP_GSR1, SP_VIVID, SP_PASS_COUNT };
enum { SP_SOURCE, SP_INTERMEDIATE, SP_DISPLAY, SP_OUTPUT, SP_FLOW0, SP_FLOW1, SP_IMAGE_COUNT };
enum { SP_SLOTS = 8, SP_QUERY_STRIDE = 10 };

typedef struct {
    vkimg_t texture;
    VkImageView view;
    VkImageUsageFlags usage;
    VkFormat view_fmt;
    VkFence last_use;
    VkDeviceSize bytes;
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
    uint32_t flow_cadence_us;
    uint64_t ticket, attempted_ticket, retry_after, rcas_retry_after, measured_ticket;
    uint64_t report_alloc_wait_ns, report_slot_wait_ns;
    unsigned report_alloc_attempts, report_allocations, report_alloc_waits;
    uint64_t query_ticket[SP_SLOTS];
    int query_mode[SP_SLOTS], measured_mode;
    VkExtent2D query_input[SP_SLOTS], query_output[SP_SLOTS], measured_input, measured_output;
    float query_budget[SP_SLOTS], measured_budget;
    uint32_t query_cadence[SP_SLOTS], measured_cadence;
    uint64_t query_epoch[SP_SLOTS], measured_epoch;
    dm_recon_shared_retry shared_output_retry;
    double gpu_ms, measured_stage_ms[4];
    uint64_t query_history_epoch[SP_SLOTS], measured_history_epoch;
    int query_history_reset[SP_SLOTS], measured_history_reset;
    int submit_failed, report_physical, report_sharpness_requested, report_sharpness_applied;
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
    unsigned next, resource_failures, resource_streak;
    dm_flow_budget policy;
    uint64_t query_generation[SP_SLOTS];
    uint64_t ticket, retry_after, history_epoch, pending_epoch, last_real_ticket;
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
    g_spatial.ticket = g_spatial.attempted_ticket = g_spatial.retry_after = g_spatial.rcas_retry_after = 0;
    memset(g_spatial.query_ticket, 0, sizeof g_spatial.query_ticket);
    g_spatial.measured_ticket = 0;
    dm_recon_shared_succeeded(&g_spatial.shared_output_retry);
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

// Capacity limits are separate from resolution: actual memory requirements
// are checked below. A heap-size fraction is NOT a free-memory measurement.
static uint64_t spatial_local_heap(void) {
    uint64_t heap = 0;
    for (uint32_t i = 0; i < V.mem.memoryHeapCount; ++i)
        if ((V.mem.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) &&
            V.mem.memoryHeaps[i].size > heap) heap = V.mem.memoryHeaps[i].size;
    return heap;
}
static int spatial_extent_ok(uint32_t w, uint32_t h, int raster) {
    uint32_t limit = V.props.limits.maxImageDimension2D;
    if (!dm_recon_extent_ok(w, h, limit, limit)) return 0;
    return !raster || dm_recon_extent_ok(w, h, V.props.limits.maxFramebufferWidth,
                                          V.props.limits.maxFramebufferHeight);
}
static uint64_t spatial_clock_ns(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) return 0;
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}
static VkResult spatial_wait(VkFence fence, uint64_t *elapsed) {
    int measure = vk_pass_dump_user_active();
    uint64_t before = measure ? spatial_clock_ns() : 0;
    VkResult result = vkWaitForFences(V.dev, 1, &fence, VK_TRUE, UINT64_MAX);
    if (before) {
        uint64_t after = spatial_clock_ns();
        if (after >= before) *elapsed += after - before;
    }
    return result;
}
static int spatial_image_matches(const spatial_image *s, uint32_t w, uint32_t h,
                                 VkImageUsageFlags usage, VkFormat view_fmt) {
    return s->texture.img && s->texture.w == w && s->texture.h == h &&
        s->texture.fmt == g_sfmt && s->view_fmt == view_fmt && (s->usage & usage) == usage;
}
static int spatial_image_ready(int index, uint32_t w, uint32_t h, VkImageUsageFlags usage, VkFormat view_fmt) {
    spatial_image *s = &g_spatial.image[index];
    if (spatial_image_matches(s, w, h, usage, view_fmt)) return 1;
    if (!spatial_extent_ok(w, h, (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0)) return 0;
    VkImageCreateFlags flags = view_fmt != g_sfmt ? VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT : 0;
    VkImageFormatProperties props;
    if (vkGetPhysicalDeviceImageFormatProperties(V.pd, g_sfmt, VK_IMAGE_TYPE_2D,
            VK_IMAGE_TILING_OPTIMAL, usage, flags, &props) ||
        !dm_recon_extent_ok(w, h, props.maxExtent.width, props.maxExtent.height)) return 0;
    uint64_t used = 0;
    for (unsigned i = 0; i < SP_IMAGE_COUNT; ++i)
        if (i != (unsigned)index) used += g_spatial.image[i].bytes;

    // Descriptor writes and image retirement wait only for the last user of this
    // particular allocation, on resize or a backend change. In steady state GPU
    // barriers order all reads and writes, with immutable descriptors.
    if (s->last_use) {
        g_spatial.report_alloc_waits++;
        if (spatial_wait(s->last_use, &g_spatial.report_alloc_wait_ns)) return 0;
    }
    g_spatial.report_alloc_attempts++;
    spatial_image_drop(s);
    vkimg_t *t = &s->texture;
    VkImageCreateInfo ic = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, flags, VK_IMAGE_TYPE_2D, g_sfmt,
        {w, h, 1}, 1, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, usage,
        VK_SHARING_MODE_EXCLUSIVE, 0, NULL, VK_IMAGE_LAYOUT_UNDEFINED};
    VkResult r = vkCreateImage(V.dev, &ic, NULL, &t->img);
    if (!r) {
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(V.dev, t->img, &req);
        uint32_t type = vk_mem_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
        if (type == UINT32_MAX || req.size > props.maxResourceSize ||
            !dm_recon_fits(dm_recon_budget(spatial_local_heap()), used, req.size))
            r = VK_ERROR_OUT_OF_DEVICE_MEMORY;
        else {
            s->bytes = req.size;
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
    g_spatial.report_allocations++;
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
        VK_QUERY_TYPE_TIMESTAMP, SP_QUERY_STRIDE * SP_SLOTS};
    if (g_spatial.timestamp_bits &&
        vkCreateQueryPool(V.dev, &queries, NULL, &g_spatial.queries)) g_spatial.queries = VK_NULL_HANDLE;
    return 1;
}

static void spatial_read_timing(unsigned slot, uint64_t decision_ticket) {
    (void)decision_ticket;
    uint64_t stamp[SP_QUERY_STRIDE];
    if (!g_spatial.queries || !g_spatial.query_ticket[slot]) return;
    uint64_t measured_ticket = g_spatial.query_ticket[slot];
    g_spatial.query_ticket[slot] = 0; // This retired slot is consumed once.
    g_spatial.measured_ticket = 0; // Missing result is not a fresh measurement.
    g_spatial.gpu_ms = 0;
    unsigned count = g_spatial.query_mode[slot] == SP_SGSR2 ? SP_QUERY_STRIDE : 2;
    memset(g_spatial.measured_stage_ms, 0, sizeof g_spatial.measured_stage_ms);
    g_spatial.measured_history_epoch = 0;
    g_spatial.measured_history_reset = 0;
    if (vkGetQueryPoolResults(V.dev, g_spatial.queries, SP_QUERY_STRIDE * slot, count,
                              count * sizeof stamp[0], stamp, sizeof stamp[0], VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
        uint32_t bits = g_spatial.timestamp_bits;
        uint64_t mask = bits >= 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1;
        g_spatial.gpu_ms = ((stamp[1] - stamp[0]) & mask) * (double)V.props.limits.timestampPeriod / 1e6;
        g_spatial.measured_ticket = measured_ticket;
        g_spatial.measured_mode = g_spatial.query_mode[slot];
        g_spatial.measured_input = g_spatial.query_input[slot];
        g_spatial.measured_output = g_spatial.query_output[slot];
        g_spatial.measured_budget = g_spatial.query_budget[slot];
        g_spatial.measured_cadence = g_spatial.query_cadence[slot];
        g_spatial.measured_epoch = g_spatial.query_epoch[slot];
        if (count == SP_QUERY_STRIDE) {
            for (unsigned i = 0; i < 4; ++i)
                g_spatial.measured_stage_ms[i] = ((stamp[3 + 2*i] - stamp[2 + 2*i]) & mask) *
                    (double)V.props.limits.timestampPeriod / 1e6;
            g_spatial.measured_history_epoch = g_spatial.query_history_epoch[slot];
            g_spatial.measured_history_reset = g_spatial.query_history_reset[slot];
        }
#if SP_TEMPORAL
        if (g_spatial.measured_mode == SP_SGSR2)
            dm_flow_budget_observe(&g_flow.policy, g_flow.query_generation[slot],
                measured_ticket, decision_ticket, g_flow.budget_ms[slot], g_spatial.gpu_ms);
#endif
    }
}

// The next recording slot has already been fence-waited. If it holds a
// pending measurement, retire it before reuse. Otherwise look for the
// oldest outstanding query on this single graphics queue. Poll at most one
// fence, never block on a different slot and never publish two samples in
// one capture row. Retiring a query does not retire its command resources.
static void spatial_retire_timing(unsigned reuse_slot, uint64_t decision_ticket) {
    if (!g_spatial.queries) return;
    if (g_spatial.query_ticket[reuse_slot]) {
        spatial_read_timing(reuse_slot, decision_ticket);
        return;
    }
    unsigned oldest = SP_SLOTS;
    for (unsigned i = 0; i < SP_SLOTS; ++i) {
        uint64_t ticket = g_spatial.query_ticket[i];
        if (!ticket || ticket >= decision_ticket) continue;
        if (oldest == SP_SLOTS || ticket < g_spatial.query_ticket[oldest]) oldest = i;
    }
    if (oldest != SP_SLOTS &&
        vkWaitForFences(V.dev, 1, &g_spatial.fence[oldest], VK_TRUE, 0) == VK_SUCCESS)
        spatial_read_timing(oldest, decision_ticket);
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
            VKLOG("[upscale] real frame %llu, requested %s, active %s, %s, GPU %.3f ms (%s frame %llu)",
                  (unsigned long long)ticket, spatial_mode_name(requested), spatial_mode_name(active),
                  reason ? reason : "ready", g_spatial.gpu_ms, spatial_mode_name(g_spatial.measured_mode),
                  (unsigned long long)g_spatial.measured_ticket);
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

static void spatial_flow_defer(uint64_t ticket) {
    g_flow.ticket = 0;
    g_flow.resource_streak = 0;
    g_flow.retry_after = dm_flow_resource_defer(&g_flow.resource_failures, ticket);
    dm_flow_budget_retire_samples(&g_flow.policy);
}

static int spatial_flow_ready(uint32_t iw, uint32_t ih, uint32_t w, uint32_t h,
                              VkFormat format, const int crop[4], int flip_y,
                              int physical, uint64_t ticket) {
    if (ticket < g_flow.retry_after || iw > w || ih > h || iw < 16 || ih < 16 ||
        !spatial_extent_ok(iw, ih, 0) || !spatial_extent_ok(w, h, 1)) return 0;
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
        g_flow.ctx = vk_gsr2_create_image(g_flow.render, g_flow.output,
            dm_recon_budget(spatial_local_heap()));
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
    spatial_flow_defer(ticket);
    return 0;
}

static int spatial_flow_record(VkCommandBuffer cb, unsigned slot, VkImage image,
                               const int crop[4], int flip_y, uint64_t ticket,
                               spatial_image *output) {
    spatial_image *current = &g_spatial.image[SP_FLOW0 + g_flow.next];
    const int cut = !g_flow.ticket || ticket - g_flow.ticket != 1;
    const unsigned first_query = SP_QUERY_STRIDE * slot;
    if (g_spatial.queries)
        vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g_spatial.queries, first_query + 2);
    spatial_image *previous = cut ? current : &g_spatial.image[SP_FLOW0 + (g_flow.next ^ 1)];
    barrier(cb, current->texture.img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    spatial_blit(cb, image, current->texture.img, crop, g_flow.render.width, g_flow.render.height,
                 flip_y, VK_FILTER_NEAREST);
    barrier(cb, current->texture.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    if (g_spatial.queries)
        vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_spatial.queries, first_query + 3);
    // Advance only the tentative epoch on a discontinuity; publication follows submit.
    g_flow.pending_epoch = g_flow.history_epoch + (cut ? 1 : 0);
    dm_temporal_frame frame = {.ticket = ticket, .stream = 1, .epoch = g_flow.pending_epoch,
        .iw = g_flow.render.width, .ih = g_flow.render.height,
        .ow = g_flow.output.width, .oh = g_flow.output.height,
        .inputs = DM_TEMPORAL_IMAGE_PAIR, .variants = DM_TEMPORAL_2FS,
        .real = 1, .memory_ok = 1, .cut = cut};
    vk_gsr2_output result;
    const vk_gsr2_inputs inputs = {
        .color = {current->view, VK_IMAGE_LAYOUT_GENERAL},
        .previous_color = {previous->view, VK_IMAGE_LAYOUT_GENERAL}, .pre_exposure = 1.0f,
    };
    const vk_gsr2_timing timing = {g_spatial.queries, first_query + 4};
    if (!vk_gsr2_record(g_flow.ctx, cb, slot, &frame, &inputs,
            g_spatial.queries ? &timing : NULL, &result)) return 0;
    if (g_spatial.queries)
        vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g_spatial.queries, first_query + 8);
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
    // The same real ticket may not record again after bypass or failure.
    // Intra-call fallback still retries through the existing backend labels.
    if (ticket <= g_spatial.attempted_ticket) return NULL;
    g_spatial.attempted_ticket = ticket;
    g_spatial.report_alloc_attempts = g_spatial.report_allocations = g_spatial.report_alloc_waits = 0;
    g_spatial.report_alloc_wait_ns = g_spatial.report_slot_wait_ns = 0;
    g_spatial.ticket = 0;
    g_spatial.measured_ticket = 0; // no new completed sample until a slot retires
    g_spatial.gpu_ms = 0;
    memset(g_spatial.measured_stage_ms, 0, sizeof g_spatial.measured_stage_ms);
    g_spatial.measured_history_epoch = 0;
    g_spatial.measured_history_reset = 0;
    g_spatial.report_input = (VkExtent2D){crop[2] > 0 ? (uint32_t)crop[2] : 0,
                                         crop[3] > 0 ? (uint32_t)crop[3] : 0};
    g_spatial.report_physical = 0;
    int requested = dm_spatial_mode();
    int sharpness_pct = dm_fsr_sharpness();
    float sharpness = dm_recon_sharpness(sharpness_pct);
    g_spatial.report_sharpness_requested = (int)(sharpness * 100.0f + 0.5f);
    g_spatial.report_sharpness_applied = 0;
    int wants_flow = requested == SP_SGSR2;
    int mode = wants_flow ? SP_SGSR1 : requested;
    const char *reason = NULL;
#if SP_TEMPORAL
    if (!wants_flow) {
        dm_flow_budget_invalidate(&g_flow.policy);
        g_flow.last_real_ticket = 0;
        g_flow.ticket = 0;
        if (g_flow.ctx) spatial_flow_release();
    }
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
        !w || !h) {
#if SP_TEMPORAL
        dm_flow_budget_invalidate(&g_flow.policy);
        g_flow.ticket = 0;
#endif
        spatial_report(requested, SP_NATIVE, "invalid source crop or output extent", ticket);
        return NULL;
    }
    if (w <= (uint32_t)crop[2] && h <= (uint32_t)crop[3]) {
#if SP_TEMPORAL
        dm_flow_budget_invalidate(&g_flow.policy);
        g_flow.ticket = 0;
#endif
        spatial_report(requested, SP_NATIVE, w == (uint32_t)crop[2] && h == (uint32_t)crop[3] ?
            "native 1:1 presentation input" : "presentation input does not require enlargement", ticket);
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
    int flow = 0, disable_post = 0, reclaimed_optional = 0;
    unsigned slot = g_spatial.slot % SP_SLOTS;
    VkFence fence = g_spatial.fence[slot];
    VkCommandBuffer cb = g_spatial.cb[slot];
#if SP_TEMPORAL
    if (wants_flow) {
        // Actual source tickets, not successful temporal tickets, define a gap.
        // Failed or spatial fallback frames must not restart the overload policy.
        if (g_flow.last_real_ticket && ticket - g_flow.last_real_ticket != 1) {
            dm_flow_budget_invalidate(&g_flow.policy);
            g_flow.ticket = 0;
        }
        g_flow.last_real_ticket = ticket;
        dm_flow_workload key = {.iw=iw, .ih=ih, .ow=w, .oh=h,
            .cadence_us=g_spatial.flow_cadence_us, .source_format=(uint32_t)format,
            .output_format=(uint32_t)g_sfmt, .crop_x=crop[0], .crop_y=crop[1],
            .flip_y=flip_y, .physical=physical != NULL,
            .sharpen=sharpness > 0};
        uint64_t generation = g_flow.policy.generation;
        dm_flow_budget_set_workload(&g_flow.policy, &key);
        if (generation != g_flow.policy.generation) {
            g_flow.retry_after = 0;
            g_flow.resource_failures = g_flow.resource_streak = 0;
        }
    }
#endif
    // Retire and consume timing before deciding which backend records this frame.
    if (spatial_wait(fence, &g_spatial.report_slot_wait_ns)) goto submit_error;
    spatial_forget_fence(fence);
    spatial_release_source(slot);
    spatial_retire_timing(slot, ticket);
#if SP_TEMPORAL
    int budget_allows = wants_flow && dm_flow_budget_allow(&g_flow.policy, ticket);
    flow = budget_allows && (g_spatial.queries || g_spatial.flow_budget_ms == 0.0f) &&
        (src_features & VK_FORMAT_FEATURE_BLIT_SRC_BIT) &&
        (dst_features & VK_FORMAT_FEATURE_BLIT_SRC_BIT) &&
        spatial_flow_ready(iw, ih, w, h, format, crop, flip_y, physical != NULL, ticket);
    if (flow) {
        mode = SP_SGSR2;
        reason = "image-driven motion";
    } else if (wants_flow) {
        if (!budget_allows) reason = g_flow.policy.retry_after == UINT64_MAX ?
            "temporal sustained overload; waiting for workload change" : "temporal sustained overload cooldown";
        else if (!g_spatial.queries && g_spatial.flow_budget_ms > 0.0f)
            reason = "temporal timing capability unavailable";
        else reason = "temporal resources or geometry unavailable";
    }
#endif
select_spatial_bilinear:;
#if SP_TEMPORAL
select_backend:;
#endif
    int rcas = mode == SP_FSR1 && sharpness > 0.001f;
    int first_pass = mode == SP_FSR1 ? SP_EASU : SP_GSR1;
    if (!flow && mode != SP_BILINEAR &&
        (iw < 16 || ih < 16 || (raster_features & sampling) != sampling ||
         !(raster_features & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) ||
         !spatial_pipeline_ready(first_pass))) {
        mode = SP_BILINEAR;
        reason = "spatial shader unavailable";
    }
    if (mode == SP_FSR1 && rcas &&
        (ticket < g_spatial.rcas_retry_after || !spatial_pipeline_ready(SP_RCAS))) {
        rcas = 0;
        reason = "FSR 1 sharpening unavailable";
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
    // Bilinear and temporal reconstruction share the same optional RCAS output
    // pass. Never sharpen the temporal history or feed RCAS back into motion.
    int wants_post = (mode == SP_BILINEAR || flow) && sharpness > 0.001f;
    int post = wants_post && !disable_post && ticket >= g_spatial.rcas_retry_after &&
        (raster_features & sampling) == sampling &&
        (raster_features & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) &&
        spatial_pipeline_ready(SP_RCAS);
    if (wants_post && !post && !reason) reason = "output sharpening unavailable";
    int raster_output = raster || post;
    VkImageUsageFlags out_usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
        (raster_output ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    // Prefer one output with transfer and raster usage and an encoded UNORM view.
    // Some drivers reject this combination; retain the old narrower allocation.
    VkImageUsageFlags shared_usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    // Do not destroy a working narrower allocation solely to retry a wider
    // usage combination. Retry the combination only on a real replacement.
    VkFormat out_view = raster_output ? sample_fmt : g_sfmt;
    int output_ready = spatial_image_matches(output, w, h, out_usage, out_view) ||
        spatial_image_matches(output, w, h, shared_usage, sample_fmt);
    if (!output_ready && dm_recon_shared_allowed(&g_spatial.shared_output_retry,
            (uint32_t)g_sfmt, w, h, ticket) &&
        (raster_features & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) {
        output_ready = spatial_image_ready(SP_OUTPUT, w, h, shared_usage, sample_fmt);
        if (output_ready) dm_recon_shared_succeeded(&g_spatial.shared_output_retry);
        else dm_recon_shared_failed(&g_spatial.shared_output_retry, (uint32_t)g_sfmt, w, h, ticket);
    }
    if (!output_ready)
        output_ready = spatial_image_ready(SP_OUTPUT, w, h, out_usage, out_view);
    int base_ready = output_ready && (!stage_needed ||
        spatial_image_ready(SP_SOURCE, iw, ih, stage_usage, raster ? sample_fmt : g_sfmt));
    // A cached optional image may have consumed the budget for mandatory
    // output/source storage. Retire it once before degrading the base filter.
    if (!base_ready && !reclaimed_optional && intermediate->texture.img) {
        reclaimed_optional = 1;
        if (!intermediate->last_use || !spatial_wait(intermediate->last_use, &g_spatial.report_alloc_wait_ns)) {
            if (intermediate->last_use) g_spatial.report_alloc_waits++;
            spatial_image_drop(intermediate);
            goto select_spatial_bilinear;
        }
    }
    if (!base_ready && post) {
        disable_post = 1;
        g_spatial.rcas_retry_after = ticket + 120;
        if (!reason) reason = "output sharpening unavailable";
        goto select_spatial_bilinear; // same backend, without optional sharpening
    }
    if (!base_ready) {
#if SP_TEMPORAL
        if (flow) {
            spatial_flow_defer(ticket);
            flow = 0;
            mode = SP_SGSR1;
            reason = "temporal output allocation unavailable";
            goto select_backend;
        }
#endif
        if (mode != SP_BILINEAR) {
            mode = SP_BILINEAR;
            reason = "spatial image allocation unavailable";
            goto select_spatial_bilinear;
        }
        g_spatial.retry_after = ticket + 120;
        spatial_report(requested, SP_NATIVE, "image allocation unavailable", ticket);
        return NULL;
    }
    // Base images have priority. A failed optional post pass must not
    // discard the selected reconstruction algorithm or temporal history.
    if (post && !spatial_image_ready(SP_INTERMEDIATE, w, h,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, sample_fmt)) {
        disable_post = 1;
        g_spatial.rcas_retry_after = ticket + 120;
        if (flow) reason = "image-driven motion; output sharpening unavailable";
        else if (!reason) reason = "output sharpening unavailable";
        goto select_spatial_bilinear;
    }
    if (rcas && !spatial_image_ready(SP_INTERMEDIATE, w, h, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                    VK_IMAGE_USAGE_SAMPLED_BIT, sample_fmt)) {
        rcas = 0;
        g_spatial.rcas_retry_after = ticket + 120;
        reason = "FSR 1 sharpening unavailable";
    }

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
        vkCmdResetQueryPool(cb, g_spatial.queries, SP_QUERY_STRIDE * slot, SP_QUERY_STRIDE);
        vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g_spatial.queries, SP_QUERY_STRIDE * slot);
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
        if (!spatial_flow_record(cb, slot, image, crop, flip_y, ticket, post ? intermediate : output)) {
            // Nothing has been submitted; abandon the command buffer and the
            // pending temporal transaction before trying the same real frame.
            vk_gsr2_commit(g_flow.ctx, 0);
            spatial_flow_defer(ticket);
            if (vkEndCommandBuffer(cb) || vkResetCommandBuffer(cb, 0)) goto submit_error;
            flow = 0;
            mode = SP_SGSR1;
            reason = "temporal recording rejected; spatial fallback";
            goto select_backend;
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
        spatial_image *first = post ? intermediate : output;
        barrier(cb, first->texture.img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        // Vulkan may filter outside a blit's crop offsets. When cropped, scale
        // the isolated source so taps clamp to the picture and avoid guard pixels.
        int full[4] = {0, 0, (int)iw, (int)ih};
        spatial_blit(cb, isolated ? stage->texture.img : image, first->texture.img,
                     isolated ? full : crop, w, h, isolated ? 0 : flip_y, VK_FILTER_LINEAR);
        barrier(cb, first->texture.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    }
    if (post) {
        spatial_constants params = {{(float)w, (float)h, (float)w, (float)h}, {0, 0}, sharpness, 0};
        barrier(cb, intermediate->texture.img, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        barrier(cb, output->texture.img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        spatial_draw(cb, SP_RCAS, g_spatial.set[SP_INTERMEDIATE], output->view, 0, 0, w, h, params);
        barrier(cb, output->texture.img, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    }
    if (g_spatial.queries) {
        if (flow) vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_spatial.queries, SP_QUERY_STRIDE * slot + 9);
        vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_spatial.queries, SP_QUERY_STRIDE * slot + 1);
    }
    if (vkEndCommandBuffer(cb) || vkResetFences(V.dev, 1, &fence)) goto submit_error;
    VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 0, NULL, NULL, 1, &cb};
    extern pthread_mutex_t g_vk_queue_mu;
    pthread_mutex_lock(&g_vk_queue_mu);
    VkResult r = vkQueueSubmit(V.q, 1, &submit, fence);
    pthread_mutex_unlock(&g_vk_queue_mu);
    if (r) goto submit_error;

#if SP_TEMPORAL
    g_flow.budget_ms[slot] = flow ? g_spatial.flow_budget_ms : 0.0f;
    g_flow.query_generation[slot] = flow ? g_flow.policy.generation : 0;
    if (!flow) { g_flow.ticket = 0; g_flow.resource_streak = 0; }
    if (flow) {
        g_spatial.query_history_epoch[slot] = g_flow.pending_epoch;
        g_spatial.query_history_reset[slot] = g_flow.pending_epoch != g_flow.history_epoch;
        vk_gsr2_commit(g_flow.ctx, 1);
        g_flow.history_epoch = g_flow.pending_epoch;
        if (g_flow.resource_failures && ++g_flow.resource_streak >= 60)
            g_flow.resource_failures = g_flow.resource_streak = 0;
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
    if (rcas || post) intermediate->last_use = fence;
    output->last_use = fence;
    // Effective processing is part of LSFG's source contract, including GSR1's
    // built-in sharpening. A changed slider resets LSFG once on the next real frame.
    g_spatial.report_sharpness_applied = (rcas || post || mode == SP_SGSR1) ?
        g_spatial.report_sharpness_requested : 0;
    g_spatial.encoding = (unsigned)mode * 128u + (unsigned)g_spatial.report_sharpness_applied;
    g_spatial.ticket = ticket;
    g_spatial.slot++;
    g_spatial.query_ticket[slot] = ticket;
    g_spatial.query_mode[slot] = mode;
    g_spatial.query_input[slot] = (VkExtent2D){iw, ih};
    g_spatial.query_output[slot] = (VkExtent2D){w, h};
    g_spatial.query_budget[slot] = flow ? g_spatial.flow_budget_ms : 0.0f;
    g_spatial.query_cadence[slot] = g_spatial.flow_cadence_us;
    g_spatial.query_epoch[slot] = 0;
#if SP_TEMPORAL
    if (flow) g_spatial.query_epoch[slot] = g_flow.policy.generation;
#endif
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
