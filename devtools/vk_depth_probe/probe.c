// Minimal offscreen Vulkan depth/stencil draw and transfer comparison.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "triangle_vert.h"
#include "early_frag.h"
#include "late_frag.h"
#include "depth_sample_comp.h"
#include <math.h>

#define INSTANCE_FUNCTIONS(X) \
    X(EnumeratePhysicalDevices) X(GetPhysicalDeviceProperties2) X(GetPhysicalDeviceMemoryProperties) \
    X(GetPhysicalDeviceQueueFamilyProperties) X(GetPhysicalDeviceImageFormatProperties) \
    X(CreateDevice) X(GetDeviceProcAddr) X(DestroyInstance)
#define DEVICE_FUNCTIONS(X) \
    X(GetDeviceQueue) X(CreateCommandPool) X(AllocateCommandBuffers) X(BeginCommandBuffer) \
    X(EndCommandBuffer) X(QueueSubmit) X(CreateFence) X(WaitForFences) X(DestroyFence) \
    X(CreateImage) X(GetImageMemoryRequirements) X(CreateImageView) X(BindImageMemory) \
    X(CreateBuffer) X(GetBufferMemoryRequirements) X(BindBufferMemory) X(AllocateMemory) \
    X(FreeMemory) X(MapMemory) X(UnmapMemory) X(FlushMappedMemoryRanges) X(InvalidateMappedMemoryRanges) \
    X(CreateRenderPass) X(CreateFramebuffer) X(CreateShaderModule) X(CreatePipelineLayout) \
    X(CreateGraphicsPipelines) X(CreateComputePipelines) X(CreateSampler) X(DestroySampler) \
    X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) X(CreateDescriptorPool) \
    X(DestroyDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) \
    X(CmdBindDescriptorSets) X(CmdDispatch) X(CmdBeginRenderPass) X(CmdEndRenderPass) X(CmdPipelineBarrier) \
    X(CmdClearDepthStencilImage) X(CmdCopyImageToBuffer) X(CmdBindPipeline) X(CmdBindIndexBuffer) \
    X(CmdPushConstants) X(CmdSetScissor) X(CmdSetStencilReference) X(CmdDrawIndexed) \
    X(DestroyImage) X(DestroyImageView) X(DestroyBuffer) X(DestroyPipeline) X(DestroyShaderModule) \
    X(DestroyRenderPass) X(DestroyFramebuffer) X(DestroyPipelineLayout) X(DestroyCommandPool) X(DestroyDevice)
#define DECLARE(name) static PFN_vk##name vk##name;
INSTANCE_FUNCTIONS(DECLARE)
DEVICE_FUNCTIONS(DECLARE)
#undef DECLARE

#define CHECK(call) do { VkResult r_ = (call); if (r_ != VK_SUCCESS) { \
    fprintf(stderr, "%s failed: %d\n", #call, r_); exit(2); } } while (0)

enum { SIDE = 8, PIXELS = SIDE * SIDE, COLOR = 0, DEPTH = 256, STENCIL = 512,
       CLEAR = 576, CLEAR_STENCIL = 832, INDEX = 896, SAMPLE = 1024, BUFFER_SIZE = SAMPLE + PIXELS * 4 };
static VkDevice device;
static VkPhysicalDeviceMemoryProperties mem;
static VkQueue queue;
static VkCommandPool commands;

typedef struct { VkImage image; VkDeviceMemory memory; VkImageView view; } Image;
typedef struct { float color[4]; float depth; } Params;
static const VkRect2D full = {{0, 0}, {SIDE, SIDE}};
static const VkImageSubresourceRange ds_range = {
    VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1
};

static VkDeviceMemory allocate(VkMemoryRequirements req, VkMemoryPropertyFlags properties) {
    for (uint32_t i = 0; i < mem.memoryTypeCount; i++) {
        if (!(req.memoryTypeBits & (1u << i)) ||
            (mem.memoryTypes[i].propertyFlags & properties) != properties) continue;
        VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, req.size, i};
        VkDeviceMemory memory;
        CHECK(vkAllocateMemory(device, &ai, NULL, &memory));
        return memory;
    }
    fprintf(stderr, "No compatible memory type for %#x\n", properties);
    exit(2);
}

