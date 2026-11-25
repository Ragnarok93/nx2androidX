// Shared loader and readback setup is in vk_spatial_upscaler.c.
#include "gfx_vk_gsr2.c"

enum { TEST_IW = 17, TEST_IH = 19, TEST_OW = 29, TEST_OH = 31, TEST_WORDS = TEST_OW * TEST_OH * 4 };
static const VkExtent2D render_size = {TEST_IW, TEST_IH}, output_size = {TEST_OW, TEST_OH};

static spatial_image make_input(VkFormat format, VkImageAspectFlags aspect) {
    spatial_image image = {0};
    VkImageCreateInfo create = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, 0, VK_IMAGE_TYPE_2D,
        format, {TEST_IW, TEST_IH, 1}, 1, 1, VK_SAMPLE_COUNT_1_BIT,
        VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT};
    CHECK(vkCreateImage(V.dev, &create, NULL, &image.texture.img));
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(V.dev, image.texture.img, &req);
    uint32_t type = vk_mem_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
    assert(type != UINT32_MAX);
    VkMemoryAllocateInfo alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, req.size, type};
    CHECK(vkAllocateMemory(V.dev, &alloc, NULL, &image.texture.mem));
    CHECK(vkBindImageMemory(V.dev, image.texture.img, image.texture.mem, 0));
    VkImageViewCreateInfo view = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, image.texture.img,
        VK_IMAGE_VIEW_TYPE_2D, format, {0}, {aspect, 0, 1, 0, 1}};
    CHECK(vkCreateImageView(V.dev, &view, NULL, &image.view));
    return image;
}

