"""Fault-injected test of the production LSFG output allocation, not GPU validation."""
import subprocess,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class LSFGAllocation(unittest.TestCase):
    def test_submit_failure_and_slot_reuse(self):
        code = (ROOT / 'nxnative/dm/android/dm_framegen.c').read_text()
        slot_type = next(line for line in code.splitlines() if line.endswith('} fg_slot;'))
        start = code.index('static VkResult begin_slot(')
        end = code.index('\nint dm_fg_frame(', start)
        commands = code[start:end]
        source = r'''
#include <assert.h>
#include <pthread.h>
#include <vulkan/vulkan_core.h>
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static struct { VkDevice dev; VkQueue q; pthread_mutex_t *qmu; uint32_t qf; } V = {.qmu = &mutex};
static struct { VkCommandPool pool; } F = {.pool = (VkCommandPool)3};
static int fail, reset, submits, waits, retired;
static VkResult wait_fence(VkDevice device, uint32_t count, const VkFence *fences,
                           VkBool32 all, uint64_t timeout) {
    assert(device == V.dev && count == 1 && *fences && all && timeout == UINT64_MAX);
    ++waits;
    return VK_SUCCESS;
}
static void vk_fg_fence_retired(VkFence fence) { assert(fence && waits == retired + 1); ++retired; }
static VkResult reset_cb(VkCommandBuffer cb, VkCommandBufferResetFlags flags) {
    assert(cb && !flags);
    return VK_SUCCESS;
}
static VkResult begin(VkCommandBuffer cb, const VkCommandBufferBeginInfo *info) {
    assert(cb && info->flags == VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    return VK_SUCCESS;
}
static VkResult end(VkCommandBuffer cb) {
    assert(cb);
    return fail == 1 ? VK_ERROR_OUT_OF_HOST_MEMORY : VK_SUCCESS;
}
static VkResult reset_fence(VkDevice device, uint32_t count, const VkFence *fences) {
    assert(device == V.dev && count == 1 && *fences);
    ++reset;
    return fail == 2 ? VK_ERROR_OUT_OF_HOST_MEMORY : VK_SUCCESS;
}
static VkResult queue(VkQueue q, uint32_t count, const VkSubmitInfo *info, VkFence fence) {
    assert(q == V.q && count == 1 && info->commandBufferCount == 1 && fence);
    ++submits;
    return fail == 3 ? VK_ERROR_OUT_OF_HOST_MEMORY : VK_SUCCESS;
}
static const struct {
    PFN_vkCreateCommandPool vkCreateCommandPool;
    PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers;
    PFN_vkCreateFence vkCreateFence;
    PFN_vkWaitForFences vkWaitForFences;
    PFN_vkResetCommandBuffer vkResetCommandBuffer;
    PFN_vkBeginCommandBuffer vkBeginCommandBuffer;
    PFN_vkEndCommandBuffer vkEndCommandBuffer;
    PFN_vkResetFences vkResetFences;
    PFN_vkQueueSubmit vkQueueSubmit;
} vk = {.vkWaitForFences = wait_fence, .vkResetCommandBuffer = reset_cb, .vkBeginCommandBuffer = begin,
    .vkEndCommandBuffer = end, .vkResetFences = reset_fence, .vkQueueSubmit = queue};
''' + slot_type + '\n' + commands + r'''
int main(void) {
    fg_slot slot = {.cb = (VkCommandBuffer)1, .fence = (VkFence)2};
    for (fail = 1; fail <= 3; ++fail) {
        assert(!begin_slot(&slot));
        assert(submit_slot(&slot) == VK_ERROR_OUT_OF_HOST_MEMORY);
        assert(!slot.pending);
    }
    assert(reset == 2 && submits == 1 && !waits);
    fail = 0;
    assert(!begin_slot(&slot) && !waits);
    assert(!submit_slot(&slot) && slot.pending);
    assert(!begin_slot(&slot) && waits == 1 && retired == 1 && !slot.pending);
}
'''
        headers = ROOT / 'nxnative/dm/third_party/vulkan-headers/include'
        with tempfile.TemporaryDirectory() as tmp:
            case = Path(tmp) / 'slot.c'
            case.write_text(source)
            exe = case.with_suffix('')
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pthread',
                            '-Wno-missing-field-initializers', '-I', str(headers),
                            str(case), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True, timeout=10)

    def test_partial_allocation_retained_for_teardown(self):
        code=(ROOT/'nxnative/dm/android/dm_framegen.c').read_text()
        start=code.index('static int out_new('); end=code.index('\n}',start)+2
        out_new=code[start:end]
        source=r'''
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <string.h>
#define VK_NULL_HANDLE 0
#define VRED(...) ((void)0)
enum { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO=1,VK_IMAGE_TYPE_2D,VK_FORMAT_R8G8B8A8_UNORM,
    VK_SAMPLE_COUNT_1_BIT,VK_IMAGE_TILING_OPTIMAL,VK_IMAGE_USAGE_STORAGE_BIT=1,
    VK_IMAGE_USAGE_SAMPLED_BIT=2,VK_IMAGE_USAGE_TRANSFER_SRC_BIT=4,VK_IMAGE_USAGE_TRANSFER_DST_BIT=8,
    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO=6,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,VK_IMAGE_VIEW_TYPE_2D,VK_IMAGE_ASPECT_COLOR_BIT,
    VK_ERROR_OUT_OF_DEVICE_MEMORY=-2 };
typedef int VkResult,VkImage,VkImageView,VkDeviceMemory;
typedef struct {unsigned width,height;} VkExtent2D;
typedef struct {int sType,imageType,format; struct {unsigned w,h,d;} extent; int mipLevels,arrayLayers,samples,tiling,usage;} VkImageCreateInfo;
typedef struct {uint64_t size;uint32_t memoryTypeBits;} VkMemoryRequirements;
typedef struct {int sType;uint64_t allocationSize;uint32_t memoryTypeIndex;} VkMemoryAllocateInfo;
typedef struct {int sType,image,viewType,format; int subresourceRange[5];} VkImageViewCreateInfo;
static struct {int dev;} V;
static struct {VkImage out[3];VkDeviceMemory out_mem[3];} F;
static int fail;
static VkResult create(int d,const VkImageCreateInfo *i,void *a,VkImage *o){(void)d;(void)a; assert(i->extent.w==3840&&i->extent.h==2160);*o=11;return fail==1?-2:0;}
static void req(int d,VkImage i,VkMemoryRequirements *r){(void)d;assert(i==11);*r=(VkMemoryRequirements){1234,1};}
static uint32_t vka_mem_type(uint32_t b,int f,int a){(void)b;(void)f;(void)a;return fail==2?UINT32_MAX:0;}
static VkResult alloc(int d,const VkMemoryAllocateInfo *i,void *a,VkDeviceMemory *o){(void)d;(void)a;assert(i->allocationSize==1234);*o=22;return fail==3?-2:0;}
static VkResult bind(int d,VkImage i,VkDeviceMemory m,int o){(void)d;assert(i==11&&m==22&&!o);return fail==4?-2:0;}
static VkResult view(int d,const VkImageViewCreateInfo *i,void *a,VkImageView *o){(void)d;(void)a;assert(i->image==11);*o=33;return fail==5?-2:0;}
static struct {VkResult (*vkCreateImage)(int,const VkImageCreateInfo*,void*,VkImage*);
    void (*vkGetImageMemoryRequirements)(int,VkImage,VkMemoryRequirements*);
    VkResult (*vkAllocateMemory)(int,const VkMemoryAllocateInfo*,void*,VkDeviceMemory*);
    VkResult (*vkBindImageMemory)(int,VkImage,VkDeviceMemory,int);
    VkResult (*vkCreateImageView)(int,const VkImageViewCreateInfo*,void*,VkImageView*);} vk={create,req,alloc,bind,view};
'''+out_new+r'''
int main(void){
    for(fail=1;fail<=5;fail++){
        memset(&F,0,sizeof F);VkImageView v=0;
        assert(!out_new((VkExtent2D){3840,2160},0,&v));
        assert(F.out[0]==(fail>=2?11:0));
        assert(F.out_mem[0]==(fail>=4?22:0));
        assert(!v);
    }
    fail=0;memset(&F,0,sizeof F);VkImageView v=0;
    assert(out_new((VkExtent2D){3840,2160},0,&v));
    assert(F.out[0]==11&&F.out_mem[0]==22&&v==33);
}
'''
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'case.c';p.write_text(source);exe=p.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True,cwd=d)
    def test_failed_extent_does_not_disable_smaller_source(self):
        code=(ROOT/'nxnative/dm/android/dm_framegen.c').read_text()
        start=code.index('    if (!F.chain || F.ext.width != e.width')
        end=code.index('\n    }',start)+6
        block=code[start:end]
        source=r'''
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stddef.h>
typedef struct {unsigned width,height;} VkExtent2D;
static struct {int chain,flow,state,failed_flow;VkExtent2D ext,failed_extent;double rebuild_retry_at;} F;
static atomic_int g_state;
static int attempts;
static int fg_skip(const char *why){assert(why&&*why);return 0;}
static void reason(const char *fmt,const char *s){(void)fmt;(void)s;}
static void unavailable(const char *fmt,const char *s){reason(fmt,s);F.state=-1;}
static int rebuild(VkExtent2D e,int flow){
    ++attempts;
    if(e.width>=3840){F.chain=0;return 0;}
    F.chain=1;F.ext=e;F.flow=flow;return 1;
}
static int run(VkExtent2D e,int flow,double tnow){
    (void)tnow; if(F.state<0)return 0;
'''+block+r'''
    return 1;
}
int main(void){
    (void)unavailable; (void)reason; (void)g_state;
    F.state=1;
    assert(!run((VkExtent2D){3840,2160},50,1));
    assert(attempts==1&&F.state==1);
    assert(!run((VkExtent2D){3840,2160},50,2));assert(attempts==1);
    assert(!run((VkExtent2D){3840,2160},50,3));assert(attempts==2);
    assert(run((VkExtent2D){2560,1440},50,3.1));assert(attempts==3);
    assert(run((VkExtent2D){2560,1440},50,3.2));assert(attempts==3);
    assert(!run((VkExtent2D){3840,2160},50,3.3));assert(attempts==4);
}
'''
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'case.c';p.write_text(source);exe=p.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True,cwd=d)

if __name__=='__main__':unittest.main()
