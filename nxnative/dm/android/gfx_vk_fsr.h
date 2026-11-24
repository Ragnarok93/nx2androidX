// FidelityFX Super Resolution 1 Vulkan presentation integration.
// Included only by gfx_vk_present.c after its swapchain globals. Every generated frame
// runs through EASU + RCAS before UI compositing; disabled/unsupported devices retain
// the original VK_FILTER_LINEAR blit path without additional allocations.
static struct {
    VkDescriptorSetLayout dsl;
    VkPipelineLayout layout;
    VkDescriptorPool pool;
    VkDescriptorSet set[2];
    VkSampler sampler;
    VkPipeline pipeline[2];  // EASU / RCAS
    VkFormat fmt;
    VkImage stage, up;
    VkDeviceMemory stage_mem, up_mem;
    VkImageView stage_view, up_view;
    uint32_t iw, ih, ow, oh;
    int unavailable, use_fsr;
} g_fsr;

static void fsr_drop_images(void) {
    if (g_fsr.stage_view) vkDestroyImageView(V.dev, g_fsr.stage_view, NULL);
    if (g_fsr.up_view) vkDestroyImageView(V.dev, g_fsr.up_view, NULL);
    if (g_fsr.stage) vkDestroyImage(V.dev, g_fsr.stage, NULL);
    if (g_fsr.up) vkDestroyImage(V.dev, g_fsr.up, NULL);
    if (g_fsr.stage_mem) vkFreeMemory(V.dev, g_fsr.stage_mem, NULL);
    if (g_fsr.up_mem) vkFreeMemory(V.dev, g_fsr.up_mem, NULL);
    g_fsr.stage_view = g_fsr.up_view = VK_NULL_HANDLE;
    g_fsr.stage = g_fsr.up = VK_NULL_HANDLE;
    g_fsr.stage_mem = g_fsr.up_mem = VK_NULL_HANDLE;
    g_fsr.iw = g_fsr.ih = g_fsr.ow = g_fsr.oh = 0;
}

static VkResult fsr_make_image(uint32_t w, uint32_t h, VkImageUsageFlags usage,
                              VkImage *img, VkDeviceMemory *mem, VkImageView *view) {
    VkImageCreateInfo ic = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, 0, VK_IMAGE_TYPE_2D, g_sfmt,
        {w, h, 1}, 1, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, usage,
        VK_SHARING_MODE_EXCLUSIVE, 0, NULL, VK_IMAGE_LAYOUT_UNDEFINED};
    VkResult r = vkCreateImage(V.dev, &ic, NULL, img);
    VkMemoryRequirements mr = {0};
    if (r == VK_SUCCESS) vkGetImageMemoryRequirements(V.dev, *img, &mr);
    VkMemoryAllocateInfo ma = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mr.size,
        r == VK_SUCCESS ? vk_mem_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0) : 0};
    if (r == VK_SUCCESS) r = vkAllocateMemory(V.dev, &ma, NULL, mem);
    if (r == VK_SUCCESS) r = vkBindImageMemory(V.dev, *img, *mem, 0);
    VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, *img,
        VK_IMAGE_VIEW_TYPE_2D, g_sfmt, {0}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    if (r == VK_SUCCESS) r = vkCreateImageView(V.dev, &vi, NULL, view);
    return r;
}

