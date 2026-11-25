/* Opt-in, one-shot compute coverage diagnostic. Private objects, private
 * command buffer and fence; never use guest cache owners or replay descriptors. */
#include "gfx_vk_resolution_compute_probe_shader.h"
extern pthread_mutex_t g_vk_queue_mu;

static int resolution_probe_compute_extent(uint32_t w,uint32_t h,const uint32_t groups[3]) {
    VkImage im=VK_NULL_HANDLE;
    VkImageView view=VK_NULL_HANDLE;
    VkDeviceMemory imem=VK_NULL_HANDLE,bmem=VK_NULL_HANDLE;
    VkBuffer buf=VK_NULL_HANDLE;
    VkShaderModule shader=VK_NULL_HANDLE;
    VkDescriptorSetLayout dsl=VK_NULL_HANDLE;
    VkDescriptorPool dp=VK_NULL_HANDLE;
    VkPipelineLayout layout=VK_NULL_HANDLE;
    VkPipeline pipeline=VK_NULL_HANDLE;
    VkCommandPool pool=VK_NULL_HANDLE;
    VkCommandBuffer cb=VK_NULL_HANDLE;
    VkFence fence=VK_NULL_HANDLE;
    void *mapped=NULL;
    VkResult r=VK_SUCCESS;
    int result=0;
    unsigned pixels=w*h, matches=0;
    const char *step="format support";
    const VkFormat fmt=VK_FORMAT_R32G32B32A32_SFLOAT;
    const VkImageUsageFlags usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkImageFormatProperties props;
    VkMemoryRequirements rq;
    if (!w || !h || w>32 || h>32 || !groups) return 0;
    const VkDeviceSize bytes=(VkDeviceSize)pixels*16;
#define RS_TRY(label,call) do {step=(label);r=(call);if(r!=VK_SUCCESS)goto cleanup;} while(0)
    RS_TRY("format support",vkGetPhysicalDeviceImageFormatProperties(V.pd,fmt,VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,usage,0,&props));
    if(w>props.maxExtent.width || h>props.maxExtent.height || !(props.sampleCounts&VK_SAMPLE_COUNT_1_BIT)) {r=VK_ERROR_FORMAT_NOT_SUPPORTED;goto cleanup;}
    VkImageCreateInfo ici={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=fmt,
        .extent={w,h,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
        .usage=usage,.sharingMode=VK_SHARING_MODE_EXCLUSIVE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};
    RS_TRY("create image",vkCreateImage(V.dev,&ici,NULL,&im));
    vkGetImageMemoryRequirements(V.dev,im,&rq);
    step="image memory type/budget";
    uint32_t mt=vk_mem_type(rq.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,0);
    if(mt==UINT32_MAX || rq.size>(1u<<20)){r=VK_ERROR_OUT_OF_DEVICE_MEMORY;goto cleanup;}
    VkMemoryAllocateInfo ma={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=rq.size,.memoryTypeIndex=mt};
    RS_TRY("allocate image",vkAllocateMemory(V.dev,&ma,NULL,&imem));
    RS_TRY("bind image",vkBindImageMemory(V.dev,im,imem,0));
    VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=im,.viewType=VK_IMAGE_VIEW_TYPE_2D,
        .format=fmt,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    RS_TRY("image view",vkCreateImageView(V.dev,&vi,NULL,&view));
    VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=bytes,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    RS_TRY("readback buffer",vkCreateBuffer(V.dev,&bi,NULL,&buf));
    vkGetBufferMemoryRequirements(V.dev,buf,&rq);
    step="readback memory type/budget";
    mt=vk_mem_type(rq.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,0);
    if(mt==UINT32_MAX || rq.size>(1u<<20)){r=VK_ERROR_OUT_OF_HOST_MEMORY;goto cleanup;}
    ma.allocationSize=rq.size;ma.memoryTypeIndex=mt;
    RS_TRY("allocate readback",vkAllocateMemory(V.dev,&ma,NULL,&bmem));
    RS_TRY("bind readback",vkBindBufferMemory(V.dev,buf,bmem,0));
    RS_TRY("map readback",vkMapMemory(V.dev,bmem,0,VK_WHOLE_SIZE,0,&mapped));
    memset(mapped,0,(size_t)bytes);
    VkShaderModuleCreateInfo sm={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=sizeof nx_rs_probe_shader,.pCode=nx_rs_probe_shader};
    RS_TRY("shader",vkCreateShaderModule(V.dev,&sm,NULL,&shader));
    VkDescriptorSetLayoutBinding binding={7,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL};
    VkDescriptorSetLayoutCreateInfo ds={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=1,.pBindings=&binding};
    RS_TRY("descriptor layout",vkCreateDescriptorSetLayout(V.dev,&ds,NULL,&dsl));
    VkDescriptorPoolSize ps={VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1};
    VkDescriptorPoolCreateInfo pc={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=1,.pPoolSizes=&ps};
    RS_TRY("descriptor pool",vkCreateDescriptorPool(V.dev,&pc,NULL,&dp));
    VkDescriptorSetAllocateInfo da={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=dp,.descriptorSetCount=1,.pSetLayouts=&dsl};
    VkDescriptorSet set;
    RS_TRY("descriptor set",vkAllocateDescriptorSets(V.dev,&da,&set));
    VkDescriptorImageInfo di={.imageView=view,.imageLayout=VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet dw={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=7,.descriptorCount=1,
        .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,.pImageInfo=&di};
    vkUpdateDescriptorSets(V.dev,1,&dw,0,NULL);
    VkPipelineLayoutCreateInfo pli={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&dsl};
    RS_TRY("pipeline layout",vkCreatePipelineLayout(V.dev,&pli,NULL,&layout));
    VkComputePipelineCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=shader,.pName="main"},
        .layout=layout,.basePipelineIndex=-1};
    RS_TRY("compute pipeline",vkCreateComputePipelines(V.dev,VK_NULL_HANDLE,1,&pi,NULL,&pipeline));
    VkCommandPoolCreateInfo cp={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,.queueFamilyIndex=V.qf};
    RS_TRY("command pool",vkCreateCommandPool(V.dev,&cp,NULL,&pool));
    VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    RS_TRY("command buffer",vkAllocateCommandBuffers(V.dev,&ca,&cb));
    VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    RS_TRY("fence",vkCreateFence(V.dev,&fi,NULL,&fence));
    VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    RS_TRY("begin",vkBeginCommandBuffer(cb,&begin));
    VkImageSubresourceRange range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    VkImageMemoryBarrier ib={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_GENERAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=im,.subresourceRange=range};
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&ib);
    VkClearColorValue zero={{0.f,0.f,0.f,0.f}};
    vkCmdClearColorImage(cb,im,VK_IMAGE_LAYOUT_GENERAL,&zero,1,&range);
    ib.oldLayout=VK_IMAGE_LAYOUT_GENERAL;ib.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;ib.dstAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,NULL,0,NULL,1,&ib);
    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
    vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);
    vkCmdDispatch(cb,groups[0],groups[1],groups[2]);
    ib.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;ib.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&ib);
    VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={w,h,1}};
    vkCmdCopyImageToBuffer(cb,im,VK_IMAGE_LAYOUT_GENERAL,buf,1,&copy);
    VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);
    RS_TRY("end",vkEndCommandBuffer(cb));
    VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cb};
    step="queue submit";
    pthread_mutex_lock(&g_vk_queue_mu);
    r=vkQueueSubmit(V.q,1,&submit,fence);
    pthread_mutex_unlock(&g_vk_queue_mu);
    if(r!=VK_SUCCESS)goto cleanup;
    r=vkWaitForFences(V.dev,1,&fence,VK_TRUE,5000000000ull);
    if(r!=VK_SUCCESS) {
        // Cannot establish completion. Never destroy possibly in-flight
        // objects. Bounded one-shot allocation remains until device teardown.
        VKLOG("[resolution-compute-probe] %ux%u fence wait=%d; INCONCLUSIVE, private resources retained",w,h,r);
        return -1;
    }
    matches=nx_rs_probe_matches((const uint32_t *)mapped,pixels);
    result=matches==pixels;
    VKLOG("[resolution-compute-probe] %ux%u groups=%ux%u pixels=%u/%u %s",w,h,groups[0],groups[1],matches,pixels,result?"PASS":"FAIL");
