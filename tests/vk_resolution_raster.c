// Real Vulkan regression using extracted production allocation and raster code.
// Tests physical memory requirements, mapped viewport/scissor, depth, and
// logical-coordinate sampling into a native-size image. No Android device
// or game bundle is required; this does not validate the full NVN replay.
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <vulkan/vulkan.h>
#include "nvn_obj.h"
#include "gfx_vk_resolution_budget.h"
#include "gfx_vk_resolution_restore.h"
#include "gfx_vk_resolution_draw.h"
#include "gfx_vk_resolution_raster.h"
#include "rs_types.inc"
#include "producer.vert.h"
#include "producer.frag.h"
#include "consumer.frag.h"
#define CHECK(x) do {VkResult r=(x);if(r!=VK_SUCCESS){fprintf(stderr,"%s: %d\n",#x,r);exit(1);}}while(0)
static struct {VkPhysicalDevice pd;VkDevice dev;VkPhysicalDeviceProperties props;VkPhysicalDeviceMemoryProperties mem;} V;
static int vk_sub_open(void){return 1;}
typedef struct {int compressed,is_int;} vkfmt_t;
static const vkfmt_t *vk_fmt(int fmt){static const vkfmt_t f={0};(void)fmt;return &f;}
#include "rs_alloc.inc"
static struct {vkimg_t *im;uint32_t w,h,layers;VkFormat fmt;} P_att[9];
static VkRenderingInfo P_ri;
static nx_rs_raster g_rs_raster;
static int g_rs_pass_scaled,g_lower_left,g_flipy;
static struct {uint32_t pass_w,pass_h;int sc_set;p_scissor sc;} S;
#include "rs_raster.inc"
enum {W=256,H=128};
static VkQueue queue;
static VkCommandBuffer cb;
static VkCommandPool cmdpool;
static VkBuffer host;
static VkDeviceMemory hostmem;
static uint8_t *mapped;
static VkDescriptorSetLayout dsl;
static VkDescriptorPool pool;
static VkDescriptorSet ds;
static VkSampler sampler;
static VkPipelineLayout layout;
static VkPipeline producer,consumer;
static const VkFormat colorfmt=VK_FORMAT_R8G8B8A8_UNORM,depthfmt=VK_FORMAT_D32_SFLOAT;
static void memory_barrier(void) {
    VkMemoryBarrier b={VK_STRUCTURE_TYPE_MEMORY_BARRIER,NULL,VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT};
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&b,0,NULL,0,NULL);
}
static vk_rs_variant *physical(vkimg_t *im,VkFormat fmt,VkImageAspectFlags aspect,uint32_t w,uint32_t h) {
    VkImageUsageFlags usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|
        (aspect==VK_IMAGE_ASPECT_COLOR_BIT?VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT:VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
    *im=(vkimg_t){.usage=usage,.fmt=fmt,.aspect=aspect,.w=W,.h=H,.d=1,.layers=1,.levels=1,.samples=1,
        .target=NVN_TEXTURE_TARGET_2D,.pool=1,.layout=VK_IMAGE_LAYOUT_UNDEFINED};
    VkImageCreateInfo ci={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,NULL,0,VK_IMAGE_TYPE_2D,fmt,{W,H,1},1,1,VK_SAMPLE_COUNT_1_BIT,
        VK_IMAGE_TILING_OPTIMAL,usage,VK_SHARING_MODE_EXCLUSIVE,0,NULL,VK_IMAGE_LAYOUT_UNDEFINED};
    CHECK(vkCreateImage(V.dev,&ci,NULL,&im->img));
    vk_rs_variant *v=vk_rs_variant_create(im,w,h);
    vkDestroyImage(V.dev,im->img,NULL);
    assert(v && v->native_bytes && v->physical_w==w && v->physical_h==h);
    if(w<W)assert(v->bytes<v->native_bytes);else assert(v->bytes==v->native_bytes);
    im->img=v->image;im->mem=v->memory;im->rs_backing=v;
    return v;
}
static void prepare(vk_rs_variant *v) {
    VkImageMemoryBarrier b={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,NULL,0,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED,v->image,{v->aspect,0,1,0,1}};
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,NULL,0,NULL,1,&b);
    v->layout=VK_IMAGE_LAYOUT_GENERAL;
}
static VkShaderModule module(const uint32_t *p,size_t n) {
    VkShaderModuleCreateInfo ci={VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,NULL,0,n,p};
    VkShaderModule m;CHECK(vkCreateShaderModule(V.dev,&ci,NULL,&m));return m;
}
static VkPipeline pipeline(int depth,const uint32_t *words,size_t size) {
    VkShaderModule vs=module(producer_vert,sizeof producer_vert),fs=module(words,size);
    VkPipelineShaderStageCreateInfo st[2]={{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,NULL,0,VK_SHADER_STAGE_VERTEX_BIT,vs,"main"},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,NULL,0,VK_SHADER_STAGE_FRAGMENT_BIT,fs,"main"}};
    VkPipelineVertexInputStateCreateInfo vi={VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia={VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,NULL,0,VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportStateCreateInfo vp={VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,NULL,0,1,NULL,1,NULL};
    VkPipelineRasterizationStateCreateInfo rs={VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,NULL,0,0,0,VK_POLYGON_MODE_FILL,VK_CULL_MODE_NONE,VK_FRONT_FACE_COUNTER_CLOCKWISE,0,0,0,0,1};
    VkPipelineMultisampleStateCreateInfo ms={VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,NULL,0,VK_SAMPLE_COUNT_1_BIT};
    VkPipelineDepthStencilStateCreateInfo dz={VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,NULL,0,depth,depth,VK_COMPARE_OP_LESS};
    VkPipelineColorBlendAttachmentState att={.colorWriteMask=15};
    VkPipelineColorBlendStateCreateInfo bl={VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,NULL,0,0,VK_LOGIC_OP_COPY,1,&att};
    VkDynamicState states[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn={VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,NULL,0,2,states};
    VkPipelineRenderingCreateInfo render={VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,NULL,0,1,&colorfmt,depth?depthfmt:VK_FORMAT_UNDEFINED};
    VkGraphicsPipelineCreateInfo ci={VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,&render,0,2,st,&vi,&ia,NULL,&vp,&rs,&ms,&dz,&bl,&dyn,layout};
    VkPipeline p;CHECK(vkCreateGraphicsPipelines(V.dev,VK_NULL_HANDLE,1,&ci,NULL,&p));
    vkDestroyShaderModule(V.dev,vs,NULL);vkDestroyShaderModule(V.dev,fs,NULL);return p;
}
static void setup(uint32_t qf) {
    VkCommandPoolCreateInfo cp={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,NULL,VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,qf};
    CHECK(vkCreateCommandPool(V.dev,&cp,NULL,&cmdpool));
    VkCommandBufferAllocateInfo ca={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,NULL,cmdpool,VK_COMMAND_BUFFER_LEVEL_PRIMARY,1};
    CHECK(vkAllocateCommandBuffers(V.dev,&ca,&cb));
    VkBufferCreateInfo bi={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,NULL,0,W*H*8,VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_SHARING_MODE_EXCLUSIVE};
    CHECK(vkCreateBuffer(V.dev,&bi,NULL,&host));VkMemoryRequirements mr;vkGetBufferMemoryRequirements(V.dev,host,&mr);
    uint32_t mt=vk_mem_type(mr.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,0);assert(mt!=UINT32_MAX);
    VkMemoryAllocateInfo ma={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,NULL,mr.size,mt};
    CHECK(vkAllocateMemory(V.dev,&ma,NULL,&hostmem));CHECK(vkBindBufferMemory(V.dev,host,hostmem,0));
    CHECK(vkMapMemory(V.dev,hostmem,0,VK_WHOLE_SIZE,0,(void **)&mapped));
    VkDescriptorSetLayoutBinding b={0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT};
    VkDescriptorSetLayoutCreateInfo sl={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,NULL,0,1,&b};
    CHECK(vkCreateDescriptorSetLayout(V.dev,&sl,NULL,&dsl));
    VkDescriptorPoolSize ps={VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1};
    VkDescriptorPoolCreateInfo pi={VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,NULL,0,1,1,&ps};CHECK(vkCreateDescriptorPool(V.dev,&pi,NULL,&pool));
    VkDescriptorSetAllocateInfo da={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,NULL,pool,1,&dsl};CHECK(vkAllocateDescriptorSets(V.dev,&da,&ds));
    VkPushConstantRange pc={VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(float)};
    VkPipelineLayoutCreateInfo pl={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,NULL,0,1,&dsl,1,&pc};CHECK(vkCreatePipelineLayout(V.dev,&pl,NULL,&layout));
    VkSamplerCreateInfo si={VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,NULL,0,VK_FILTER_NEAREST,VK_FILTER_NEAREST,VK_SAMPLER_MIPMAP_MODE_NEAREST,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
    CHECK(vkCreateSampler(V.dev,&si,NULL,&sampler));
    producer=pipeline(1,producer_frag,sizeof producer_frag);consumer=pipeline(0,consumer_frag,sizeof consumer_frag);
}
static void run(unsigned pct) {
    uint32_t w=W*pct/100,h=H*pct/100;
    vkimg_t col,dep,out;
    vk_rs_variant *c=physical(&col,colorfmt,VK_IMAGE_ASPECT_COLOR_BIT,w,h);
    vk_rs_variant *d=physical(&dep,depthfmt,VK_IMAGE_ASPECT_DEPTH_BIT,w,h);
    vk_rs_variant *o=physical(&out,colorfmt,VK_IMAGE_ASPECT_COLOR_BIT,W,H);
    VkDescriptorImageInfo di={sampler,c->view,VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet wr={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,NULL,ds,0,0,1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,&di};
    vkUpdateDescriptorSets(V.dev,1,&wr,0,NULL);
    CHECK(vkResetCommandBuffer(cb,0));VkCommandBufferBeginInfo begin={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,NULL,VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    CHECK(vkBeginCommandBuffer(cb,&begin));prepare(c);prepare(d);prepare(o);
    VkRenderingAttachmentInfo ca={VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,NULL,c->view,VK_IMAGE_LAYOUT_GENERAL,0,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED,
        VK_ATTACHMENT_LOAD_OP_CLEAR,VK_ATTACHMENT_STORE_OP_STORE,.clearValue.color={{0,0,1,1}}};
    VkRenderingAttachmentInfo da={VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,NULL,d->view,VK_IMAGE_LAYOUT_GENERAL,0,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED,
        VK_ATTACHMENT_LOAD_OP_CLEAR,VK_ATTACHMENT_STORE_OP_STORE,.clearValue.depthStencil={.625f,0}};
    memset(P_att,0,sizeof P_att);
    P_att[0].im=&col;P_att[0].w=W;P_att[0].h=H;P_att[0].layers=1;P_att[0].fmt=colorfmt;
    P_att[8].im=&dep;P_att[8].w=W;P_att[8].h=H;P_att[8].layers=1;P_att[8].fmt=depthfmt;
    P_ri=(VkRenderingInfo){VK_STRUCTURE_TYPE_RENDERING_INFO,NULL,0,{{0,0},{W,H}},1,0,1,&ca,&da,NULL};
    g_rs_pass_scaled=0;assert(resolution_configure_pass());assert(P_ri.renderArea.extent.width==w && P_ri.renderArea.extent.height==h);
    assert(g_rs_pass_scaled==(pct<100));
    assert(nx_rs_raster_init(&g_rs_raster,W,H,w,h));
    S.pass_w=W;S.pass_h=H;S.sc_set=1;S.sc=(p_scissor){0,0,W,H*3/4};
    nx_rs_viewport vp={0,0,W,H};nx_rs_scale_viewport(&g_rs_raster,&vp);
    VkViewport v={vp.x,vp.y,vp.w,vp.h,0,1};VkRect2D sc=scissor_rect();
    assert(sc.extent.width==w && sc.extent.height==h*3/4);
    float scale=g_rs_raster.scale;
    vkCmdBeginRendering(cb,&P_ri);vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,producer);
    vkCmdSetViewport(cb,0,1,&v);vkCmdSetScissor(cb,0,1,&sc);
    vkCmdPushConstants(cb,layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof scale,&scale);vkCmdDraw(cb,3,1,0,0);vkCmdEndRendering(cb);
    memory_barrier();
    ca.imageView=o->view;P_ri.renderArea.extent=(VkExtent2D){W,H};P_ri.pDepthAttachment=NULL;
    v=(VkViewport){0,0,W,H,0,1};sc=(VkRect2D){{0,0},{W,H}};
    vkCmdBeginRendering(cb,&P_ri);vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,consumer);
    vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&ds,0,NULL);
    vkCmdSetViewport(cb,0,1,&v);vkCmdSetScissor(cb,0,1,&sc);
    vkCmdPushConstants(cb,layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof scale,&scale);vkCmdDraw(cb,3,1,0,0);vkCmdEndRendering(cb);
    memory_barrier();
    VkBufferImageCopy copy={0,0,0,{VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},{0},{W,H,1}};
    vkCmdCopyImageToBuffer(cb,o->image,VK_IMAGE_LAYOUT_GENERAL,host,1,&copy);
    copy=(VkBufferImageCopy){W*H*4,0,0,{VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1},{0},{w,h,1}};
    vkCmdCopyImageToBuffer(cb,d->image,VK_IMAGE_LAYOUT_GENERAL,host,1,&copy);
    memory_barrier();CHECK(vkEndCommandBuffer(cb));
    VkSubmitInfo sub={VK_STRUCTURE_TYPE_SUBMIT_INFO,NULL,0,NULL,NULL,1,&cb};CHECK(vkQueueSubmit(queue,1,&sub,VK_NULL_HANDLE));CHECK(vkQueueWaitIdle(queue));
    for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++){
        const uint8_t *p=mapped+4*(y*W+x);
        unsigned expected=y>=H*3/4?2:x<W/2?0:1;
        for(unsigned k=0;k<3;k++)assert(p[k]==(k==expected?255:0));assert(p[3]==255);
    }
    const float *z=(const float *)(mapped+W*H*4);
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)assert(fabsf(z[y*w+x]-(y<h*3/4?.25f:.625f))<.00001f);
    printf("scale %u%%: %ux%u, color+depth %llu native bytes -> %llu allocated bytes; all color/depth readbacks pass\n",
        pct,w,h,(unsigned long long)(c->native_bytes+d->native_bytes),(unsigned long long)(c->bytes+d->bytes));
    vk_rs_variant_destroy(c);vk_rs_variant_destroy(d);vk_rs_variant_destroy(o);
    assert(!atomic_load(&g_rs_budget.bytes) && !atomic_load(&g_rs_restore_budget.bytes));
}
int main(void) {
    VkApplicationInfo app={VK_STRUCTURE_TYPE_APPLICATION_INFO,NULL,"nx2androidX physical raster",1,NULL,0,VK_API_VERSION_1_3};
    VkInstanceCreateInfo ici={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,NULL,0,&app};
    VkInstance instance;CHECK(vkCreateInstance(&ici,NULL,&instance));
    uint32_t n=1;VkResult r=vkEnumeratePhysicalDevices(instance,&n,&V.pd);assert((r==VK_SUCCESS||r==VK_INCOMPLETE)&&n);
    vkGetPhysicalDeviceProperties(V.pd,&V.props);vkGetPhysicalDeviceMemoryProperties(V.pd,&V.mem);
    VkPhysicalDeviceDynamicRenderingFeatures dr={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};
    VkPhysicalDeviceFeatures2 feat={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,&dr};vkGetPhysicalDeviceFeatures2(V.pd,&feat);assert(dr.dynamicRendering);
    vkGetPhysicalDeviceQueueFamilyProperties(V.pd,&n,NULL);VkQueueFamilyProperties *q=calloc(n,sizeof *q);assert(q);
    vkGetPhysicalDeviceQueueFamilyProperties(V.pd,&n,q);unsigned qf=0;while(qf<n&&!(q[qf].queueFlags&VK_QUEUE_GRAPHICS_BIT))qf++;assert(qf<n);free(q);
    float pr=1;VkDeviceQueueCreateInfo qi={VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,NULL,0,qf,1,&pr};
    VkDeviceCreateInfo dev={VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,&dr,0,1,&qi};CHECK(vkCreateDevice(V.pd,&dev,NULL,&V.dev));vkGetDeviceQueue(V.dev,qf,0,&queue);
    setup(qf);run(100);run(75);run(50);
    vkDestroyPipeline(V.dev,producer,NULL);vkDestroyPipeline(V.dev,consumer,NULL);vkDestroyPipelineLayout(V.dev,layout,NULL);
    vkDestroyDescriptorPool(V.dev,pool,NULL);vkDestroyDescriptorSetLayout(V.dev,dsl,NULL);vkDestroySampler(V.dev,sampler,NULL);
    vkUnmapMemory(V.dev,hostmem);vkDestroyBuffer(V.dev,host,NULL);vkFreeMemory(V.dev,hostmem,NULL);vkDestroyCommandPool(V.dev,cmdpool,NULL);
    vkDestroyDevice(V.dev,NULL);vkDestroyInstance(instance,NULL);
    return 0;
}
