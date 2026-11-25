// Shared loader and readback setup is in vk_spatial_upscaler.c.
#include "gfx_vk_gsr2.c"

enum { TEST_IW = 17, TEST_IH = 19, TEST_OW = 29, TEST_OH = 31, TEST_WORDS = TEST_OW * TEST_OH * 4 };
static const VkExtent2D render_size = {TEST_IW, TEST_IH}, output_size = {TEST_OW, TEST_OH};

static spatial_image make_input(void) {
    spatial_image image = {0};
    VkImageCreateInfo create = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, 0, VK_IMAGE_TYPE_2D,
        VK_FORMAT_R8G8B8A8_UNORM, {TEST_IW, TEST_IH, 1}, 1, 1, VK_SAMPLE_COUNT_1_BIT,
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
        VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, {0}, temporal_color};
    CHECK(vkCreateImageView(V.dev, &view, NULL, &image.view));
    return image;
}

static void update_scene(const spatial_image images[3], int frame) {
    unsigned size = TEST_IW * TEST_IH * 4;
    for (int y = 0; y < TEST_IH; y++) {
        for (int x = 0; x < TEST_IW; x++) {
            unsigned i = (y * TEST_IW + x) * 4;
            int subject = x >= 4 + frame && x < 10 + frame && y >= 4 && y < 13;
            int lattice = (x + y + frame) & 1;
            mapped[i] = subject ? 108 + 18 * lattice : 24 + 6 * lattice;
            mapped[i + 1] = subject ? 82 : 30;
            mapped[i + 2] = subject ? 64 : 36;
            mapped[i + 3] = 255;
            mapped[size + i] = subject ? 51 : 185;
            mapped[size + i + 1] = mapped[size + i + 2] = mapped[size + i + 3] = 0;
            mapped[2 * size + i] = subject ? 135 : 0;
            mapped[2 * size + i + 1] = subject ? 127 : 0;
            mapped[2 * size + i + 2] = mapped[2 * size + i + 3] = 0;
        }
    }
    begin();
    for (unsigned i = 0; i < 3; i++) {
        VkImage image = images[i].texture.img;
        barrier(test_cb, image, frame ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy copy = {i * size, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
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

static void check_variant(uint32_t variant) {
    assert(!vk_gsr2_create(variant, render_size, output_size, 1));
    vk_gsr2 *ctx = vk_gsr2_create(variant, render_size, output_size, 2 * 1024 * 1024);
    assert(ctx);
    spatial_image inputs[3] = {make_input(), make_input(), make_input()};
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
    update_scene(inputs, 0);
    draw_temporal(ctx, &frame, &in, queries, first);
    assert(ctx->history.ticket == 1 && ctx->pending_plan.reset);
    vk_gsr2_output unused;
    assert(!vk_gsr2_record(ctx, test_cb, 1, &frame, &in, NULL, &unused)); // duplicate
    frame.ticket = 2;
    frame.real = 0;
    assert(!vk_gsr2_record(ctx, test_cb, 2, &frame, &in, NULL, &unused)); // generated
    frame.real = 1;
    frame.ticket = 2;
    update_scene(inputs, 1);
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
    vk_gsr2_destroy(ctx);
    for (unsigned i = 0; i < 3; i++) spatial_image_drop(&inputs[i]);
    printf("SGSR 2 variant %u passed moving geometry, reset and submission checks\n", variant);
}

static void check_temporal(void) {
    uint32_t supported = vk_gsr2_variants();
    assert(supported);
    for (uint32_t variant = 1; variant <= DM_TEMPORAL_3CS; variant <<= 1)
        if (supported & variant) check_variant(variant);
}