cleanup:
    if(r!=VK_SUCCESS)VKLOG("[resolution-compute-probe] %ux%u %s result=%d; INCONCLUSIVE",w,h,step,r);
    if(fence)vkDestroyFence(V.dev,fence,NULL);
    if(pool)vkDestroyCommandPool(V.dev,pool,NULL);
    if(pipeline)vkDestroyPipeline(V.dev,pipeline,NULL);
    if(layout)vkDestroyPipelineLayout(V.dev,layout,NULL);
    if(dp)vkDestroyDescriptorPool(V.dev,dp,NULL);
    if(dsl)vkDestroyDescriptorSetLayout(V.dev,dsl,NULL);
    if(shader)vkDestroyShaderModule(V.dev,shader,NULL);
    if(view)vkDestroyImageView(V.dev,view,NULL);
    if(im)vkDestroyImage(V.dev,im,NULL);
    if(imem)vkFreeMemory(V.dev,imem,NULL);
    if(mapped)vkUnmapMemory(V.dev,bmem);
    if(buf)vkDestroyBuffer(V.dev,buf,NULL);
    if(bmem)vkFreeMemory(V.dev,bmem,NULL);
#undef RS_TRY
    return r==VK_SUCCESS?result:-1;
}
static void resolution_probe_compute(void) {
    const uint32_t local[3]={8,8,1},native[3]={4,4,1};
    uint32_t half[3];
    if (!nx_rs_compute_constant_fill(nx_rs_probe_shader,sizeof nx_rs_probe_shader/sizeof nx_rs_probe_shader[0],7,local) ||
        !nx_rs_compute_grid(native,local,32,32,16,16,half)) {
        VKLOG("[resolution-compute-probe] INCONCLUSIVE: shader or grid self-check rejected");
        return;
    }
    int a=resolution_probe_compute_extent(32,32,native);
    int b=a<0?-1:resolution_probe_compute_extent(16,16,half);
    VKLOG("[resolution-compute-probe] native=%d half=%d overall=%s; guest images unchanged",
        a,b,a==1&&b==1?"PASS":a<0||b<0?"INCONCLUSIVE":"FAIL");
}
