// Fixture inputs stand in for guest vertex buffers and uniforms absent from
// the capture. The shader modules themselves are supplied at test time.
#include "gfx_vk_guest_filter.h"
#include "guest_vert.spv.h"
#include "guest_edge_frag.spv.h"
#include "guest_smooth_frag.spv.h"
#include "guest_spatial_frag.spv.h"
#ifdef SGSR2_HOST_PROBE
#include "guest_temporal_frag.spv.h"
#endif

enum { GUEST_VS_UBO = 4096, GUEST_FS_UBO = 5120,
       GUEST_EDGE_UBO = 6144, GUEST_VERTEX_OFFSET = 7168 };

static VkPipeline guest_pipeline(VkPipelineLayout layout, const uint32_t *code, size_t size,
                                 VkBool32 blend_enabled, VkColorComponentFlags write_mask, unsigned spatial_mode) {
    VkShaderModule modules[2];
    VkShaderModuleCreateInfo vertex = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0,
                                       sizeof guest_vert_spv, guest_vert_spv};
    VkShaderModuleCreateInfo fragment = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, size, code};
    CHECK(vkCreateShaderModule(V.dev, &vertex, NULL, &modules[0]));
    CHECK(vkCreateShaderModule(V.dev, &fragment, NULL, &modules[1]));
    VkPipelineShaderStageCreateInfo stages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, modules[0], "main"},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, modules[1], "main"},
    };
    VkSpecializationMapEntry mode = {0, 0, sizeof spatial_mode};
    VkSpecializationInfo spec = {1, &mode, sizeof spatial_mode, &spatial_mode};
    if (spatial_mode) stages[1].pSpecializationInfo = &spec;
    VkVertexInputBindingDescription binding = {0, 8 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[] = {
        {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
        {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 4 * sizeof(float)},
    };
    VkPipelineVertexInputStateCreateInfo input = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                                                 NULL, 0, 1, &binding, 2, attributes};
    VkPipelineInputAssemblyStateCreateInfo assembly = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                                                       NULL, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkViewport viewport = {0, 0, OUT, OUT, 0, 1};
    VkPipelineViewportStateCreateInfo region = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                                               NULL, 0, 1, &viewport, 1};
    VkDynamicState dynamic_scissor = VK_DYNAMIC_STATE_SCISSOR;
    VkPipelineDynamicStateCreateInfo dynamic = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                                NULL, 0, 1, &dynamic_scissor};
    VkPipelineRasterizationStateCreateInfo raster = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .lineWidth = 1.0f};
    VkPipelineMultisampleStateCreateInfo samples = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                                    NULL, 0, VK_SAMPLE_COUNT_1_BIT};
    // A synthetic later pass reads destination alpha when it blends.
    VkPipelineColorBlendAttachmentState color = {blend_enabled, VK_BLEND_FACTOR_DST_ALPHA,
        VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA, VK_BLEND_OP_ADD,
        VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_ADD, write_mask};
    VkPipelineColorBlendStateCreateInfo blend = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                                  NULL, 0, VK_FALSE, 0, 1, &color};
    VkPipelineRenderingCreateInfo attachment = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
                                                NULL, 0, 1, &g_sfmt};
    VkGraphicsPipelineCreateInfo info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &attachment,
        0, 2, stages, &input, &assembly, NULL, &region, &raster, &samples, NULL, &blend, &dynamic, layout};
    VkPipeline pipeline;
    CHECK(vkCreateGraphicsPipelines(V.dev, VK_NULL_HANDLE, 1, &info, NULL, &pipeline));
    for (unsigned i = 0; i < 2; ++i) vkDestroyShaderModule(V.dev, modules[i], NULL);
    return pipeline;
}

