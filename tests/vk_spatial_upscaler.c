// Exercise the production spatial recorder with a desktop Vulkan loader.
#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#include "spatial_upscaler.h"
#include "fsr_vert.spv.h"
#include "fsr_easu_frag.spv.h"
#include "fsr_rcas_frag.spv.h"
#include "gsr1_frag.spv.h"
#include "vivid_frag.spv.h"

#define CHECK(call) do { VkResult r = (call); if (r != VK_SUCCESS) { \
    fprintf(stderr, "%s returned %d\n", #call, r); exit(1); } } while (0)
#define VKLOG(...) do { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)

typedef struct {
    VkImage img;
    VkDeviceMemory mem;
    VkFormat fmt;
    uint32_t w, h, samples, layers, levels, d;
    VkImageAspectFlags aspect;
    VkImageLayout layout;
} vkimg_t;

static struct {
    VkPhysicalDevice pd;
    VkDevice dev;
    VkQueue q;
    uint32_t qf;
    VkPhysicalDeviceProperties props;
    VkPhysicalDeviceMemoryProperties mem;
    VkPipelineCache pcache;
    int has_dynrender;
} V;

static VkFormat g_sfmt = VK_FORMAT_R8G8B8A8_UNORM;
pthread_mutex_t g_vk_queue_mu = PTHREAD_MUTEX_INITIALIZER;
static int selected_mode, sharpness = 50;
int dm_spatial_mode(void) { return selected_mode; }
int dm_fsr_sharpness(void) { return sharpness; }
int vk_pass_dump_user_active(void) { return 1; }
static void vk_pipeline_cache_changed(void) {}

static uint32_t vk_mem_type(uint32_t bits, VkMemoryPropertyFlags want, VkMemoryPropertyFlags avoid) {
    for (uint32_t i = 0; i < V.mem.memoryTypeCount; i++) {
        VkMemoryPropertyFlags flags = V.mem.memoryTypes[i].propertyFlags;
        if ((bits & (1u << i)) && (flags & want) == want && !(flags & avoid)) return i;
    }
    return UINT32_MAX;
}

static void barrier(VkCommandBuffer cb, VkImage img, VkImageLayout from, VkImageLayout to) {
    VkImageMemoryBarrier b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, NULL, VK_ACCESS_MEMORY_WRITE_BIT,
        VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, from, to,
        VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, img, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0, 0, NULL, 0, NULL, 1, &b);
}

static void vk_img_barrier_all(VkCommandBuffer cb) {
    VkAccessFlags access = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    VkMemoryBarrier b = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, NULL, access, access};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0, 1, &b, 0, NULL, 0, NULL);
}

typedef struct {
    VkImage image;
    VkImageView view;
    _Atomic unsigned refs;
} vk_rs_variant;
#include "gfx_vk_reconstruction.h"

static void vk_rs_variant_destroy(vk_rs_variant *v) {
    if (v) {
        unsigned previous = atomic_fetch_sub(&v->refs, 1);
        assert(previous > 1); // each fixture owns its allocation separately
    }
}

#define SPATIAL_HOST_PROBE 1
#include "gfx_vk_spatial.h"

enum { SRC = 20, OUT = 32 };
static VkCommandPool test_pool;
static VkCommandBuffer test_cb;
static VkBuffer pixels;
static VkDeviceMemory host_memory;
static uint8_t *mapped;

static void setup_buffers(void) {
    VkBufferCreateInfo buf = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, OUT * OUT * 8,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    CHECK(vkCreateBuffer(V.dev, &buf, NULL, &pixels));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(V.dev, pixels, &req);
    uint32_t type = vk_mem_type(req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0);
    assert(type != UINT32_MAX);
    VkMemoryAllocateInfo alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, req.size, type};
    CHECK(vkAllocateMemory(V.dev, &alloc, NULL, &host_memory));
    CHECK(vkBindBufferMemory(V.dev, pixels, host_memory, 0));
    CHECK(vkMapMemory(V.dev, host_memory, 0, VK_WHOLE_SIZE, 0, (void **)&mapped));
    VkCommandPoolCreateInfo pool = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL,
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, V.qf};
    CHECK(vkCreateCommandPool(V.dev, &pool, NULL, &test_pool));
    VkCommandBufferAllocateInfo cb = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL,
        test_pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
    CHECK(vkAllocateCommandBuffers(V.dev, &cb, &test_cb));
}