static Image image_create(VkFormat fmt, VkImageUsageFlags usage, VkImageAspectFlags aspect) {
    VkImageCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, 0, VK_IMAGE_TYPE_2D,
        fmt, {SIDE, SIDE, 1}, 1, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, usage,
        VK_SHARING_MODE_EXCLUSIVE, 0, NULL, VK_IMAGE_LAYOUT_UNDEFINED};
    Image im;
    CHECK(vkCreateImage(device, &ci, NULL, &im.image));
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device, im.image, &req);
    im.memory = allocate(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    CHECK(vkBindImageMemory(device, im.image, im.memory, 0));
    VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0,
        im.image, VK_IMAGE_VIEW_TYPE_2D, fmt, {0, 0, 0, 0}, {aspect, 0, 1, 0, 1}};
    CHECK(vkCreateImageView(device, &vi, NULL, &im.view));
    return im;
}

static VkRenderPass pass_create(VkFormat depth, VkAttachmentLoadOp load) {
    VkAttachmentDescription a[] = {
        {0, VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, load,
         VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE,
         VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL},
        {0, depth, VK_SAMPLE_COUNT_1_BIT, load, VK_ATTACHMENT_STORE_OP_STORE,
         load, VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL}
    };
    VkAttachmentReference ar = {0, VK_IMAGE_LAYOUT_GENERAL};
    VkAttachmentReference ds = {1, VK_IMAGE_LAYOUT_GENERAL};
    VkSubpassDescription sub = {0, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, NULL, 1, &ar, NULL, &ds};
    VkRenderPassCreateInfo ci = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, NULL, 0,
                                 2, a, 1, &sub, 0, NULL};
    VkRenderPass pass;
    CHECK(vkCreateRenderPass(device, &ci, NULL, &pass));
    return pass;
}

static VkShaderModule shader(const uint32_t *words, size_t bytes) {
    VkShaderModuleCreateInfo ci = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, bytes, words};
    VkShaderModule module;
    CHECK(vkCreateShaderModule(device, &ci, NULL, &module));
    return module;
}

static VkPipeline pipeline(VkRenderPass pass, VkPipelineLayout layout, VkShaderModule vert,
                           VkShaderModule frag, int load) {
    VkPipelineShaderStageCreateInfo stages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, vert, "main"},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main"}
    };
    VkPipelineVertexInputStateCreateInfo vi = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        NULL, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE};
    VkViewport vp = {0, 0, SIDE, SIDE, 0, 1};
    VkPipelineViewportStateCreateInfo vps = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        NULL, 0, 1, &vp, 1, NULL};
    VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        NULL, 0, VK_FALSE, VK_FALSE, VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE,
        VK_FALSE, 0, 0, 0, 1};
    VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        NULL, 0, VK_SAMPLE_COUNT_1_BIT};
    VkStencilOpState st = {VK_STENCIL_OP_KEEP, VK_STENCIL_OP_REPLACE, VK_STENCIL_OP_KEEP,
        load ? VK_COMPARE_OP_EQUAL : VK_COMPARE_OP_ALWAYS, 0xff, load ? 0 : 0xff, 0};
    VkPipelineDepthStencilStateCreateInfo ds = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        NULL, 0, VK_TRUE, VK_TRUE, VK_COMPARE_OP_GREATER, VK_FALSE, VK_TRUE, st, st, 0, 1};
    VkPipelineColorBlendAttachmentState att = {.colorWriteMask = 15};
    VkPipelineColorBlendStateCreateInfo blend = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        NULL, 0, VK_FALSE, VK_LOGIC_OP_COPY, 1, &att};
    const VkDynamicState states[] = {VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_STENCIL_REFERENCE};
    VkPipelineDynamicStateCreateInfo dyn = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        NULL, 0, 2, states};
    VkGraphicsPipelineCreateInfo ci = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        NULL, 0, 2, stages, &vi, &ia, NULL, &vps, &rs, &ms, &ds, &blend, &dyn, layout, pass, 0};
    VkPipeline p;
    CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &ci, NULL, &p));
    return p;
}