static void guest_draw_bound(VkPipeline pipeline, VkPipelineLayout layout, const VkDescriptorSet *sets,
                             unsigned set_count, const spatial_image *target, VkAttachmentLoadOp load,
                             unsigned coverage_w) {
    VkRenderingAttachmentInfo color = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, NULL,
        target->view, VK_IMAGE_LAYOUT_GENERAL, VK_RESOLVE_MODE_NONE, VK_NULL_HANDLE,
        VK_IMAGE_LAYOUT_UNDEFINED, load, VK_ATTACHMENT_STORE_OP_STORE, {.color = {{0, 0, 0, 1}}}};
    VkRenderingInfo info = {VK_STRUCTURE_TYPE_RENDERING_INFO, NULL, 0,
                            {{0, 0}, {OUT, OUT}}, 1, 0, 1, &color};
    VkDeviceSize offset = GUEST_VERTEX_OFFSET;
    VkRect2D scissor = {{0, 0}, {coverage_w, OUT}};
    vkCmdBeginRendering(test_cb, &info);
    vkCmdBindPipeline(test_cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(test_cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, set_count, sets, 0, NULL);
    vkCmdBindVertexBuffers(test_cb, 0, 1, &pixels, &offset);
    vkCmdSetScissor(test_cb, 0, 1, &scissor);
    vkCmdDraw(test_cb, 3, 1, 0, 0);
    vkCmdEndRendering(test_cb);
}

static void guest_draw(VkPipeline pipeline, VkPipelineLayout layout, const VkDescriptorSet sets[2],
                       const spatial_image *target, VkAttachmentLoadOp load, unsigned coverage_w) {
    guest_draw_bound(pipeline, layout, sets, 2, target, load, coverage_w);
}

static void guest_readback(const spatial_image *target) {
    vk_img_barrier_all(test_cb);
    VkBufferImageCopy region = {0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                               {0}, {OUT, OUT, 1}};
    vkCmdCopyImageToBuffer(test_cb, target->texture.img, VK_IMAGE_LAYOUT_GENERAL, pixels, 1, &region);
    complete();
}