static int fsr_pipeline_ready(void) {
    if (g_fsr.unavailable) return 0;
    VkResult r = VK_SUCCESS;
    if (!g_fsr.dsl) {
        VkDescriptorSetLayoutBinding binding = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL};
        VkDescriptorSetLayoutCreateInfo dc = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, 1, &binding};
        r = vkCreateDescriptorSetLayout(V.dev, &dc, NULL, &g_fsr.dsl);
        VkPushConstantRange pc = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, 32};
        VkPipelineLayoutCreateInfo lc = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 1, &g_fsr.dsl, 1, &pc};
        if (!r) r = vkCreatePipelineLayout(V.dev, &lc, NULL, &g_fsr.layout);
        VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2};
        VkDescriptorPoolCreateInfo pool = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, 2, 1, &ps};
        if (!r) r = vkCreateDescriptorPool(V.dev, &pool, NULL, &g_fsr.pool);
        VkDescriptorSetLayout ls[2] = {g_fsr.dsl, g_fsr.dsl};
        VkDescriptorSetAllocateInfo ai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, g_fsr.pool, 2, ls};
        if (!r) r = vkAllocateDescriptorSets(V.dev, &ai, g_fsr.set);
        VkSamplerCreateInfo si = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, NULL, 0, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
        if (!r) r = vkCreateSampler(V.dev, &si, NULL, &g_fsr.sampler);
    }
    if (!r && g_fsr.fmt != g_sfmt) {
        for (int i = 0; i < 2; i++) {
            if (g_fsr.pipeline[i]) vkDestroyPipeline(V.dev, g_fsr.pipeline[i], NULL);
            g_fsr.pipeline[i] = VK_NULL_HANDLE;
        }
        const uint32_t *words[3] = {fsr_vert_spv, fsr_easu_frag_spv, fsr_rcas_frag_spv};
        size_t sizes[3] = {sizeof fsr_vert_spv, sizeof fsr_easu_frag_spv, sizeof fsr_rcas_frag_spv};
        VkShaderModule module[3] = {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
        for (int i = 0; i < 3 && !r; i++) {
            VkShaderModuleCreateInfo sc = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, sizes[i], words[i]};
            r = vkCreateShaderModule(V.dev, &sc, NULL, &module[i]);
        }
        for (int i = 0; i < 2 && !r; i++) {
            VkPipelineShaderStageCreateInfo ss[2] = {
                {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, module[0], "main", NULL},
                {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, module[i + 1], "main", NULL}};
            VkPipelineVertexInputStateCreateInfo vi = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
            VkPipelineInputAssemblyStateCreateInfo ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, NULL, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0};
            VkPipelineViewportStateCreateInfo vs = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, NULL, 0, 1, NULL, 1, NULL};
            VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, NULL, 0, 0, 0, VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0, 0, 0, 0, 1};
            VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, NULL, 0, VK_SAMPLE_COUNT_1_BIT};
            VkPipelineColorBlendAttachmentState ba = {0, 0, 0, 0, 0, 0, 0, 0xf};
            VkPipelineColorBlendStateCreateInfo blend = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, NULL, 0, 0, 0, 1, &ba};
            VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, NULL, 0, 2, states};
            VkPipelineRenderingCreateInfo rendering = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, NULL, 0, 1, &g_sfmt};
            VkGraphicsPipelineCreateInfo gc = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &rendering, 0, 2, ss,
                &vi, &ia, NULL, &vs, &rs, &ms, NULL, &blend, &dynamic, g_fsr.layout, VK_NULL_HANDLE};
            r = vkCreateGraphicsPipelines(V.dev, V.pcache, 1, &gc, NULL, &g_fsr.pipeline[i]);
        }
        for (int i = 0; i < 3; i++) if (module[i]) vkDestroyShaderModule(V.dev, module[i], NULL);
        if (!r) {
            g_fsr.fmt = g_sfmt;
            vk_pipeline_cache_changed();
        }
    }
    if (r) {
        g_fsr.unavailable = 1;
        VKRED_ONCE(128, "FSR 1 disabled on this Vulkan device: setup failed (%d); using linear blit", r);
        return 0;
    }
    return 1;
}