static void transition(VkCommandBuffer cb, VkImage im, VkImageSubresourceRange range,
                       VkAccessFlags access) {
    VkImageMemoryBarrier b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, NULL, 0, access,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
        VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, im, range};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0, 0, NULL, 0, NULL, 1, &b);
}

static void order(VkCommandBuffer cb, VkPipelineStageFlags from, VkAccessFlags write,
                  VkPipelineStageFlags to, VkAccessFlags read) {
    VkMemoryBarrier b = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, NULL, write, read};
    vkCmdPipelineBarrier(cb, from, to, 0, 1, &b, 0, NULL, 0, NULL);
}

static void copy_plane(VkCommandBuffer cb, VkImage im, VkBuffer buf, VkImageAspectFlags aspect, unsigned offset) {
    VkBufferImageCopy copy = {offset, 0, 0, {aspect, 0, 0, 1}, {0, 0, 0}, {SIDE, SIDE, 1}};
    vkCmdCopyImageToBuffer(cb, im, VK_IMAGE_LAYOUT_GENERAL, buf, 1, &copy);
}

static void draw(VkCommandBuffer cb, VkPipelineLayout layout, VkRect2D scissor, const Params *params) {
    vkCmdSetScissor(cb, 0, 1, &scissor);
    vkCmdPushConstants(cb, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof *params, params);
    vkCmdDrawIndexed(cb, 3, 1, 0, 0, 0);
}

static uint32_t word(const uint8_t *p) {
    uint32_t w;
    memcpy(&w, p, sizeof w);
    return w;
}

static int depth_equal(const uint8_t *p, int d24, float expected) {
    if (d24) {
        int v = (int)(word(p) & 0xffffff);
        return abs(v - (int)((double)expected * 16777215 + 0.5)) <= 1;
    }
    float value;
    memcpy(&value, p, sizeof value);
    return value == expected;
}

typedef struct {
    VkDescriptorSetLayout set_layout;
    VkDescriptorPool pool;
    VkDescriptorSet set;
    VkSampler sampler;
    VkImageView view;
    VkPipelineLayout layout;
    VkPipeline pipe;
} SamplePipeline;

static SamplePipeline sample_pipeline(VkImage image, VkFormat format, VkBuffer buffer) {
    SamplePipeline s;
    VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0,
        image, VK_IMAGE_VIEW_TYPE_2D, format, {0, 0, 0, 0}, {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1}};
    CHECK(vkCreateImageView(device, &vi, NULL, &s.view));
    VkSamplerCreateInfo sampler = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST, .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST, .maxLod = 0 };
    CHECK(vkCreateSampler(device, &sampler, NULL, &s.sampler));
    VkDescriptorSetLayoutBinding bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}
    };
    VkDescriptorSetLayoutCreateInfo dsl = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, 2, bindings};
    CHECK(vkCreateDescriptorSetLayout(device, &dsl, NULL, &s.set_layout));
    VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
                                    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
    VkDescriptorPoolCreateInfo pool = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, 1, 2, sizes};
    CHECK(vkCreateDescriptorPool(device, &pool, NULL, &s.pool));
    VkDescriptorSetAllocateInfo alloc = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, s.pool, 1, &s.set_layout};
    CHECK(vkAllocateDescriptorSets(device, &alloc, &s.set));
    VkDescriptorImageInfo image_info = {s.sampler, s.view, VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorBufferInfo buf_info = {buffer, 0, BUFFER_SIZE};
    VkWriteDescriptorSet writes[] = {
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, s.set, 0, 0, 1,
         VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &image_info},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, s.set, 1, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &buf_info}
    };
    vkUpdateDescriptorSets(device, 2, writes, 0, NULL);
    VkPipelineLayoutCreateInfo layout = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 1, &s.set_layout};
    CHECK(vkCreatePipelineLayout(device, &layout, NULL, &s.layout));
    VkShaderModule comp = shader(depth_sample_comp, sizeof depth_sample_comp);
    VkComputePipelineCreateInfo pipe = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, NULL, 0,
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_COMPUTE_BIT, comp, "main"},
        s.layout, VK_NULL_HANDLE, -1};
    CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipe, NULL, &s.pipe));
    vkDestroyShaderModule(device, comp, NULL);
    return s;
}