static void guest_bilinear(const spatial_image *input, const spatial_image *target,
                           VkImageLayout target_layout) {
    const int crop[] = {0, 0, SRC, SRC};
    barrier(test_cb, target->texture.img, target_layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    spatial_blit(test_cb, input->texture.img, target->texture.img, crop, OUT, OUT, 0, VK_FILTER_LINEAR);
    barrier(test_cb, target->texture.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
}

// Model the insertion transaction for a synthetic draw with known full coverage.
// Allocate privately and commit the completed image to the same guest target.
static int guest_insert(const spatial_image *input, spatial_image *target, unsigned coverage_w,
                        VkPipeline alpha_only, VkPipelineLayout layout, const VkDescriptorSet sets[2]) {
    if (coverage_w != OUT || input->texture.img == target->texture.img ||
        input->texture.fmt != target->texture.fmt) return 0;
    spatial_image saved = *target;
    *target = (spatial_image){0};
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    int ready = spatial_image_ready(SP_OUTPUT, OUT, OUT, usage, g_sfmt);
    spatial_image scratch = *target;
    *target = saved;
    if (!ready) return 0;

    begin();
    guest_bilinear(input, &scratch, VK_IMAGE_LAYOUT_UNDEFINED);
    guest_draw(alpha_only, layout, sets, &scratch, VK_ATTACHMENT_LOAD_OP_LOAD, OUT);
    vk_img_barrier_all(test_cb);
    VkImageSubresourceLayers color = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    VkImageCopy copy = {color, {0}, color, {0}, {OUT, OUT, 1}};
    vkCmdCopyImage(test_cb, scratch.texture.img, VK_IMAGE_LAYOUT_GENERAL,
                   target->texture.img, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
    complete();
    spatial_image_drop(&scratch);
    return 1;
}

static void check_guest_enlargement(void) {
    VkFormat saved_format = g_sfmt;
    g_sfmt = VK_FORMAT_R8G8B8A8_UNORM;
    spatial_image input = source_image(SRC);
    spatial_image overlay = source_image(SRC);
    float vertex_constants[] = {1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 1, 1};
    const float edge_luma[] = {0.7151687f, 0.0721923f, 0, 0};
    const float vertices[][8] = {
        {0, 1, 0, 1, 0, 0, 0, 0},
        {2, 1, 0, 1, 2, 0, 0, 0},
        {0, -1, 0, 1, 0, 2, 0, 0},
    };
    memcpy(mapped + GUEST_VS_UBO, vertex_constants, sizeof vertex_constants);
    memcpy(mapped + GUEST_EDGE_UBO, edge_luma, sizeof edge_luma);
    memcpy(mapped + GUEST_VERTEX_OFFSET, vertices, sizeof vertices);
    // Vary source alpha independently of RGB so the replacement cannot pass
    // merely because it preserves one constant that matches the guest output.
    for (unsigned y = 0; y < SRC; ++y)
        for (unsigned x = 0; x < SRC; ++x)
            mapped[(y * SRC + x) * 4 + 3] = (x < SRC / 2) ? 73 : 173;
    begin();
    barrier(test_cb, input.texture.img, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy source_region = {0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0}, {SRC, SRC, 1}};
    vkCmdCopyBufferToImage(test_cb, pixels, input.texture.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &source_region);
    barrier(test_cb, input.texture.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    complete();

    // Make the overlay visually distinct while preserving its zero output alpha.
    VkClearColorValue magenta = {.float32 = {1, 0, 1, 1}};
    VkImageSubresourceRange full = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    begin();
    vkCmdClearColorImage(test_cb, overlay.texture.img, VK_IMAGE_LAYOUT_GENERAL, &magenta, 1, &full);
    complete();

    VkDescriptorSetLayoutBinding vs = {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT};
    VkDescriptorSetLayoutBinding fs[] = {
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
    };
    VkDescriptorSetLayoutCreateInfo vi = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, 1, &vs};
    VkDescriptorSetLayoutCreateInfo fi = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, 3, fs};
    VkDescriptorSetLayout layouts[2];
    CHECK(vkCreateDescriptorSetLayout(V.dev, &vi, NULL, &layouts[0]));
    CHECK(vkCreateDescriptorSetLayout(V.dev, &fi, NULL, &layouts[1]));
    VkPipelineLayoutCreateInfo li = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 2, layouts};
    VkPipelineLayout layout;
    CHECK(vkCreatePipelineLayout(V.dev, &li, NULL, &layout));
    VkDescriptorSetLayoutBinding grid_binding={0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT};
    VkDescriptorSetLayoutCreateInfo gi={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,NULL,0,1,&grid_binding};
    VkDescriptorSetLayout grid_layout;
    CHECK(vkCreateDescriptorSetLayout(V.dev,&gi,NULL,&grid_layout));
    VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 5},
                                   {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3}};
    VkDescriptorPoolCreateInfo pi = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, 4, 2, sizes};
    VkDescriptorPool pool;
    CHECK(vkCreateDescriptorPool(V.dev, &pi, NULL, &pool));
    VkDescriptorSetLayout set_layouts[] = {layouts[0], layouts[1], layouts[1],grid_layout};
    VkDescriptorSet sets[4];
    VkDescriptorSetAllocateInfo alloc = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                        NULL, pool, 4, set_layouts};
    CHECK(vkAllocateDescriptorSets(V.dev, &alloc, sets));
    VkSamplerCreateInfo si = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR, .minFilter = VK_FILTER_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
    VkSampler sampler;
    CHECK(vkCreateSampler(V.dev, &si, NULL, &sampler));
    assert(GUEST_VS_UBO % V.props.limits.minUniformBufferOffsetAlignment == 0);
    assert(GUEST_FS_UBO % V.props.limits.minUniformBufferOffsetAlignment == 0);
    assert(GUEST_EDGE_UBO % V.props.limits.minUniformBufferOffsetAlignment == 0);
    VkDescriptorBufferInfo edge_buf = {pixels, GUEST_EDGE_UBO, sizeof edge_luma};
    VkDescriptorBufferInfo vb = {pixels, GUEST_VS_UBO, sizeof vertex_constants};
    VkDescriptorBufferInfo fb = {pixels, GUEST_FS_UBO, 8 * sizeof(float)};
    VkDescriptorImageInfo sampled = {sampler, input.view, VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorImageInfo over = {sampler, overlay.view, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet writes[] = {
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, sets[0], 1, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, NULL, &vb},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, sets[1], 2, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, NULL, &fb},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, sets[2], 2, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, NULL, &fb},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, sets[1], 3, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, NULL, &edge_buf},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, sets[1], 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &sampled},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, sets[2], 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &over},
    };
    vkUpdateDescriptorSets(V.dev, sizeof writes / sizeof writes[0], writes, 0, NULL);
    VkPipeline edge = guest_pipeline(layout, guest_edge_frag_spv, sizeof guest_edge_frag_spv, VK_FALSE, 0xf, 0);
    VkPipeline smooth = guest_pipeline(layout, guest_smooth_frag_spv, sizeof guest_smooth_frag_spv, VK_FALSE, 0xf, 0);
    VkPipeline compose = guest_pipeline(layout, guest_smooth_frag_spv, sizeof guest_smooth_frag_spv, VK_TRUE, 0xf, 0);
    VkPipeline alpha_only = guest_pipeline(layout, guest_smooth_frag_spv, sizeof guest_smooth_frag_spv,
                                           VK_FALSE, VK_COLOR_COMPONENT_A_BIT, 0);
    assert(spatial_image_ready(SP_OUTPUT, OUT, OUT,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, g_sfmt));
    spatial_image *target = &g_spatial.image[SP_OUTPUT];
    begin();
    guest_bilinear(&input, target, VK_IMAGE_LAYOUT_UNDEFINED);
    guest_readback(target);
    uint8_t baseline[OUT * OUT * 4], guest[sizeof baseline];
    memcpy(baseline, mapped, sizeof baseline);

    float uv_min = 0.5f / SRC, uv_max = 1 - uv_min;
    const float constants[][8] = {
        {1.0f / SRC, 1.0f / SRC, uv_min, uv_min, uv_max, uv_max, 0, 0},
        {SRC, SRC, 1.0f / SRC, 1.0f / SRC, uv_min, uv_min, uv_max, uv_max},
    };
    const VkPipeline pipelines[] = {edge, smooth};
    for (unsigned i = 0; i < 2; ++i) {
        memcpy(mapped + GUEST_FS_UBO, constants[i], sizeof constants[i]);
        begin();
        VkAttachmentLoadOp load = i ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
        guest_draw(pipelines[i], layout, sets, target, load, OUT);
        guest_readback(target);
        memcpy(guest, mapped, sizeof guest);
        unsigned changed = 0;
        for (unsigned p = 0; p < sizeof guest; p += 4) {
            assert(guest[p + 3] == 0 && baseline[p + 3] >= 73);
            changed += abs((int)guest[p] - baseline[p]) > 2 ||
                       abs((int)guest[p + 1] - baseline[p + 1]) > 2;
            assert(abs((int)guest[p + 2] - 76) <= 1);
        }
        assert(changed > 0);
        printf("guest %s: %u pixels differ from bilinear, RGB DC preserved, alpha zero\n",
               i ? "smoothstep" : "edge-directed", changed);

        memcpy(mapped + GUEST_FS_UBO, constants[1], sizeof constants[1]);
        const VkDescriptorSet overlay_sets[] = {sets[0], sets[2]};
        begin();
        guest_draw(compose, layout, overlay_sets, target, VK_ATTACHMENT_LOAD_OP_LOAD, OUT);
        guest_readback(target);
        assert(memcmp(mapped, guest, sizeof guest) == 0);
        begin();
        guest_bilinear(&input, target, VK_IMAGE_LAYOUT_GENERAL);
        guest_draw(compose, layout, overlay_sets, target, VK_ATTACHMENT_LOAD_OP_LOAD, OUT);
        guest_readback(target);
        assert(mapped[2] > baseline[2] && mapped[3] == baseline[3]);

        // With this fixture's proven coverage, retaining the guest's zero
        // alpha write keeps the later blend independent of reconstructed RGB.
        assert(guest_insert(&input, target, OUT, alpha_only, layout, sets));
        begin();
        guest_draw(compose, layout, overlay_sets, target, VK_ATTACHMENT_LOAD_OP_LOAD, OUT);
        guest_readback(target);
        for (unsigned p = 0; p < sizeof guest; p += 4) {
            assert(memcmp(mapped + p, baseline + p, 3) == 0);
            assert(mapped[p + 3] == 0);
        }
    }
    puts("guest enlargement alpha-sensitive composition readback PASS");
    fail_spatial_allocation = 1;
    assert(!guest_insert(&input, target, OUT, alpha_only, layout, sets) && !fail_spatial_allocation);
    assert(!guest_insert(target, target, OUT, alpha_only, layout, sets));
    assert(!guest_insert(&input, target, OUT - 1, alpha_only, layout, sets));
    begin();
    guest_draw(smooth, layout, sets, target, VK_ATTACHMENT_LOAD_OP_LOAD, OUT);
    guest_readback(target);
    assert(memcmp(mapped, guest, sizeof guest) == 0);
    puts("guest enlargement allocation and admission fallback readback PASS");
    begin();
    vkCmdClearColorImage(test_cb, target->texture.img, VK_IMAGE_LAYOUT_GENERAL, &magenta, 1, &full);
    vk_img_barrier_all(test_cb);
    guest_draw(smooth, layout, sets, target, VK_ATTACHMENT_LOAD_OP_LOAD, OUT - 1);
    guest_readback(target);
    const uint8_t sentinel[] = {255, 0, 255, 255};
    for (unsigned y = 0; y < OUT; ++y) {
        size_t last = (y * OUT + OUT - 1) * 4;
        assert(mapped[last - 1] == 0);
        assert(memcmp(mapped + last, sentinel, sizeof sentinel) == 0);
    }
    puts("guest enlargement partial LOAD coverage readback PASS");
    // Use the runtime patch on the supplied shader with the original
    // coverage and a loaded guest attachment.
    uint32_t patched[sizeof guest_smooth_frag_spv / sizeof(uint32_t)];
    memcpy(patched, guest_smooth_frag_spv, sizeof patched);
    assert(dm_guest_bilinear_patch(patched, sizeof patched / sizeof patched[0]));
    VkPipeline guest_linear = guest_pipeline(layout, patched, sizeof patched, VK_FALSE, 0xf, 0);
    begin();
    vkCmdClearColorImage(test_cb, target->texture.img, VK_IMAGE_LAYOUT_GENERAL, &magenta, 1, &full);
    vk_img_barrier_all(test_cb);
    guest_draw(guest_linear, layout, sets, target, VK_ATTACHMENT_LOAD_OP_LOAD, OUT - 1);
    guest_readback(target);
    for (unsigned y = 0; y < OUT; ++y) {
        for (unsigned x = 0; x < OUT - 1; ++x) {
            size_t at = (y * OUT + x) * 4;
            for (unsigned c = 0; c < 3; ++c)
                assert(abs((int)mapped[at + c] - baseline[at + c]) <= 1);
            assert(mapped[at + 3] == 0);
        }
        size_t last = (y * OUT + OUT - 1) * 4;
        assert(memcmp(mapped + last, sentinel, sizeof sentinel) == 0);
    }
    puts("guest Bilinear shader variant preserves raster coverage and alpha PASS");
    VkPushConstantRange pc = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, 3 * sizeof(float)};
    li.pushConstantRangeCount = 1;
    li.pPushConstantRanges = &pc;
    VkDescriptorSetLayout spatial_layouts[]={layouts[0],layouts[1],grid_layout};
    li.setLayoutCount=3;li.pSetLayouts=spatial_layouts;
    VkPipelineLayout spatial_layout;
    CHECK(vkCreatePipelineLayout(V.dev, &li, NULL, &spatial_layout));
    // The original sampler is deliberately incompatible with the
    // reconstruction filter; the independent set keeps the real clamp.
    VkSamplerCreateInfo original_si=si;
    original_si.magFilter=original_si.minFilter=VK_FILTER_NEAREST;
    original_si.addressModeU=original_si.addressModeV=VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSampler original;
    CHECK(vkCreateSampler(V.dev,&original_si,NULL,&original));
    VkDescriptorImageInfo original_sample={original,input.view,VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet independent_writes[]={
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,NULL,sets[1],1,0,1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,&original_sample},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,NULL,sets[3],0,0,1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,&sampled},
    };
    vkUpdateDescriptorSets(V.dev,2,independent_writes,0,NULL);
    VkDescriptorSet spatial_sets[]={sets[0],sets[1],sets[3]};
    spatial_image guest_target = *target;
    *target = (spatial_image){0};
    int saved_mode = selected_mode, saved_sharpness = sharpness;
    uint64_t ticket = 0;
    const int source_crop[] = {0, 0, SRC, SRC};
    for (int mode = SP_BILINEAR; mode <= SP_SGSR1; ++mode) {
        VkPipeline pipeline = guest_pipeline(spatial_layout, guest_spatial_frag_spv,
            sizeof guest_spatial_frag_spv, VK_FALSE, 0xf,
            mode==SP_BILINEAR?SP_GUEST_GRID_BILINEAR:(unsigned)mode);
        selected_mode = mode;
        uint8_t unsharpened[sizeof baseline];
        uint64_t previous = 0;
        for (sharpness = 0; sharpness <= (mode==SP_BILINEAR?0:100); sharpness += 25) {
            spatial_image *reference = spatial_reconstruct(&input.texture, source_crop, 0, OUT, OUT, ++ticket, NULL);
            assert(reference && spatial_active == mode);
            begin();
            guest_readback(reference);
            memcpy(baseline, mapped, sizeof baseline);

            begin();
            float constants[] = {OUT, OUT, sharpness / 100.0f};
            vkCmdPushConstants(test_cb, spatial_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof constants, constants);
            vkCmdClearColorImage(test_cb, guest_target.texture.img, VK_IMAGE_LAYOUT_GENERAL, &magenta, 1, &full);
            vk_img_barrier_all(test_cb);
            guest_draw_bound(pipeline, spatial_layout, spatial_sets,3,&guest_target,VK_ATTACHMENT_LOAD_OP_LOAD, OUT - 1);
            guest_readback(&guest_target);
            unsigned max_error = 0;
            for (unsigned y = 0; y < OUT; ++y) {
                for (unsigned x = 0; x < OUT - 1; ++x) {
                    size_t at = (y * OUT + x) * 4;
                    for (unsigned c = 0; c < 3; ++c) {
                        unsigned error = (unsigned)abs((int)mapped[at + c] - baseline[at + c]);
                        if (error > max_error) max_error = error;
                    }
                    assert(mapped[at + 3] == 0);
                }
                size_t last = (y * OUT + OUT - 1) * 4;
                assert(memcmp(mapped + last, sentinel, sizeof sentinel) == 0);
            }
            fprintf(stderr, "guest %s at %d%% max RGB error %u\n", spatial_mode_name(mode), sharpness, max_error);
            assert(max_error <= 2);
            if (!sharpness) memcpy(unsharpened, mapped, sizeof unsharpened);
            uint64_t distance = slider_distance(unsharpened, mapped, sizeof unsharpened);
            assert(distance >= previous);
            if (sharpness == 25) assert(distance);
            previous = distance;
        }
        // A narrowed guest crop rejects adaptive taps. The fallback preserves
        // the original shader's coordinate warp, mask and alpha.
        float cropped[8];
        memcpy(cropped, constants[1], sizeof cropped);
        cropped[4] = 0.2f;
        memcpy(mapped + GUEST_FS_UBO, cropped, sizeof cropped);
        begin();
        guest_draw(smooth, layout, sets, &guest_target, VK_ATTACHMENT_LOAD_OP_LOAD, OUT - 1);
        guest_readback(&guest_target);
        memcpy(guest, mapped, sizeof guest);
        begin();
        float draw_params[] = {OUT, OUT, 0.5f};
        vkCmdPushConstants(test_cb, spatial_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof draw_params, draw_params);
        guest_draw_bound(pipeline, spatial_layout, spatial_sets,3,&guest_target,VK_ATTACHMENT_LOAD_OP_LOAD, OUT - 1);
        guest_readback(&guest_target);
        assert(memcmp(guest, mapped, sizeof guest) == 0);
        printf("guest %s independent sampler, reference and fallback PASS\n",spatial_mode_name(mode));
        memcpy(mapped + GUEST_FS_UBO, constants[1], sizeof constants[1]);
        vkDestroyPipeline(V.dev, pipeline, NULL);
    }
    // Restore original fixture's clamp for the existing temporal test.
    vkUpdateDescriptorSets(V.dev,1,&writes[4],0,NULL);
    vkDestroySampler(V.dev,original,NULL);
