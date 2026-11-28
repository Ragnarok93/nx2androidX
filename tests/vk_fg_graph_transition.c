/* Real Vulkan graph construction/dispatch/retirement using synthetic modules.
 * No proprietary LSFG SPIR-V or actual interpolation-quality claim. */
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vulkan/vulkan.h>
#define CHECK(x) do { VkResult r_=(x); if(r_!=VK_SUCCESS){fprintf(stderr,"%s: %d\n",#x,r_);abort();} } while(0)
#define LOG(...) do {fprintf(stderr,__VA_ARGS__);fputc('\n',stderr);} while(0)
#include "fg_vka_shim.h"
#include "fg_shader_modules.h"
static vka_V_t device_ctx;
static vka_vk_t table={
#include "fg_vka_entries.h"
};
vka_V_t *vka_V(void){return &device_ctx;}
vka_vk_t *vka_vk(void){return &table;}
static VkPhysicalDeviceMemoryProperties memory_props;
static pthread_mutex_t queue_mu=PTHREAD_MUTEX_INITIALIZER;
static uint32_t vk_mem_type(uint32_t bits,VkMemoryPropertyFlags want,VkMemoryPropertyFlags avoid){
    for(uint32_t i=0;i<memory_props.memoryTypeCount;i++){
        VkMemoryPropertyFlags f=memory_props.memoryTypes[i].propertyFlags;
        if((bits&(1u<<i))&&(f&want)==want&&!(f&avoid))return i;
    }
    return UINT32_MAX;
}
void vka_img_barrier(VkCommandBuffer cb,VkImage img,VkImageAspectFlags aspect,VkImageLayout from,VkImageLayout to){
    VkImageMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask=from==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,
        .oldLayout=from,.newLayout=to,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=img,.subresourceRange={aspect,0,1,0,1}};
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,NULL,0,NULL,1,&b);
}
/* Include the real graph source with only the Android entry-point shim replaced. */
#define __ANDROID__ 1
#include "lsfg/lsfg_chain.c"
#undef __ANDROID__
#include "lsfg/lsfg_graph_owner.h"
static lsfg_module noop_mod={.nwords=sizeof(noop_spv)/4,.words=(uint32_t *)noop_spv};
static lsfg_module gen_mod={.nwords=sizeof(gen_spv)/4,.words=(uint32_t *)gen_spv};
const lsfg_module *lsfg_module_get(const lsfg_modules *mods,uint32_t id){
    (void)mods;return id==LSFG_SH_GENERATE?&gen_mod:&noop_mod;
}
enum { SIDE=32, SLOT_COUNT=3 };
static const VkExtent2D extent={SIDE,SIDE};
static const VkImageSubresourceRange color={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
static VkInstance instance;
static VkCommandPool pool;
static VkImage outputs[3];
static VkImageView views[3];
static VkDeviceMemory output_mem[3];
static struct {
    VkCommandBuffer cb;VkFence fence;VkBuffer buf;VkDeviceMemory mem;uint8_t *mapped;
    unsigned pending,expected;
} slots[SLOT_COUNT];
static lsfg_graph_owner owner;
static lsfg_chain *active;
static lsfg_modules modules;
static uint64_t count,ticket;
static void all_barrier(VkCommandBuffer cb){
    VkMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT};
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&b,0,NULL,0,NULL);
}
static void setup(void){
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_1};
    VkInstanceCreateInfo ii={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
    CHECK(vkCreateInstance(&ii,NULL,&instance));
    uint32_t n=1;CHECK(vkEnumeratePhysicalDevices(instance,&n,&V.pd));assert(n==1);
    uint32_t qn=0;vkGetPhysicalDeviceQueueFamilyProperties(V.pd,&qn,NULL);
    VkQueueFamilyProperties *q=calloc(qn,sizeof *q);assert(q);
    vkGetPhysicalDeviceQueueFamilyProperties(V.pd,&qn,q);
    V.qf=UINT32_MAX;
    for(uint32_t i=0;i<qn;i++)if(q[i].queueFlags&VK_QUEUE_COMPUTE_BIT){V.qf=i;break;}
    free(q);assert(V.qf!=UINT32_MAX);
    VkPhysicalDeviceRobustness2FeaturesEXT robust={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT};
    VkPhysicalDeviceFeatures2 features={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&robust};
    vkGetPhysicalDeviceFeatures2(V.pd,&features);assert(robust.nullDescriptor);
    robust.robustBufferAccess2=robust.robustImageAccess2=VK_FALSE;
    float priority=1;
    VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=V.qf,.queueCount=1,.pQueuePriorities=&priority};
    const char *ext=VK_EXT_ROBUSTNESS_2_EXTENSION_NAME;
    VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=&robust,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi,
        .enabledExtensionCount=1,.ppEnabledExtensionNames=&ext};
    CHECK(vkCreateDevice(V.pd,&di,NULL,&V.dev));
    vkGetDeviceQueue(V.dev,V.qf,0,&V.q);V.qmu=&queue_mu;V.null_desc=1;
    VkPhysicalDeviceProperties props;vkGetPhysicalDeviceProperties(V.pd,&props);V.ub_align=props.limits.minUniformBufferOffsetAlignment;
    vkGetPhysicalDeviceMemoryProperties(V.pd,&memory_props); V.mem=memory_props;
    VkCommandPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=V.qf};
    CHECK(vkCreateCommandPool(V.dev,&pi,NULL,&pool));
    for(unsigned i=0;i<3;i++){
        VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_UNORM,
            .extent={SIDE,SIDE,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
            .usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT};
        CHECK(vkCreateImage(V.dev,&ci,NULL,&outputs[i]));
        VkMemoryRequirements mr;vkGetImageMemoryRequirements(V.dev,outputs[i],&mr);
        VkMemoryAllocateInfo ma={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=mr.size,
            .memoryTypeIndex=vk_mem_type(mr.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,0)};
        CHECK(vkAllocateMemory(V.dev,&ma,NULL,&output_mem[i]));CHECK(vkBindImageMemory(V.dev,outputs[i],output_mem[i],0));
        VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=outputs[i],.viewType=VK_IMAGE_VIEW_TYPE_2D,
            .format=ci.format,.subresourceRange=color};
        CHECK(vkCreateImageView(V.dev,&vi,NULL,&views[i]));
        VkCommandBufferAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
        CHECK(vkAllocateCommandBuffers(V.dev,&ai,&slots[i].cb));
        VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};CHECK(vkCreateFence(V.dev,&fi,NULL,&slots[i].fence));
        VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=SIDE*SIDE*4,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT};
        CHECK(vkCreateBuffer(V.dev,&bi,NULL,&slots[i].buf));vkGetBufferMemoryRequirements(V.dev,slots[i].buf,&mr);
        ma.allocationSize=mr.size;ma.memoryTypeIndex=vk_mem_type(mr.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,0);
        CHECK(vkAllocateMemory(V.dev,&ma,NULL,&slots[i].mem));CHECK(vkBindBufferMemory(V.dev,slots[i].buf,slots[i].mem,0));
        CHECK(vkMapMemory(V.dev,slots[i].mem,0,VK_WHOLE_SIZE,0,(void **)&slots[i].mapped));
    }
}
static void retire(unsigned i){
    if(!slots[i].pending)return;
    CHECK(vkWaitForFences(V.dev,1,&slots[i].fence,VK_TRUE,UINT64_MAX));
    if(slots[i].expected!=UINT32_MAX){
        for(unsigned p=0;p<SIDE*SIDE;p++){
            assert(abs((int)slots[i].mapped[4*p]-(int)slots[i].expected)<=1);
            assert(slots[i].mapped[4*p+3]==255);
        }
    }
    slots[i].pending=0;lsfg_graph_owner_retire_slot(&owner,i);
}
static void begin(unsigned i){
    retire(i);CHECK(vkResetFences(V.dev,1,&slots[i].fence));CHECK(vkResetCommandBuffer(slots[i].cb,0));
    VkCommandBufferBeginInfo bi={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    CHECK(vkBeginCommandBuffer(slots[i].cb,&bi));all_barrier(slots[i].cb);
}
static void submit(unsigned i,unsigned expected){
    CHECK(vkEndCommandBuffer(slots[i].cb));VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&slots[i].cb};
    CHECK(vkQueueSubmit(V.q,1,&si,slots[i].fence));slots[i].pending=1;slots[i].expected=expected;
}
static unsigned value(uint64_t t){return (unsigned)(t*14%200);}
static void source(VkCommandBuffer cb,lsfg_chain *chain,uint64_t index,uint64_t t){
    all_barrier(cb);VkClearColorValue c={.float32={value(t)/255.0f,0,0,1}};
    vkCmdClearColorImage(cb,lsfg_chain_input(chain,index),VK_IMAGE_LAYOUT_GENERAL,&c,1,&color);
}
static void frame(uint64_t epoch,int seed,int switching){
    ++ticket;unsigned i=(unsigned)(ticket%SLOT_COUNT);begin(i);VkCommandBuffer cb=slots[i].cb;
    lsfg_chain *g=switching?owner.extra:active;
    uint64_t index=switching?owner.history.next_index:count;
    if(switching)assert(lsfg_graph_owner_ready(&owner,epoch,ticket));
    source(cb,g,index,ticket);lsfg_chain_shared(g,cb,index);
    lsfg_chain_generate(g,cb,index,1,0);
    if(seed){
        assert(owner.phase==LSFG_GRAPH_WARMING);
        if(!owner.history.initialized)lsfg_chain_record_init(owner.extra,cb);
        uint64_t n=lsfg_graph_history_index(&owner.history,ticket);
        source(cb,owner.extra,n,ticket);lsfg_chain_seed(owner.extra,cb,n);
    }
    all_barrier(cb);
    VkBufferImageCopy cp={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={SIDE,SIDE,1}};
    vkCmdCopyImageToBuffer(cb,outputs[0],VK_IMAGE_LAYOUT_GENERAL,slots[i].buf,1,&cp);
    submit(i,(value(ticket-1)+value(ticket))/2);
    if(switching)assert(lsfg_graph_owner_publish(&owner,&active,i,epoch,ticket,1));
    else lsfg_graph_owner_submit(&owner,i,1,1,seed,epoch,ticket);
    count=index+1;
}
static void drain(void){for(unsigned i=0;i<SLOT_COUNT;i++)retire(i);}
static int start(unsigned pct,uint64_t cap,uint64_t epoch){
    return lsfg_graph_owner_start(&owner,&modules,extent,pct,views,cap,epoch);
}
static int collect_job(uint64_t epoch){
    int r;unsigned tries=0;
    do{r=lsfg_graph_owner_poll(&owner,epoch);if(!r){struct timespec ts={0,1000000};nanosleep(&ts,NULL);}}while(!r&&++tries<30000);
    assert(r);return r;
}
int main(void){
    setup();
    atomic_int cancelled=1;
    assert(!lsfg_chain_build_cancelable(&modules,extent,.5f,views,UINT64_MAX,&cancelled));
    active=lsfg_chain_build(&modules,extent,.5f,views,UINT64_MAX);assert(active&&lsfg_chain_bytes(active));
    uint64_t private_sum=0;
    for(unsigned heap=0;heap<memory_props.memoryHeapCount;heap++)private_sum+=lsfg_chain_heap_bytes(active,heap);
    assert(private_sum==lsfg_chain_bytes(active));
    lsfg_graph_budget measured;
    assert(lsfg_chain_measure(extent,.5f,&measured,NULL));
    assert(measured.used==private_sum);
    for(unsigned heap=0;heap<memory_props.memoryHeapCount;heap++)
        assert(measured.heap_used[heap]==lsfg_chain_heap_bytes(active,heap));
    assert(!lsfg_chain_build(&modules,extent,.5f,views,measured.used-1));
    lsfg_chain *exact=lsfg_chain_build(&modules,extent,.5f,views,measured.used);
    assert(exact&&lsfg_chain_bytes(exact)==measured.used);
    lsfg_chain_free(exact);
    begin(0);lsfg_chain_record_init(active,slots[0].cb);
    for(unsigned i=0;i<3;i++)vka_img_barrier(slots[0].cb,outputs[i],VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL);
    for(unsigned i=0;i<3;i++){source(slots[0].cb,active,i,++ticket);lsfg_chain_seed(active,slots[0].cb,i);}
    count=3;submit(0,UINT32_MAX);retire(0);
    frame(1,0,0);
    assert(start(40,1,1));assert(collect_job(1)==-1);frame(1,0,0);drain();
    unsigned scales[]={40,60,25,75};
    for(unsigned k=0;k<4;k++){
        uint64_t epoch=k+2;
        assert(start(scales[k],128u*1024u*1024u,epoch));assert(collect_job(epoch)==1);
        assert(owner.extra&&lsfg_chain_bytes(owner.extra)<=128u*1024u*1024u);
        lsfg_chain *old=active;
        for(unsigned n=0;n<3;n++)frame(epoch,1,0);
        assert(owner.history.filled==3);
        frame(epoch,0,1);assert(active!=old&&owner.phase==LSFG_GRAPH_RETIRING);
        assert(!start(50,128u*1024u*1024u,epoch));
        drain();assert(owner.phase==LSFG_GRAPH_EMPTY);
        frame(epoch,0,0);drain();
    }
    assert(start(50,128u*1024u*1024u,8));assert(collect_job(8)==1);
    frame(8,1,0);lsfg_graph_owner_cancel(&owner);assert(owner.phase==LSFG_GRAPH_RETIRING);
    drain();assert(owner.phase==LSFG_GRAPH_EMPTY);frame(8,0,0);drain();
    lsfg_graph_owner_clear_idle(&owner);lsfg_chain_free(active);
    for(unsigned i=0;i<3;i++){
        vkUnmapMemory(V.dev,slots[i].mem);vkDestroyBuffer(V.dev,slots[i].buf,NULL);vkFreeMemory(V.dev,slots[i].mem,NULL);
        vkDestroyFence(V.dev,slots[i].fence,NULL);
        vkDestroyImageView(V.dev,views[i],NULL);vkDestroyImage(V.dev,outputs[i],NULL);vkFreeMemory(V.dev,output_mem[i],NULL);
    }
    vkDestroyCommandPool(V.dev,pool,NULL);vkDestroyDevice(V.dev,NULL);vkDestroyInstance(instance,NULL);
    puts("LSFG synthetic Vulkan repeated graph handoff readback PASS");
    return 0;
}