static int run_case(VkFormat format, int late, int sample_depth) {
    int d24 = format == VK_FORMAT_D24_UNORM_S8_UINT;
    VkImageAspectFlags ds = ds_range.aspectMask;
    VkImageUsageFlags ds_usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (sample_depth) ds_usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    Image depth = image_create(format, ds_usage, ds);
    Image color = image_create(VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, BUFFER_SIZE,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE};
    VkBuffer buffer;
    CHECK(vkCreateBuffer(device, &bi, NULL, &buffer));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, buffer, &req);
    VkDeviceMemory memory = allocate(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    CHECK(vkBindBufferMemory(device, buffer, memory, 0));
    void *map;
    CHECK(vkMapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, &map));
    const uint16_t index[] = {0, 1, 2};
    memcpy((uint8_t *)map + INDEX, index, sizeof index);
    VkMappedMemoryRange host = {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, NULL, memory, 0, VK_WHOLE_SIZE};
    CHECK(vkFlushMappedMemoryRanges(device, 1, &host));

    VkRenderPass passes[] = {pass_create(format, VK_ATTACHMENT_LOAD_OP_CLEAR),
                             pass_create(format, VK_ATTACHMENT_LOAD_OP_LOAD)};
    VkImageView attachments[] = {color.view, depth.view};
    VkFramebufferCreateInfo fbi = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, NULL, 0,
        passes[0], 2, attachments, SIDE, SIDE, 1};
    VkFramebuffer fb;
    CHECK(vkCreateFramebuffer(device, &fbi, NULL, &fb));
    VkShaderModule vert = shader(triangle_vert, sizeof triangle_vert);
    VkShaderModule frag = late ? shader(late_frag, sizeof late_frag) : shader(early_frag, sizeof early_frag);
    VkPushConstantRange push = {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Params)};
    VkPipelineLayoutCreateInfo li = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 0, NULL, 1, &push};
    VkPipelineLayout layout;
    CHECK(vkCreatePipelineLayout(device, &li, NULL, &layout));
    VkPipeline pipelines[] = {pipeline(passes[0], layout, vert, frag, 0),
                              pipeline(passes[1], layout, vert, frag, 1)};

    SamplePipeline sample = {0};
    if (sample_depth) sample = sample_pipeline(depth.image, format, buffer);
    VkCommandBufferAllocateInfo cmd_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        NULL, commands, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
    VkCommandBuffer cb;
    CHECK(vkAllocateCommandBuffers(device, &cmd_info, &cb));
    VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                     NULL, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    CHECK(vkBeginCommandBuffer(cb, &begin));

    transition(cb, color.image, (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    transition(cb, depth.image, ds_range, VK_ACCESS_TRANSFER_WRITE_BIT);
    VkClearDepthStencilValue known = {.depth = 0.625f, .stencil = 0x5a};
    vkCmdClearDepthStencilImage(cb, depth.image, VK_IMAGE_LAYOUT_GENERAL, &known, 1, &ds_range);
    order(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    copy_plane(cb, depth.image, buffer, VK_IMAGE_ASPECT_DEPTH_BIT, CLEAR);
    copy_plane(cb, depth.image, buffer, VK_IMAGE_ASPECT_STENCIL_BIT, CLEAR_STENCIL);
    VkPipelineStageFlags attachment_stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    VkAccessFlags attachment_write = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    VkAccessFlags attachment_read = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    order(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
          attachment_stages, attachment_read | attachment_write);

    VkClearValue clear[] = {{.color.float32 = {0, 0, 0, 1}}, {.depthStencil = known}};
    VkRenderPassBeginInfo pi = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, NULL, passes[0],
        fb, full, 2, clear};
    vkCmdBeginRenderPass(cb, &pi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindIndexBuffer(cb, buffer, INDEX, VK_INDEX_TYPE_UINT16);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines[0]);
    vkCmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_AND_BACK, 0x7d);
    draw(cb, layout, full, &(Params){{0, 1, 0, 1}, 0.75f});
    vkCmdEndRenderPass(cb);

    order(cb, attachment_stages, attachment_write, attachment_stages, attachment_read | attachment_write);
    pi.renderPass = passes[1];
    pi.clearValueCount = 0;
    vkCmdBeginRenderPass(cb, &pi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines[1]);
    vkCmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_AND_BACK, 0x7d);
    draw(cb, layout, full, &(Params){{1, 0, 0, 1}, 0.6875f});
    draw(cb, layout, (VkRect2D){{0, 0}, {SIDE / 2, SIDE}}, &(Params){{1, 1, 0, 1}, 0.875f});
    vkCmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_AND_BACK, 0x5a);
    draw(cb, layout, full, &(Params){{0, 0, 1, 1}, 0.9375f});
    vkCmdEndRenderPass(cb);

    if (sample_depth) {
        order(cb, attachment_stages, attachment_write, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, sample.pipe);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, sample.layout, 0, 1, &sample.set, 0, NULL);
        vkCmdDispatch(cb, 1, 1, 1);
        // The descriptor spans the same buffer as the later copies. Order
        // them even though the shader writes only the word range at SAMPLE.
        order(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
              VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    }
    order(cb, attachment_stages, attachment_write, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    copy_plane(cb, color.image, buffer, VK_IMAGE_ASPECT_COLOR_BIT, COLOR);
    copy_plane(cb, depth.image, buffer, VK_IMAGE_ASPECT_DEPTH_BIT, DEPTH);
    copy_plane(cb, depth.image, buffer, VK_IMAGE_ASPECT_STENCIL_BIT, STENCIL);
    order(cb, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT,
          VK_ACCESS_HOST_READ_BIT);
    CHECK(vkEndCommandBuffer(cb));
    VkFenceCreateInfo fi = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence;
    CHECK(vkCreateFence(device, &fi, NULL, &fence));
    VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 0, NULL, NULL, 1, &cb};
    CHECK(vkQueueSubmit(queue, 1, &si, fence));
    CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
    CHECK(vkInvalidateMappedMemoryRanges(device, 1, &host));

    const uint8_t *bytes = map;
    unsigned clear_ok = 0, clear_stencil_ok = 0, color_ok = 0, depth_ok = 0, stencil_ok = 0, sample_ok = 0;
    for (unsigned i = 0; i < PIXELS; ++i) {
        int left = i % SIDE < SIDE / 2;
        const uint8_t *rgba = bytes + COLOR + i * 4;
        clear_ok += depth_equal(bytes + CLEAR + i * 4, d24, 0.625f);
        clear_stencil_ok += bytes[CLEAR_STENCIL + i] == 0x5a;
        color_ok += rgba[0] == (left ? 255 : 0) && rgba[1] == 255 && rgba[2] == 0 && rgba[3] == 255;
        depth_ok += depth_equal(bytes + DEPTH + i * 4, d24, left ? 0.875f : 0.75f);
        stencil_ok += bytes[STENCIL + i] == 0x7d;
        if (sample_depth) {
            float sampled;
            memcpy(&sampled, bytes + SAMPLE + i * 4, sizeof sampled);
            sample_ok += fabsf(sampled - (left ? 0.875f : 0.75f)) <= 2.0f / 16777215.0f;
        }
    }
    printf("%s %s: raster_color=%u/64 clear_depth_copy=%u/64 draw_depth_copy=%u/64 "
           "clear_stencil_copy=%u/64 draw_stencil_copy=%u/64 raw_clear=%08x raw_draw=%08x,%08x",
           d24 ? "D24S8" : "D32FS8", late ? "late" : "early", color_ok, clear_ok, depth_ok,
           clear_stencil_ok, stencil_ok, word(bytes + CLEAR), word(bytes + DEPTH),
           word(bytes + DEPTH + SIDE / 2 * 4));
    if (sample_depth) printf(" sample_depth=%u/64\n", sample_ok);
    else puts(" sample_depth=unsupported");

    vkDestroyFence(device, fence, NULL);
    for (unsigned i = 0; i < 2; i++) vkDestroyPipeline(device, pipelines[i], NULL);
    vkDestroyFramebuffer(device, fb, NULL);
    for (unsigned i = 0; i < 2; i++) vkDestroyRenderPass(device, passes[i], NULL);
    vkDestroyPipelineLayout(device, layout, NULL);
    vkDestroyShaderModule(device, vert, NULL);
    vkDestroyShaderModule(device, frag, NULL);
    if (sample_depth) {
        vkDestroyPipeline(device, sample.pipe, NULL);
        vkDestroyPipelineLayout(device, sample.layout, NULL);
        vkDestroyDescriptorPool(device, sample.pool, NULL);
        vkDestroyDescriptorSetLayout(device, sample.set_layout, NULL);
        vkDestroySampler(device, sample.sampler, NULL);
        vkDestroyImageView(device, sample.view, NULL);
    }
    vkUnmapMemory(device, memory);
    vkDestroyBuffer(device, buffer, NULL);
    vkFreeMemory(device, memory, NULL);
    Image images[] = {color, depth};
    for (unsigned i = 0; i < 2; ++i) {
        vkDestroyImageView(device, images[i].view, NULL);
        vkDestroyImage(device, images[i].image, NULL);
        vkFreeMemory(device, images[i].memory, NULL);
    }
    return clear_ok != PIXELS || clear_stencil_ok != PIXELS ||
           color_ok != PIXELS || depth_ok != PIXELS || stencil_ok != PIXELS || (sample_depth && sample_ok != PIXELS);
}

