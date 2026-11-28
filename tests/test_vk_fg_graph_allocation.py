"""Fault-inject the actual graph image allocator and initialization recorder."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
ANDROID=ROOT/'nxnative/dm/android'
class FGGraphAllocation(unittest.TestCase):
    def test_budget_and_private_layouts(self):
        source=(ANDROID/'lsfg/lsfg_chain.c').read_text()
        start=source.index('static limg *img_new(')
        image=source[source.index('static int chain_stopped('):source.index('\n}',start)+2]
        start=source.index('void lsfg_chain_record_init(')
        init=source[start:source.index('\n}',start)+2]
        build=source[source.index('lsfg_chain *lsfg_chain_build_cancelable('):source.index('uint64_t lsfg_chain_bytes(')]
        # The queue-free constructor must not own any submission path.
        for forbidden in ('vkQueueSubmit','vkQueueWaitIdle','vkWaitForFences'):
            self.assertNotIn(forbidden,build)
        code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>
#include <stdio.h>
#include <vulkan/vulkan_core.h>
#include "lsfg/lsfg_graph_budget.h"
#define VRED(...) do { if (0) fprintf(stderr,__VA_ARGS__); } while (0)
#define VLOG(...) VRED(__VA_ARGS__)
enum {MAX_IMAGES=192};
typedef struct {VkImage img;VkDeviceMemory mem;VkImageView view;VkExtent2D ext;VkFormat fmt;} limg;
typedef struct {int bad,nimg;lsfg_graph_budget budget;const atomic_int *cancel;limg imgs[MAX_IMAGES];} lsfg_chain;
static struct {VkDevice dev; VkPhysicalDeviceMemoryProperties mem;} V={.mem={.memoryTypeCount=1,.memoryTypes={{.heapIndex=0}}}};
static unsigned fail,allocs,layouts;
static uint64_t req_bytes=4096;
static VkResult create(VkDevice d,const VkImageCreateInfo *i,const VkAllocationCallbacks *a,VkImage *out){
    (void)d;(void)i;(void)a;*out=(VkImage)(uintptr_t)11;return fail==1?VK_ERROR_OUT_OF_DEVICE_MEMORY:VK_SUCCESS;
}
static void requirements(VkDevice d,VkImage i,VkMemoryRequirements *r){
    (void)d;assert(i);*r=(VkMemoryRequirements){req_bytes,4096,1};
}
static uint32_t vka_mem_type(uint32_t b,uint32_t f,uint32_t p){(void)b;(void)f;(void)p;return fail==2?UINT32_MAX:0;}
static VkResult allocate(VkDevice d,const VkMemoryAllocateInfo *i,const VkAllocationCallbacks *a,VkDeviceMemory *out){
    (void)d;(void)a;assert(i->allocationSize==req_bytes);++allocs;*out=(VkDeviceMemory)(uintptr_t)22;
    return fail==3?VK_ERROR_OUT_OF_DEVICE_MEMORY:VK_SUCCESS;
}
static VkResult bind(VkDevice d,VkImage i,VkDeviceMemory m,VkDeviceSize off){
    (void)d;assert(i&&m&&!off);return fail==4?VK_ERROR_OUT_OF_DEVICE_MEMORY:VK_SUCCESS;
}
static VkResult view(VkDevice d,const VkImageViewCreateInfo *i,const VkAllocationCallbacks *a,VkImageView *out){
    (void)d;(void)a;assert(i->image);*out=(VkImageView)(uintptr_t)33;return fail==5?VK_ERROR_OUT_OF_DEVICE_MEMORY:VK_SUCCESS;
}
static struct {
    PFN_vkCreateImage vkCreateImage;PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements;
    PFN_vkAllocateMemory vkAllocateMemory;PFN_vkBindImageMemory vkBindImageMemory;PFN_vkCreateImageView vkCreateImageView;
} vk={create,requirements,allocate,bind,view};
static void vka_img_barrier(VkCommandBuffer cb,VkImage img,VkImageAspectFlags aspect,VkImageLayout from,VkImageLayout to){
    assert(cb&&img==(VkImage)(uintptr_t)11&&aspect==VK_IMAGE_ASPECT_COLOR_BIT);
    assert(from==VK_IMAGE_LAYOUT_UNDEFINED&&to==VK_IMAGE_LAYOUT_GENERAL);++layouts;
}
'''+image+'\n'+init+r'''
int main(void){
    lsfg_chain c={.budget={4096,0}};
    VkExtent2D e={1920,1080};
    assert(img_new(&c,e,VK_FORMAT_R8G8B8A8_UNORM)&&allocs==1&&c.budget.used==4096);
    lsfg_chain_record_init(&c,(VkCommandBuffer)(uintptr_t)1);assert(layouts==1);
    assert(!img_new(&c,e,VK_FORMAT_R8G8B8A8_UNORM)&&allocs==1&&c.bad&&c.budget.used==4096);
    for(fail=1;fail<=5;++fail){
        c=(lsfg_chain){.budget={8192,0}};allocs=0;
        assert(!img_new(&c,e,VK_FORMAT_R8G8B8A8_UNORM)&&c.bad);
        assert(c.imgs[0].img==(fail>=2?(VkImage)(uintptr_t)11:VK_NULL_HANDLE));
        assert(c.imgs[0].mem==(fail>=4?(VkDeviceMemory)(uintptr_t)22:VK_NULL_HANDLE));
        assert(!c.imgs[0].view&&c.budget.used==(fail>=4?4096:0));
        assert(allocs==(fail>=3?1:0));
    }
    atomic_int cancel=0;fail=0;allocs=0;
    c=(lsfg_chain){.budget={8192,0},.cancel=&cancel};
    assert(img_new(&c,e,VK_FORMAT_R8G8B8A8_UNORM)&&allocs==1);
    atomic_store(&cancel,1);
    assert(!img_new(&c,e,VK_FORMAT_R8G8B8A8_UNORM)&&allocs==1&&c.bad);
    lsfg_graph_budget b={UINT64_MAX,UINT64_MAX-1};
    assert(!lsfg_graph_charge(&b,2)&&b.used==UINT64_MAX-1);
    assert(lsfg_graph_charge(&b,1)&&b.used==UINT64_MAX);
    assert(!lsfg_graph_fits(&b,1));
    return 0;
}'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'case.c';p.write_text(code);exe=p.with_suffix('')
            headers=ROOT/'nxnative/dm/third_party/vulkan-headers/include'
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers',
                            '-I',str(headers),'-I',str(ANDROID),str(p),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