static int fsr_prepare(uint32_t iw, uint32_t ih, uint32_t ow, uint32_t oh, int use_fsr) {
    if (!fsr_pipeline_ready()) return 0;
    if (iw == g_fsr.iw && ih == g_fsr.ih && ow == g_fsr.ow && oh == g_fsr.oh && use_fsr == g_fsr.use_fsr) return 1;
    // This pair is shared between present submissions. On a size change, retire all
    // outstanding submissions before destroying images or updating descriptors.
    vkDeviceWaitIdle(V.dev);
    fsr_drop_images();
    VkResult r = fsr_make_image(iw, ih, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                &g_fsr.stage, &g_fsr.stage_mem, &g_fsr.stage_view);
    if (!r && use_fsr) r = fsr_make_image(ow, oh, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                &g_fsr.up, &g_fsr.up_mem, &g_fsr.up_view);
    if (r) {
        fsr_drop_images();
        g_fsr.unavailable = 1;
        VKRED_ONCE(129, "FSR 1 disabled: intermediate image allocation (%d); using linear blit", r);
        return 0;
    }
    VkDescriptorImageInfo image[2] = {
        {g_fsr.sampler, g_fsr.stage_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {g_fsr.sampler, g_fsr.up_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
    VkWriteDescriptorSet wr[2];
    for (int i = 0; i < 2; i++) wr[i] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL,
        g_fsr.set[i], 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &image[i]};
    vkUpdateDescriptorSets(V.dev, use_fsr ? 2 : 1, wr, 0, NULL);
    g_fsr.use_fsr = use_fsr;
    g_fsr.iw = iw; g_fsr.ih = ih; g_fsr.ow = ow; g_fsr.oh = oh;
    VKLOG("[fsr] 1.0 EASU/RCAS ready: %ux%u -> %ux%u", iw, ih, ow, oh);
    return 1;
}

typedef struct { float sizes[4], origin[2], sharpness, padding; } fsr_constants;
static void fsr_draw(VkCommandBuffer cb, int pass, int set, VkImageView view,
                     int x, int y, uint32_t w, uint32_t h, fsr_constants c) {
    VkRenderingAttachmentInfo ca = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, NULL, view,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, NULL, VK_IMAGE_LAYOUT_UNDEFINED,
        pass == 0 ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : VK_ATTACHMENT_LOAD_OP_LOAD,
        VK_ATTACHMENT_STORE_OP_STORE};
    VkRenderingInfo ri = {VK_STRUCTURE_TYPE_RENDERING_INFO, NULL, 0,
        {{x, y}, {w, h}}, 1, 0, 1, &ca, NULL, NULL};
    vkCmdBeginRendering(cb, &ri);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_fsr.pipeline[pass]);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_fsr.layout, 0, 1, &g_fsr.set[set], 0, NULL);
    vkCmdPushConstants(cb, g_fsr.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof c, &c);
    VkViewport vp = {(float)x, (float)y, (float)w, (float)h, 0, 1};
    VkRect2D sc = {{x, y}, {w, h}};
    vkCmdSetViewport(cb, 0, 1, &vp);
    vkCmdSetScissor(cb, 0, 1, &sc);
    vkCmdDraw(cb, 3, 1, 0, 0);
    vkCmdEndRendering(cb);
}

static int fsr_record(VkCommandBuffer cb, VkImage source, VkImage target, VkImageView target_view,
                      int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh,
                      int flip_y, int use_fsr, int vivid, VkImageLayout *target_layout) {
    if (!V.has_dynrender || sw < 16 || sh < 16 || dw < 16 || dh < 16) return 0;
    if ((uint64_t)sw * sh > 12000000ull || (uint64_t)dw * dh > 12000000ull) return 0; // bounded VRAM
    uint32_t iw = use_fsr ? (uint32_t)sw : (uint32_t)dw, ih = use_fsr ? (uint32_t)sh : (uint32_t)dh;
    if (!fsr_prepare(iw, ih, (uint32_t)dw, (uint32_t)dh, use_fsr)) return 0;
    barrier(cb, g_fsr.stage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageBlit copy = {{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {{sx, sy, 0}, {sx + sw, sy + sh, 1}},
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {{0, 0, 0}, {(int)iw, (int)ih, 1}}};
    if (flip_y) { copy.srcOffsets[0].y = sy + sh; copy.srcOffsets[1].y = sy; }
    vkCmdBlitImage(cb, source, VK_IMAGE_LAYOUT_GENERAL, g_fsr.stage,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy, use_fsr ? VK_FILTER_NEAREST : VK_FILTER_LINEAR);
    barrier(cb, g_fsr.stage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    int srgb = g_sfmt == VK_FORMAT_R8G8B8A8_SRGB || g_sfmt == VK_FORMAT_B8G8R8A8_SRGB || g_sfmt == VK_FORMAT_A8B8G8R8_SRGB_PACK32;
    fsr_constants c = {{(float)iw, (float)ih, (float)dw, (float)dh},
                       {0, 0}, use_fsr ? (float)dm_fsr_sharpness() / 100.0f : 0.0f, vivid ? (srgb ? 2.0f : 1.0f) : 0.0f};
    if (use_fsr) {
        barrier(cb, g_fsr.up, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        fsr_draw(cb, 0, 0, g_fsr.up_view, 0, 0, (uint32_t)dw, (uint32_t)dh, c);
        barrier(cb, g_fsr.up, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    // tgt is transfer-dst cleared black by the caller; switch it to color attachment,
    // and draw only inside the aspect-fitted picture rect, leaving letterboxing black.
    barrier(cb, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    *target_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    c.origin[0] = (float)dx; c.origin[1] = (float)dy;
    fsr_draw(cb, 1, use_fsr ? 1 : 0, target_view, dx, dy, (uint32_t)dw, (uint32_t)dh, c);
    return 1;
}