int main(int argc, char **argv) {
    void *loader = NULL;
    if (argc == 5 && !strcmp(argv[1], "--custom")) {
        void *hooks = dlopen("libadrenotools.so", RTLD_NOW | RTLD_LOCAL);
        void *(*open_vk)(int, int, const char *, const char *, const char *, const char *, const char *, void **) =
            hooks ? dlsym(hooks, "adrenotools_open_libvulkan") : NULL;
        if (open_vk) loader = open_vk(RTLD_NOW | RTLD_LOCAL, 1, NULL, argv[2], argv[3], argv[4], NULL, NULL);
        if (!loader) { fprintf(stderr, "Cannot load custom Vulkan through libadrenotools\n"); return 2; }
        printf("Requested custom library %s/%s\n", argv[3], argv[4]);
    } else if (argc == 1) {
#ifdef __ANDROID__
        loader = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
#else
        loader = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
#endif
    } else {
        fprintf(stderr, "Usage: %s [--custom HOOKS_DIR DRIVER_DIR DRIVER_SO]\n", argv[0]);
        return 2;
    }
    PFN_vkGetInstanceProcAddr gipa = loader ? dlsym(loader, "vkGetInstanceProcAddr") : NULL;
    if (!gipa) { fprintf(stderr, "Vulkan loader unavailable\n"); return 2; }
    PFN_vkCreateInstance create_instance = (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
    VkApplicationInfo ai = {VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "nvn-depth-probe", 1, NULL, 0, VK_API_VERSION_1_2};
    VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &ai};
    VkInstance instance;
    CHECK(create_instance(&ici, NULL, &instance));
#define LOAD_INSTANCE(name) vk##name = (PFN_vk##name)gipa(instance, "vk" #name);
    INSTANCE_FUNCTIONS(LOAD_INSTANCE)
#undef LOAD_INSTANCE
    uint32_t n = 1;
    VkPhysicalDevice physical;
    VkResult found = vkEnumeratePhysicalDevices(instance, &n, &physical);
    if (found < 0 || !n) { fprintf(stderr, "No Vulkan device (%d)\n", found); return 2; }
    VkPhysicalDeviceDriverProperties driver = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
    VkPhysicalDeviceIDProperties id = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES, &driver};
    VkPhysicalDeviceProperties2 props = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &id};
    vkGetPhysicalDeviceProperties2(physical, &props);
    printf("GPU %s, API %#x, driver ID %u '%s', version %#x, info '%s', UUID ",
        props.properties.deviceName, props.properties.apiVersion, driver.driverID, driver.driverName,
        props.properties.driverVersion, driver.driverInfo);
    for (unsigned i = 0; i < VK_UUID_SIZE; ++i) printf("%02x", id.driverUUID[i]);
    puts("");
    if (argc == 5 && (strstr(argv[4], "radeon") || strstr(argv[4], "radv")) &&
        driver.driverID != VK_DRIVER_ID_MESA_RADV) {
        fprintf(stderr, "The custom loader selected a different Vulkan implementation\n");
        return 2;
    }
    vkGetPhysicalDeviceMemoryProperties(physical, &mem);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &n, NULL);
    VkQueueFamilyProperties *families = calloc(n, sizeof *families);
    if (!families) return 2;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &n, families);
    uint32_t family = 0;
    VkQueueFlags required = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
    while (family < n && (families[family].queueFlags & required) != required) ++family;
    free(families);
    if (family == n) return 2;
    float priority = 1;
    VkDeviceQueueCreateInfo qi = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, family, 1, &priority};
    VkDeviceCreateInfo di = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, NULL, 0, 1, &qi};
    CHECK(vkCreateDevice(physical, &di, NULL, &device));