static void begin(void) {
    VkCommandBufferBeginInfo info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL,
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    CHECK(vkBeginCommandBuffer(test_cb, &info));
    vk_img_barrier_all(test_cb);
}

static void complete(void) {
    CHECK(vkEndCommandBuffer(test_cb));
    VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 0, NULL, NULL, 1, &test_cb};
    CHECK(vkQueueSubmit(V.q, 1, &submit, VK_NULL_HANDLE));
    CHECK(vkQueueWaitIdle(V.q));
}

static spatial_image source_image(int size) {
    spatial_image saved = g_spatial.image[SP_OUTPUT];
    g_spatial.image[SP_OUTPUT] = (spatial_image){0};
    assert(spatial_image_ready(SP_OUTPUT, size, size,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, g_sfmt));
    spatial_image src = g_spatial.image[SP_OUTPUT];
    g_spatial.image[SP_OUTPUT] = saved;
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            uint8_t *p = mapped + (size * y + x) * 4;
            p[0] = (x < 2 || x > size - 3) ? 255 : y < size / 2 ? 55 : 218;
            p[1] = x + y > size - 1 ? 250 : 14;
            p[2] = 76;
            p[3] = 173;
        }
    begin();
    barrier(test_cb, src.texture.img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy region = {0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0}, {size, size, 1}};
    vkCmdCopyBufferToImage(test_cb, pixels, src.texture.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    barrier(test_cb, src.texture.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    complete();
    return src;
}

static void readback(VkImage image, uint32_t w, uint32_t h) {
    begin();
    VkBufferImageCopy region = {0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0}, {w, h, 1}};
    vkCmdCopyImageToBuffer(test_cb, image, VK_IMAGE_LAYOUT_GENERAL, pixels, 1, &region);
    vk_img_barrier_all(test_cb);
    complete();
    assert(mapped[0] >= 180 && mapped[4 * ((h - 1) * w)] < 95);
    for (uint32_t i = 0; i < w * h; i++)
        assert(abs((int)mapped[i * 4 + 3] - 173) <= 1);
}

