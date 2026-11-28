"""Check the real graph image allocator distinguishes policy rejection and driver OOM."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class FGAllocationOrigin(unittest.TestCase):
    def test_policy_rejection_does_not_masquerade_as_driver_oom(self):
        android = ROOT / 'nxnative/dm/android'
        src = (android / 'lsfg/lsfg_chain.c').read_text()
        start = src.index('static int chain_stopped(')
        end = src.index('\n}', src.index('static limg *img_new('))+2
        code = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include "lsfg/lsfg_graph_budget.h"
#define VK_NULL_HANDLE 0
#define VK_SUCCESS 0
#define VK_ERROR_OUT_OF_DEVICE_MEMORY (-2)
#define MAX_IMAGES 4
#define VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO 1
#define VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO 2
#define VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO 3
#define VK_IMAGE_TYPE_2D 1
#define VK_SAMPLE_COUNT_1_BIT 1
#define VK_IMAGE_TILING_OPTIMAL 1
#define VK_IMAGE_USAGE_TRANSFER_SRC_BIT 1
#define VK_IMAGE_USAGE_TRANSFER_DST_BIT 2
#define VK_IMAGE_USAGE_STORAGE_BIT 4
#define VK_IMAGE_USAGE_SAMPLED_BIT 8
#define VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT 1
#define VK_IMAGE_VIEW_TYPE_2D 1
#define VK_IMAGE_ASPECT_COLOR_BIT 1
typedef int VkResult,VkImage,VkFormat,VkImageView,VkDeviceMemory;
typedef struct {uint32_t width,height;} VkExtent2D;
typedef struct {int sType,imageType,format;struct {unsigned w,h,d;} extent;int mipLevels,arrayLayers,samples,tiling,usage;} VkImageCreateInfo;
typedef struct {uint64_t size;uint32_t memoryTypeBits;} VkMemoryRequirements;
typedef struct {int sType;uint64_t allocationSize;uint32_t memoryTypeIndex;} VkMemoryAllocateInfo;
typedef struct {int sType,image,viewType,format;int subresourceRange[5];} VkImageViewCreateInfo;
typedef struct {VkImage img;VkDeviceMemory mem;VkImageView view;VkExtent2D ext;VkFormat fmt;} limg;
typedef struct {int bad,nimg;lsfg_graph_budget budget;const atomic_int *cancel;limg imgs[MAX_IMAGES];} lsfg_chain;
static struct {int dev;struct {unsigned memoryTypeCount;struct {unsigned heapIndex;} memoryTypes[2];} mem;} V={.mem={2,{{0},{1}}}};
static unsigned allocs,fail;
static uint64_t req_bytes=8294400;
static char message[512];
#define VRED(...) snprintf(message,sizeof message,__VA_ARGS__)
#define VLOG(...) VRED(__VA_ARGS__)
static VkResult create(int d,const VkImageCreateInfo *i,void *a,VkImage *o){(void)d;(void)i;(void)a;*o=11;return 0;}
static void req(int d,VkImage i,VkMemoryRequirements *r){(void)d;assert(i==11);*r=(VkMemoryRequirements){req_bytes,2};}
static uint32_t vka_mem_type(uint32_t b,int f,int a){(void)b;(void)f;(void)a;return fail==2?UINT32_MAX:1;}
static VkResult alloc(int d,const VkMemoryAllocateInfo *i,void *a,VkDeviceMemory *o){(void)d;(void)a;assert(i->allocationSize==req_bytes);++allocs;*o=22;return fail==3?VK_ERROR_OUT_OF_DEVICE_MEMORY:0;}
static VkResult bind(int d,VkImage i,VkDeviceMemory m,int o){(void)d;assert(i==11&&m==22&&!o);return 0;}
static VkResult view(int d,const VkImageViewCreateInfo *i,void *a,VkImageView *o){(void)d;(void)a;assert(i->image==11);*o=33;return 0;}
static struct {VkResult (*vkCreateImage)(int,const VkImageCreateInfo*,void*,VkImage*);
    void (*vkGetImageMemoryRequirements)(int,VkImage,VkMemoryRequirements*);
    VkResult (*vkAllocateMemory)(int,const VkMemoryAllocateInfo*,void*,VkDeviceMemory*);
    VkResult (*vkBindImageMemory)(int,VkImage,VkDeviceMemory,int);
    VkResult (*vkCreateImageView)(int,const VkImageViewCreateInfo*,void*,VkImageView*);} vk={create,req,alloc,bind,view};
''' + src[start:end] + r'''
int main(void){
    VkExtent2D e={1920,1080};
    lsfg_chain c={.budget={6*1048576,0}};
    assert(!img_new(&c,e,37)&&c.bad&&!allocs&&c.imgs[0].img==11&&!c.imgs[0].mem);
    if(!strstr(message,"origin=policy_budget")||!strstr(message,"heap=1")){
        fprintf(stderr,"policy rejection indistinguishable: %s\n",message);return 1;
    }
    c=(lsfg_chain){.budget={16*1048576,0}};fail=2;
    assert(!img_new(&c,e,37)&&!allocs&&strstr(message,"origin=memory_type"));
    c=(lsfg_chain){.budget={16*1048576,0}};fail=3;
    assert(!img_new(&c,e,37)&&allocs==1&&!c.imgs[0].mem&&!c.budget.used);
    assert(strstr(message,"origin=driver_allocate")&&strstr(message,"result=-2"));
    c=(lsfg_chain){.budget={16*1048576,0}};fail=0;
    assert(img_new(&c,e,37)&&allocs==2&&c.budget.used==req_bytes);
}
'''
        with tempfile.TemporaryDirectory() as d:
            c=Path(d)/'alloc.c';c.write_text(code);exe=c.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(android),str(c),'-o',str(exe)],check=True)
            run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=5)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