#define LOAD_DEVICE(name) vk##name = (PFN_vk##name)vkGetDeviceProcAddr(device, "vk" #name);
    DEVICE_FUNCTIONS(LOAD_DEVICE)
#undef LOAD_DEVICE
    vkGetDeviceQueue(device, family, 0, &queue);
    VkCommandPoolCreateInfo ci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL, 0, family};
    CHECK(vkCreateCommandPool(device, &ci, NULL, &commands));

    const VkFormat formats[] = {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT};
    int failed = 0, cases = 0;
    for (unsigned i = 0; i < 2; ++i) {
        VkImageFormatProperties ip;
        VkImageUsageFlags usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        VkResult r = vkGetPhysicalDeviceImageFormatProperties(physical, formats[i], VK_IMAGE_TYPE_2D,
            VK_IMAGE_TILING_OPTIMAL, usage, 0, &ip);
        if (r == VK_ERROR_FORMAT_NOT_SUPPORTED) { printf("SKIP format %d\n", formats[i]); continue; }
        if (r != VK_SUCCESS) { fprintf(stderr, "Format query failed: %d\n", r); return 2; }
        r = vkGetPhysicalDeviceImageFormatProperties(physical, formats[i], VK_IMAGE_TYPE_2D,
            VK_IMAGE_TILING_OPTIMAL, usage | VK_IMAGE_USAGE_SAMPLED_BIT, 0, &ip);
        if (r != VK_SUCCESS && r != VK_ERROR_FORMAT_NOT_SUPPORTED) return 2;
        for (int late = 0; late < 2; ++late) { failed |= run_case(formats[i], late, r == VK_SUCCESS); ++cases; }
    }
    vkDestroyCommandPool(device, commands, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    if (!cases) return 77;
    return failed;
}