static void check_vivid(spatial_image *input) {
    uint8_t unprocessed[OUT * OUT * 4];
    memcpy(unprocessed, mapped, sizeof unprocessed);
    spatial_image target = g_spatial.image[SP_OUTPUT];
    g_spatial.image[SP_OUTPUT] = (spatial_image){0};
    assert(spatial_image_ready(SP_OUTPUT, OUT, OUT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, g_sfmt));
    spatial_image display = g_spatial.image[SP_OUTPUT];
    g_spatial.image[SP_OUTPUT] = target;
    int full[] = {0, 0, OUT, OUT};
    begin();
    barrier(test_cb, display.texture.img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkClearColorValue black = {{0, 0, 0, 1}};
    VkImageSubresourceRange all = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(test_cb, display.texture.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &all);
    assert(vivid_record(test_cb, input->texture.img, full, 0,
                        display.texture.img, display.view, 0, 0, OUT, OUT));
    barrier(test_cb, display.texture.img, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    complete();
    readback(display.texture.img, OUT, OUT);
    assert(memcmp(unprocessed, mapped, sizeof unprocessed));
    spatial_image_drop(&display);
}

static void run_spatial(void) {
    const char *names[] = {"None", "Bilinear", "FSR 1", "GSR 1", "GSR 2"};
    int mode = SP_NATIVE;
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++) {
        assert(!strcmp(names[i], spatial_mode_name(mode)));
        mode = spatial_mode_next(mode);
    }
    assert(mode == SP_NATIVE && spatial_mode_restore(SP_LEGACY_AUTO) == SP_SGSR2);
    spatial_image src = source_image(SRC);
    int crop[] = {2, 2, 16, 16};
    uint8_t bilinear[OUT * OUT * 4];
    static uint64_t ticket = 0;
    const int modes[] = {SP_BILINEAR, SP_FSR1, SP_SGSR1, SP_NATIVE};
    for (unsigned i = 0; i < sizeof modes / sizeof modes[0]; i++) {
        selected_mode = modes[i];
        spatial_image *out = spatial_reconstruct(&src.texture, crop, 1, OUT, OUT, ++ticket, NULL);
        if (selected_mode == SP_NATIVE) {
            assert(!out && spatial_active == SP_NATIVE);
            continue;
        }
        assert(out && spatial_active == selected_mode);
        unsigned slot = g_spatial.slot;
        assert(spatial_reconstruct(&src.texture, crop, 1, OUT, OUT, ticket, NULL) == out);
        assert(spatial_reconstruct(&src.texture, crop, 1, OUT, OUT, 0, NULL) == NULL);
        assert(g_spatial.slot == slot);
        readback(out->texture.img, OUT, OUT);
        if (selected_mode == SP_BILINEAR) memcpy(bilinear, mapped, sizeof bilinear);
        else assert(memcmp(bilinear, mapped, sizeof bilinear));
        if (selected_mode == SP_SGSR1) check_vivid(out);
    }
    selected_mode = SP_FSR1;
    sharpness = 0;
    spatial_image *resized = spatial_reconstruct(&src.texture, crop, 1, 24, OUT, ++ticket, NULL);
    assert(resized && resized->texture.w == 24);
    readback(resized->texture.img, 24, OUT);
    sharpness = 50;
    selected_mode = SP_SGSR2;
    V.has_dynrender = 0;
    spatial_image *fallback = spatial_reconstruct(&src.texture, crop, 1, OUT, OUT, ++ticket, NULL);
    assert(fallback && spatial_active == SP_BILINEAR);
    readback(fallback->texture.img, OUT, OUT);
    V.has_dynrender = 1;
    spatial_image_drop(&src);
}

static void run_physical(VkFormat source_format) {
    static uint64_t ticket = 100;
    int raw[] = {0, 0, OUT, OUT};
    for (unsigned scale = 50; scale <= 75; scale += 25) {
        int size = OUT * scale / 100;
        int actual[] = {0, 0, size, size};
        VkFormat output_format = g_sfmt;
        g_sfmt = source_format;
        spatial_image src = source_image(size);
        g_sfmt = output_format;
        vk_rs_variant backing = {.image = src.texture.img, .view = src.view, .refs = 1};
        vkimg_t guest = {.w = OUT, .h = OUT, .fmt = source_format, .layout = VK_IMAGE_LAYOUT_GENERAL};
        for (int mode = SP_BILINEAR; mode <= SP_SGSR1; mode++) {
            selected_mode = mode;
            spatial_image *ref = spatial_reconstruct(&src.texture, actual, 0, OUT, OUT, ++ticket, NULL);
            assert(ref);
            begin();
            VkBufferImageCopy region = {0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0}, {OUT, OUT, 1}};
            vkCmdCopyImageToBuffer(test_cb, ref->texture.img, VK_IMAGE_LAYOUT_GENERAL, pixels, 1, &region);
            vk_img_barrier_all(test_cb);
            complete();
            uint8_t expected[OUT * OUT * 4];
            memcpy(expected, mapped, sizeof expected);

            spatial_image_drop(&g_spatial.image[SP_SOURCE]);
            vk_reconstruction_input input = {
                .backing = &backing,
                .physical = {size, size},
                .logical = {OUT, OUT},
                .valid = {{0, 0}, {size, size}},
                .sampled = src.view,
                .format = source_format,
                .layout = VK_IMAGE_LAYOUT_GENERAL,
                .ticket = ++ticket,
            };
            spatial_image *out = spatial_reconstruct(&guest, raw, 0, OUT, OUT, ticket, &input);
            assert(out && spatial_active == mode && backing.refs > 1);
            int staged = mode != SP_BILINEAR &&
                (source_format != g_sfmt || g_sfmt != VK_FORMAT_R8G8B8A8_UNORM);
            assert((g_spatial.image[SP_SOURCE].texture.img != VK_NULL_HANDLE) == staged);
            begin();
            vkCmdCopyImageToBuffer(test_cb, out->texture.img, VK_IMAGE_LAYOUT_GENERAL, pixels, 1, &region);
            vk_img_barrier_all(test_cb);
            complete();
            spatial_collect_sources();
            assert(backing.refs == 1);
            for (unsigned i = 0; i < sizeof expected; i++)
                assert(abs((int)expected[i] - mapped[i]) <= 1);
        }
        spatial_drop_images();
        assert(backing.refs == 1);
        spatial_image_drop(&src);
    }
    assert(!spatial_reconstruct(&(vkimg_t){.w = OUT, .h = OUT}, raw, 0, OUT, OUT, ++ticket, NULL));
}