#ifdef SGSR2_HOST_PROBE
    if (vk_gsr2_variants() & DM_TEMPORAL_2FS) {
        const VkExtent2D scene_size = {SRC, SRC}, target_size = {OUT, OUT};
        vk_gsr2 *temporal = vk_gsr2_create_replay(scene_size, target_size, 4 * 1024 * 1024);
        assert(temporal);
        VkDescriptorSetLayout sampling_layout = vk_gsr2_create_replay_layout();
        assert(vk_gsr2_replay_bindings(temporal, sampling_layout));
        VkDescriptorSetLayout temporal_layouts[] = {layouts[0], layouts[1], sampling_layout};
        VkPipelineLayout temporal_layout;
        li.setLayoutCount = 3;
        li.pSetLayouts = temporal_layouts;
        CHECK(vkCreatePipelineLayout(V.dev, &li, NULL, &temporal_layout));
        VkPipeline pipeline = guest_pipeline(temporal_layout, guest_temporal_frag_spv,
            sizeof guest_temporal_frag_spv, VK_FALSE, 0xf, 0);
        dm_temporal_frame frame = {.ticket = 1, .stream = 1, .epoch = 1, .iw = SRC, .ih = SRC,
            .ow = OUT, .oh = OUT, .inputs = DM_TEMPORAL_IMAGE_PAIR, .variants = DM_TEMPORAL_2FS,
            .real = 1, .memory_ok = 1};
        begin();
        vk_gsr2_output result;
        assert(vk_gsr2_record_replay(temporal, test_cb, 0, &frame, input.texture.img, NULL, &result));
        VkDescriptorSet temporal_sets[] = {sets[0], sets[1], vk_gsr2_replay_sample(temporal, 0, &result)};
        const float params[] = {OUT, OUT, 0};
        vkCmdPushConstants(test_cb, temporal_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof params, params);
        vkCmdClearColorImage(test_cb, guest_target.texture.img, VK_IMAGE_LAYOUT_GENERAL, &magenta, 1, &full);
        vk_img_barrier_all(test_cb);
        guest_draw_bound(pipeline, temporal_layout, temporal_sets, 3, &guest_target,
                         VK_ATTACHMENT_LOAD_OP_LOAD, OUT - 1);
        vk_img_barrier_all(test_cb);
        const VkDescriptorSet temporal_overlay[] = {sets[0], sets[2]};
        guest_draw(compose, layout, temporal_overlay, &guest_target, VK_ATTACHMENT_LOAD_OP_LOAD, OUT);
        guest_readback(&guest_target);
        vk_gsr2_commit(temporal, 1);
        memcpy(guest, mapped, sizeof guest);

        // A separate transfer reads the temporal result before the guest
        // fragment's alpha policy and subsequent composition are applied.
        begin();
        VkBufferImageCopy reference = {0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            {0}, {OUT, OUT, 1}};
        vkCmdCopyImageToBuffer(test_cb, result.image, result.layout, pixels, 1, &reference);
        complete();
        const uint16_t *fp16 = (const uint16_t *)mapped;
        for (unsigned y = 0; y < OUT; ++y) {
            for (unsigned x = 0; x < OUT - 1; ++x) {
                unsigned i = (y * OUT + x) * 4;
                for (unsigned c = 0; c < 3; ++c) {
                    float value = fminf(fmaxf(unpack_fp16(fp16[i + c]), 0.0f), 1.0f);
                    int expected = (int)lroundf(value * 255.0f);
                    if (abs((int)guest[i + c] - expected) > 1)
                        fprintf(stderr, "temporal result (%u,%u) channel %u: %u, expected %d\n",
                                x, y, c, guest[i + c], expected);
                    assert(abs((int)guest[i + c] - expected) <= 1);
                }
                assert(guest[i + 3] == 0);
            }
            assert(memcmp(guest + (y * OUT + OUT - 1) * 4, sentinel, sizeof sentinel) == 0);
        }

        // An incompatible mapping must retain the original smoothstep.
        memcpy(mapped + GUEST_VS_UBO, vertex_constants, sizeof vertex_constants);
        memcpy(mapped + GUEST_VERTEX_OFFSET, vertices, sizeof vertices);
        float cropped[8];
        memcpy(cropped, constants[1], sizeof cropped);
        cropped[4] = 0.2f;
        memcpy(mapped + GUEST_FS_UBO, cropped, sizeof cropped);
        begin();
        guest_draw(smooth, layout, sets, &guest_target, VK_ATTACHMENT_LOAD_OP_LOAD, OUT - 1);
        guest_readback(&guest_target);
        memcpy(guest, mapped, sizeof guest);
        begin();
        vkCmdPushConstants(test_cb, temporal_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof params, params);
        guest_draw_bound(pipeline, temporal_layout, temporal_sets, 3, &guest_target,
                         VK_ATTACHMENT_LOAD_OP_LOAD, OUT - 1);
        guest_readback(&guest_target);
        assert(memcmp(guest, mapped, sizeof guest) == 0);
        // Keep the derivative but shift the UV phase by a fraction of an
        // output pixel. A temporal result uses a discrete pixel grid.
        memcpy(mapped + GUEST_FS_UBO, constants[1], sizeof constants[1]);
        float shifted[3][8];
        memcpy(shifted, vertices, sizeof shifted);
        for (unsigned i = 0; i < 3; i++) shifted[i][4] += 0.25f / OUT;
        memcpy(mapped + GUEST_VERTEX_OFFSET, shifted, sizeof shifted);
        begin();
        guest_draw(smooth, layout, sets, &guest_target, VK_ATTACHMENT_LOAD_OP_LOAD, OUT - 1);
        guest_readback(&guest_target);
        memcpy(guest, mapped, sizeof guest);
        begin();
        vkCmdPushConstants(test_cb, temporal_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof params, params);
        guest_draw_bound(pipeline, temporal_layout, temporal_sets, 3, &guest_target,
                         VK_ATTACHMENT_LOAD_OP_LOAD, OUT - 1);
        guest_readback(&guest_target);
        assert(memcmp(guest, mapped, sizeof guest) == 0);
        vkDestroyPipeline(V.dev, pipeline, NULL);
        vkDestroyPipelineLayout(V.dev, temporal_layout, NULL);
        vk_gsr2_destroy(temporal);
        vkDestroyDescriptorSetLayout(V.dev, sampling_layout, NULL);
        puts("guest GSR 2 replay sampling, composition and mapping fallback PASS");
    }
#endif
    selected_mode = saved_mode;
    sharpness = saved_sharpness;
    spatial_drop_images();
    *target = guest_target;
    vkDestroyPipelineLayout(V.dev, spatial_layout, NULL);
    vkDestroyPipeline(V.dev, guest_linear, NULL);
    vkDestroyPipeline(V.dev, alpha_only, NULL);
    vkDestroyPipeline(V.dev, compose, NULL);
    vkDestroyPipeline(V.dev, smooth, NULL);
    vkDestroyPipeline(V.dev, edge, NULL);
    vkDestroyPipelineLayout(V.dev, layout, NULL);
    vkDestroyDescriptorPool(V.dev, pool, NULL);
    vkDestroyDescriptorSetLayout(V.dev,grid_layout,NULL);
    vkDestroyDescriptorSetLayout(V.dev, layouts[1], NULL);
    vkDestroyDescriptorSetLayout(V.dev, layouts[0], NULL);
    vkDestroySampler(V.dev, sampler, NULL);
    spatial_image_drop(&overlay);
    spatial_image_drop(&input);
    spatial_image_drop(target);
    g_sfmt = saved_format;
}