static void update_scene(const spatial_image images[3], int frame, int reversed_depth, int float_motion, int initialized) {
    unsigned size = TEST_IW * TEST_IH * 4;
    unsigned motion_offset = float_motion ? (2 * size + 15) & ~15u : 2 * size;
    float *signed_motion = (float *)(mapped + motion_offset);
    for (int y = 0; y < TEST_IH; y++) {
        for (int x = 0; x < TEST_IW; x++) {
            unsigned i = (y * TEST_IW + x) * 4;
            int subject = x >= 4 + frame && x < 10 + frame && y >= 4 && y < 13;
            int lattice = (x + y + frame) & 1;
            mapped[i] = subject ? 108 + 18 * lattice : 24 + 6 * lattice;
            mapped[i + 1] = subject ? 82 : 30;
            mapped[i + 2] = subject ? 64 : 36;
            mapped[i + 3] = 255;
            unsigned depth = subject ? 51 : 185;
            mapped[size + i] = reversed_depth ? 255 - depth : depth;
            mapped[size + i + 1] = mapped[size + i + 2] = mapped[size + i + 3] = 0;
            if (float_motion) {
                const float bias = 32767.0f / 65535.0f;
                const float inv = 1.0f / (0.499f * 0.5f);
                signed_motion[i] = -(135.0f / 255 * inv - bias * inv) * (TEST_IW * 0.5f);
                signed_motion[i + 1] = (127.0f / 255 * inv - bias * inv) * (TEST_IH * 0.5f);
                signed_motion[i + 2] = 0;
                signed_motion[i + 3] = subject ? 1 : 0;
            } else {
                mapped[2 * size + i] = subject ? 135 : 0;
                mapped[2 * size + i + 1] = subject ? 127 : 0;
                mapped[2 * size + i + 2] = mapped[2 * size + i + 3] = 0;
            }
        }
    }
    begin();
    for (unsigned i = 0; i < 3; i++) {
        VkImage image = images[i].texture.img;
        barrier(test_cb, image, (initialized & (1 << i)) ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy copy = {i == 2 ? motion_offset : i * size, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            {0}, {TEST_IW, TEST_IH, 1}};
        vkCmdCopyBufferToImage(test_cb, pixels, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        barrier(test_cb, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    }
    complete();
}

static float unpack_fp16(uint16_t word) {
    int e = (word >> 10) & 31;
    float mantissa = (float)(word & 1023) / 1024;
    return (word & 0x8000 ? -1 : 1) * ldexpf((e != 0) + mantissa, e ? e - 15 : -14);
}

static void verify_temporal_output(uint16_t rgb[TEST_WORDS]) {
    memcpy(rgb, mapped, sizeof(uint16_t) * TEST_WORDS);
    for (unsigned i = 0; i < TEST_WORDS; i += 4)
        for (unsigned channel = 0; channel < 3; channel++)
            assert((rgb[i + channel] & 0x7c00) != 0x7c00); // finite RGB
    unsigned foreground = (TEST_OW * 14 + 14) * 4;
    unsigned background = (TEST_OW + 1) * 4;
    assert(unpack_fp16(rgb[foreground]) > unpack_fp16(rgb[background]) + 0.05f); // distinct linear-red geometry
}

static void draw_temporal(vk_gsr2 *ctx, dm_temporal_frame *frame, const vk_gsr2_inputs *inputs,
                          VkQueryPool timing, uint16_t results[TEST_WORDS]) {
    begin();
    vk_gsr2_timing queries = {timing, 0};
    vk_gsr2_output output;
    assert(vk_gsr2_record(ctx, test_cb, frame->ticket % VK_GSR2_SLOTS, frame, inputs, &queries, &output));
    assert(output.extent.width == TEST_OW && output.extent.height == TEST_OH);
    VkBufferImageCopy copy = {0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        {0}, {TEST_OW, TEST_OH, 1}};
    vkCmdCopyImageToBuffer(test_cb, output.image, output.layout, pixels, 1, &copy);
    vk_img_barrier_all(test_cb);
    complete();
    vk_gsr2_commit(ctx, 1);
    verify_temporal_output(results);
}

enum { MOTION_WORDS = TEST_IW * TEST_IH * 4 };

static void read_conversion(vk_gsr2 *ctx, dm_temporal_frame *frame, const vk_gsr2_inputs *in,
                            uint16_t result[MOTION_WORDS]) {
    begin();
    vk_gsr2_output output;
    assert(vk_gsr2_record(ctx, test_cb, frame->ticket % VK_GSR2_SLOTS, frame, in, NULL, &output));
    temporal_image *converted = &ctx->images[ctx->spec[0].target];
    vk_img_barrier_all(test_cb);
    VkBufferImageCopy copy = {0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        {0}, {TEST_IW, TEST_IH, 1}};
    vkCmdCopyImageToBuffer(test_cb, converted->image, VK_IMAGE_LAYOUT_GENERAL, pixels, 1, &copy);
    vk_img_barrier_all(test_cb);
    complete();
    vk_gsr2_commit(ctx, 1);
    memcpy(result, mapped, MOTION_WORDS * sizeof(*result));
}

static vk_gsr2_camera test_camera(float dx, float jitter_x, float jitter_y) {
    // Y-down, reversed depth, and a perspective W=Z. Panning affects screen X
    // by a depth-dependent amount, unlike testing with an affine identity.
    vk_gsr2_camera camera = {.world_to_clip = {
        2, 0, 0, 0, 0, -3, 0, 0, jitter_x, jitter_y, -0.125f, 1, dx, 0, 0.125f, 0},
        .jitter_ndc = {jitter_x, jitter_y}};
    return camera;
}

static void check_depth_aspect(vk_gsr2 *ctx, dm_temporal_frame frame, vk_gsr2_inputs in) {
    VkFormat format = VK_FORMAT_D24_UNORM_S8_UINT;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImageFormatProperties properties;
    if (vkGetPhysicalDeviceImageFormatProperties(V.pd, format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
            usage, 0, &properties) != VK_SUCCESS) {
        puts("SGSR 2 sampled D24S8 probe skipped: format unavailable");
        return;
    }
    spatial_image depth = make_input(format, VK_IMAGE_ASPECT_DEPTH_BIT);
    VkImageSubresourceRange range = {VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1};
    begin();
    VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, NULL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
        depth.texture.img, range};
    vkCmdPipelineBarrier(test_cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 1, &barrier);
    VkClearDepthStencilValue clear = {0.375f, 0};
    vkCmdClearDepthStencilImage(test_cb, depth.texture.img, VK_IMAGE_LAYOUT_GENERAL, &clear, 1, &range);
    vk_img_barrier_all(test_cb);
    complete();

    in.depth.view = depth.view;
    uint16_t converted[MOTION_WORDS];
    frame.ticket++;
    read_conversion(ctx, &frame, &in, converted);
    float x = 2 * (0.5f - in.jitter[0]) / TEST_IW - 1;
    float motion = -0.25f * x + 0.2f * (1.125f - 0.625f) / 0.125f;
    assert(fabsf(unpack_fp16(converted[0]) - motion) < 0.002f);
    spatial_image_drop(&depth);
    puts("SGSR 2 sampled D24S8 depth-aspect reprojection passed");
}

static void check_input_formats(vk_gsr2 *ctx, dm_temporal_frame frame, vk_gsr2_inputs in,
                                const spatial_image images[3]) {
    vk_gsr2_camera cur = test_camera(0, 0.5f / TEST_IW, -0.25f / TEST_IH);
    vk_gsr2_camera prev = test_camera(-0.2f, -0.3f / TEST_IW, 0.1f / TEST_IH);
    prev.world_to_clip[0] = 2.5f;
    vk_gsr2_replay_constants replay = {
        .view = {
            .projection = {2, 0, 0, 0, 0, -3, 0, 0, 0, 0, -0.125f, 1, 0, 0, 0.125f, 0},
            .current_pre_exposure = 2, .previous_pre_exposure = 3,
            .clip_y_down = 1,
        },
    };
    vk_gsr2_camera base_cur = test_camera(0, 0, 0), base_prev = test_camera(-0.2f, 0, 0);
    base_prev.world_to_clip[0] = 2.5f;
    memcpy(replay.current.jittered, cur.world_to_clip, sizeof replay.current.jittered);
    memcpy(replay.current.unjittered, base_cur.world_to_clip, sizeof replay.current.unjittered);
    memcpy(replay.previous.jittered, prev.world_to_clip, sizeof replay.previous.jittered);
    memcpy(replay.previous.unjittered, base_prev.world_to_clip, sizeof replay.previous.unjittered);
    // Vulkan receives jitter relative to the actual render extent, including
    // when that extent is smaller than the producer's logical viewport.
    vk_gsr2_inputs half = in;
    assert(vk_gsr2_set_replay_constants(&half, (VkExtent2D){8, 9}, &replay));
    assert(fabsf(half.jitter[0] - 0.25f * 8 / TEST_IW) < 1e-6f);
    assert(vk_gsr2_set_replay_constants(&in, render_size, &replay));
    assert(fabsf(in.near_plane - (1.0f / 9)) < 1e-6f);
    assert(in.tan_half_fov_x == 0.5f && in.pre_exposure == 1.5f);
    assert(in.depth_convention == VK_GSR2_DEPTH_REVERSED);
    vk_gsr2_replay_constants invalid_replay = replay;
    invalid_replay.current.jittered[12] += 0.01f; // off-axis translation, no matching W
    assert(!vk_gsr2_set_replay_constants(&in, render_size, &invalid_replay));
    invalid_replay = replay;
    invalid_replay.current.jittered[10] *= 2;
    assert(!vk_gsr2_set_replay_constants(&in, render_size, &invalid_replay));
    invalid_replay = replay;
    invalid_replay.view.projection[14] *= -1; // contradictory depth coefficients
    assert(!vk_gsr2_set_replay_constants(&in, render_size, &invalid_replay));
    invalid_replay = replay;
    invalid_replay.view.current_pre_exposure = 0;
    assert(!vk_gsr2_set_replay_constants(&in, render_size, &invalid_replay));
    assert(in.depth_convention == VK_GSR2_DEPTH_REVERSED && in.pre_exposure == 1.5f);
    vk_gsr2_replay_constants forward = replay;
    forward.view.projection[10] = 1 - replay.view.projection[10];
    forward.view.projection[14] = -replay.view.projection[14];
    vk_gsr2_replay_camera *frames[] = {&forward.current, &forward.previous};
    for (unsigned f = 0; f < 2; f++)
        for (unsigned c = 0; c < 16; c += 4) {
            frames[f]->jittered[c + 2] = frames[f]->jittered[c + 3] - frames[f]->jittered[c + 2];
            frames[f]->unjittered[c + 2] = frames[f]->unjittered[c + 3] - frames[f]->unjittered[c + 2];
        }
    vk_gsr2_inputs forward_inputs = in;
    assert(vk_gsr2_set_replay_constants(&forward_inputs, render_size, &forward));
    assert(forward_inputs.depth_convention == VK_GSR2_DEPTH_STANDARD);
    assert(fabsf(forward_inputs.near_plane - in.near_plane) < 1e-6f);
    // The rest of this test compares motion encodings, with exposure held
    // constant across its two independent test sequences.
    replay.view.previous_pre_exposure = replay.view.current_pre_exposure;
    assert(vk_gsr2_set_replay_constants(&in, render_size, &replay));
    assert(fabsf(in.jitter[0] - 0.25f) < 1e-6f && fabsf(in.jitter[1] + 0.125f) < 1e-6f);
    assert(!in.same_camera);
    float guest_clip[16] = {1.25f, 0, 0, 0, 0, 1, 0, 0, -1.6f, 0, 1, 0, -0.2f, 0, 0, 1};
    vk_gsr2_inputs direct = in;
    assert(vk_gsr2_set_replay_clip(&direct, render_size, &replay.view, guest_clip,
                                   cur.jitter_ndc, prev.jitter_ndc, 0));
    for (unsigned i = 0; i < 16; i++) assert(fabsf(direct.clip_to_prev[i] - in.clip_to_prev[i]) < 1e-6f);
    guest_clip[12] += prev.jitter_ndc[0] - 1.25f * cur.jitter_ndc[0];
    guest_clip[13] += prev.jitter_ndc[1] - cur.jitter_ndc[1];
    assert(vk_gsr2_set_replay_clip(&direct, render_size, &replay.view, guest_clip,
                                   cur.jitter_ndc, prev.jitter_ndc, 1));
    for (unsigned i = 0; i < 16; i++) assert(fabsf(direct.clip_to_prev[i] - in.clip_to_prev[i]) < 1e-6f);
    assert(direct.jitter[0] == in.jitter[0] && direct.jitter[1] == in.jitter[1]);
    in = direct;
    guest_clip[15] = 0; // singular transform
    assert(!vk_gsr2_set_replay_clip(&direct, render_size, &replay.view, guest_clip,
                                    cur.jitter_ndc, prev.jitter_ndc, 1));
    // At W=0.5, standard depth is 0.875 and previous X is 1.25*X - 0.2/W.
    float point[4] = {0.2f, 0.4f, 0.875f, 1};
    float expected[4] = {-0.15f, 0.4f, 0.875f, 1};
    for (unsigned row = 0; row < 4; row++) {
        float actual = 0;
        for (unsigned col = 0; col < 4; col++) actual += in.clip_to_prev[col * 4 + row] * point[col];
        assert(fabsf(actual - expected[row]) < 1e-6f);
    }
    vk_gsr2_camera invalid = cur;
    invalid.world_to_clip[0] = 0;
    assert(!vk_gsr2_set_camera(&in, render_size, &cur, &invalid, 1, VK_GSR2_DEPTH_REVERSED));
    invalid = cur;
    invalid.jitter_ndc[0] = NAN;
    assert(!vk_gsr2_set_camera(&in, render_size, &invalid, &prev, 1, VK_GSR2_DEPTH_REVERSED));

    spatial_image alternate[3] = {images[0], images[1], make_input(VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT)};
    uint16_t packed[MOTION_WORDS], normalized[MOTION_WORDS];
    frame.ticket++;
    frame.cut = 1;
    update_scene(images, 0, 0, 0, 7);
    in.depth_convention = VK_GSR2_DEPTH_STANDARD;
    read_conversion(ctx, &frame, &in, packed);

    update_scene(alternate, 0, 1, 1, 3);
    in.depth_convention = VK_GSR2_DEPTH_REVERSED;
    in.motion.view = alternate[2].view;
    in.motion_format = VK_GSR2_MOTION_PIXELS_A;
    in.motion_flags = VK_GSR2_MOTION_Y_DOWN | VK_GSR2_MOTION_PREV_MINUS_CURRENT;
    frame.ticket++;
    read_conversion(ctx, &frame, &in, normalized);
    for (unsigned i = 0; i < MOTION_WORDS; i++)
        assert(fabsf(unpack_fp16(packed[i]) - unpack_fp16(normalized[i])) < 0.002f);

    unsigned subject = (8 * TEST_IW + 7) * 4;
    assert(fabsf(unpack_fp16(normalized[subject]) - 0.1179f) < 0.003f);
    assert(fabsf(unpack_fp16(normalized[subject + 1]) + 0.0078f) < 0.003f);
    // The background uses unjittered pixel position and depth for camera motion.
    float x = 2 * (0.5f - in.jitter[0]) / TEST_IW - 1;
    float static_motion = -0.25f * x + 0.2f * (1.125f - 185.0f / 255) / 0.125f;
    assert(fabsf(unpack_fp16(normalized[0]) - static_motion) < 0.002f);
    check_depth_aspect(ctx, frame, in);
    spatial_image_drop(&alternate[2]);
}

static void check_variant(uint32_t variant) {
    assert(!vk_gsr2_create(variant, render_size, output_size, 1));
    vk_gsr2 *ctx = vk_gsr2_create(variant, render_size, output_size, 2 * 1024 * 1024);
    assert(ctx);
    spatial_image inputs[3] = {make_input(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT),
        make_input(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT), make_input(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT)};
    vk_gsr2_inputs in = {
        .color = {inputs[0].view, VK_IMAGE_LAYOUT_GENERAL},
        .depth = {inputs[1].view, VK_IMAGE_LAYOUT_GENERAL},
        .motion = {inputs[2].view, VK_IMAGE_LAYOUT_GENERAL},
        .opaque = {inputs[0].view, VK_IMAGE_LAYOUT_GENERAL},
        .clip_to_prev = {[0] = 1, [5] = 1, [10] = 1, [15] = 1},
        .pre_exposure = 1, .near_plane = 0.1f, .tan_half_fov_x = 0.75f, .min_lerp = 0.1f,
        .same_camera = 1
    };
    dm_temporal_frame frame = {.ticket = 1, .stream = 1, .epoch = 1,
        .iw = TEST_IW, .ih = TEST_IH, .ow = TEST_OW, .oh = TEST_OH,
        .inputs = DM_TEMPORAL_REQUIRED | DM_TEMPORAL_OPAQUE, .variants = variant, .real = 1, .memory_ok = 1};
    VkQueryPool queries = VK_NULL_HANDLE;
    uint32_t queues = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(V.pd, &queues, NULL);
    VkQueueFamilyProperties properties[queues];
    vkGetPhysicalDeviceQueueFamilyProperties(V.pd, &queues, properties);
    if (properties[V.qf].timestampValidBits) {
        VkQueryPoolCreateInfo timestamps = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, NULL, 0,
            VK_QUERY_TYPE_TIMESTAMP, 6};
        CHECK(vkCreateQueryPool(V.dev, &timestamps, NULL, &queries));
    }
    uint16_t first[TEST_WORDS], accumulated[TEST_WORDS], cut[TEST_WORDS];
    update_scene(inputs, 0, 0, 0, 0);
    draw_temporal(ctx, &frame, &in, queries, first);
    assert(ctx->history.ticket == 1 && ctx->pending_plan.reset);
    vk_gsr2_output unused;
    assert(!vk_gsr2_record(ctx, test_cb, 1, &frame, &in, NULL, &unused)); // duplicate
    frame.ticket = 2;
    frame.real = 0;
    assert(!vk_gsr2_record(ctx, test_cb, 2, &frame, &in, NULL, &unused)); // generated
    frame.real = 1;
    frame.ticket = 2;
    update_scene(inputs, 1, 0, 0, 7);
    in.jitter[0] = 0.25f;
    in.jitter[1] = -0.5f / 3;
    draw_temporal(ctx, &frame, &in, queries, accumulated);
    assert(ctx->history.ticket == 2 && !ctx->pending_plan.reset);

    frame.ticket = 3;
    frame.cut = 1;
    draw_temporal(ctx, &frame, &in, queries, cut);
    assert(ctx->pending_plan.reset && ctx->history.ticket == 3);
    assert(memcmp(first, accumulated, sizeof first));
    assert(memcmp(cut, accumulated, sizeof cut));

    frame.ticket = 4;
    frame.cut = 0;
    begin();
    assert(vk_gsr2_record(ctx, test_cb, 1, &frame, &in, NULL, &unused));
    CHECK(vkEndCommandBuffer(test_cb));   // staged commands are deliberately not submitted
    vk_gsr2_commit(ctx, 0);
    assert(!ctx->history.valid);
    frame.ticket = 5;
    draw_temporal(ctx, &frame, &in, queries, first);
    assert(ctx->pending_plan.reset && !memcmp(first, cut, sizeof first));
    frame.ticket = 6;
    frame.memory_ok = 0;
    assert(!vk_gsr2_record(ctx, test_cb, 0, &frame, &in, NULL, &unused));
    assert(!ctx->history.valid);
    frame.memory_ok = 1;
    vk_gsr2_inputs unknown_motion = in;
    unknown_motion.motion.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    assert(!vk_gsr2_record(ctx, test_cb, 0, &frame, &unknown_motion, NULL, &unused));
    if (queries) {
        uint64_t stamps[6];
        unsigned count = 2 * ctx->pass_count;
        CHECK(vkGetQueryPoolResults(V.dev, queries, 0, count, count * sizeof stamps[0], stamps,
            sizeof stamps[0], VK_QUERY_RESULT_64_BIT));
        vkDestroyQueryPool(V.dev, queries, NULL);
    }
    check_input_formats(ctx, frame, in, inputs);
    vk_gsr2_destroy(ctx);
    for (unsigned i = 0; i < 3; i++) spatial_image_drop(&inputs[i]);
    printf("SGSR 2 variant %u passed moving geometry, reset, submission and input normalization checks\n", variant);
}

static void check_temporal(void) {
    uint32_t supported = vk_gsr2_variants();
    assert(supported);
    for (uint32_t variant = 1; variant <= DM_TEMPORAL_3CS; variant <<= 1)
        if (supported & variant) check_variant(variant);
}