#ifdef SGSR2_HOST_PROBE
#include "vk_gsr2_probe.inc"
#endif

int main(void) {
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "nx2androidX spatial test",
        1, NULL, 0, VK_API_VERSION_1_3};
    VkInstanceCreateInfo instance_info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &app};
    VkInstance instance;
    CHECK(vkCreateInstance(&instance_info, NULL, &instance));
    uint32_t count = 1;
    VkResult result = vkEnumeratePhysicalDevices(instance, &count, &V.pd);
    if (result != VK_SUCCESS && result != VK_INCOMPLETE) return 77;
    if (!count) return 77;
    vkGetPhysicalDeviceProperties(V.pd, &V.props);
    if (V.props.apiVersion < VK_API_VERSION_1_3) return 77;
    vkGetPhysicalDeviceMemoryProperties(V.pd, &V.mem);
    VkPhysicalDeviceDynamicRenderingFeatures dynamic = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};
    VkPhysicalDeviceFeatures2 features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &dynamic};
    vkGetPhysicalDeviceFeatures2(V.pd, &features);
    if (!dynamic.dynamicRendering) return 77;
    vkGetPhysicalDeviceQueueFamilyProperties(V.pd, &count, NULL);
    VkQueueFamilyProperties families[count];
    vkGetPhysicalDeviceQueueFamilyProperties(V.pd, &count, families);
    while (V.qf < count && !(families[V.qf].queueFlags & VK_QUEUE_GRAPHICS_BIT)) V.qf++;
    if (V.qf >= count) return 77;
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, V.qf, 1, &priority};
    VkPhysicalDeviceFeatures enabled = {.shaderImageGatherExtended = features.features.shaderImageGatherExtended};
    VkDeviceCreateInfo device = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &dynamic, 0, 1, &queue,
        0, NULL, 0, NULL, &enabled};
    CHECK(vkCreateDevice(V.pd, &device, NULL, &V.dev));
    vkGetDeviceQueue(V.dev, V.qf, 0, &V.q);
    V.has_dynrender = 1;
    setup_buffers();

#ifdef SGSR2_HOST_PROBE
    check_temporal();
#else
    run_spatial();
    run_physical(g_sfmt);
    g_sfmt = VK_FORMAT_R8G8B8A8_SRGB;
    run_spatial();
    run_physical(g_sfmt);
    // Surfaces may select an sRGB swapchain even for an UNORM window image.
    run_physical(VK_FORMAT_R8G8B8A8_UNORM);
#endif
    CHECK(vkDeviceWaitIdle(V.dev));
    spatial_drop_images();
    if (g_spatial.queries) vkDestroyQueryPool(V.dev, g_spatial.queries, NULL);
    for (int i = 0; i < SP_SLOTS; i++) vkDestroyFence(V.dev, g_spatial.fence[i], NULL);
    vkDestroyCommandPool(V.dev, g_spatial.commands, NULL);
    for (int i = 0; i < SP_PASS_COUNT; i++)
        if (g_spatial.pipeline[i]) vkDestroyPipeline(V.dev, g_spatial.pipeline[i], NULL);
    vkDestroyPipelineLayout(V.dev, g_spatial.layout, NULL);
    vkDestroyDescriptorPool(V.dev, g_spatial.pool, NULL);
    vkDestroyDescriptorSetLayout(V.dev, g_spatial.dsl, NULL);
    vkDestroySampler(V.dev, g_spatial.sampler, NULL);
    vkDestroyCommandPool(V.dev, test_pool, NULL);
    vkUnmapMemory(V.dev, host_memory);
    vkDestroyBuffer(V.dev, pixels, NULL);
    vkFreeMemory(V.dev, host_memory, NULL);
    vkDestroyDevice(V.dev, NULL);
    vkDestroyInstance(instance, NULL);
#ifndef SGSR2_HOST_PROBE
    puts("spatial reconstruction passed (UNORM and sRGB, physical 50% and 75%, native bypass at 100%)");
#endif
}
