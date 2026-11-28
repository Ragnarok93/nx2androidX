"""Count real history initialization commands across image-driven and native resets."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class Gsr2ResetClear(unittest.TestCase):
    def test_initialized_image_driven_reset_does_not_clear_history(self):
        src = (ROOT / 'nxnative/dm/android/gfx_vk_gsr2.c').read_text()
        start = src.index('static void gsr2_prepare_images(')
        prepare = src[start:src.index('\n}', start) + 2]
        code = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
typedef uintptr_t VkImage, VkCommandBuffer;
typedef struct { float value[4]; } VkClearColorValue;
typedef struct { int aspect, base, levels, layer, layers; } VkImageSubresourceRange;
typedef struct { int type; void *next; int src,dst,old_layout,new_layout,q1,q2; VkImage image;
    VkImageSubresourceRange range; } VkImageMemoryBarrier;
enum { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER=1, VK_ACCESS_MEMORY_READ_BIT=1,
    VK_ACCESS_MEMORY_WRITE_BIT=2, VK_IMAGE_LAYOUT_UNDEFINED=0, VK_IMAGE_LAYOUT_GENERAL=1,
    VK_QUEUE_FAMILY_IGNORED=-1, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT=1,
    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT=2 };
enum { G2_CLIP,G2_MOTION,G2_YCOCG,G2_RESULT,G2_HISTORY0,G2_HISTORY1,G2_LUMA0,G2_LUMA1,G2_IMAGES };
enum { G2_PREV=20,G2_PREV_LUMA };
typedef struct { int initialized,image_driven; unsigned next; struct {VkImage image;} images[G2_IMAGES]; } vk_gsr2;
static VkImageSubresourceRange temporal_color={1,0,1,0,1};
static unsigned transitions,clears,barriers; static VkImage last_clear;
static unsigned gsr2_image_index(const vk_gsr2 *c,unsigned i) {
    assert(i==G2_PREV||i==G2_PREV_LUMA);return (i==G2_PREV?G2_HISTORY0:G2_LUMA0)+(c->next^1);
}
static void vkCmdPipelineBarrier(VkCommandBuffer cb,int a,int b,int c,int d,void *e,int f,void *g,int n,VkImageMemoryBarrier *i) {
    (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;
    assert(cb&&n==1&&i->image&&i->old_layout==VK_IMAGE_LAYOUT_UNDEFINED);transitions++;
}
static void vkCmdClearColorImage(VkCommandBuffer cb,VkImage i,int layout,VkClearColorValue *v,int n,VkImageSubresourceRange *r) {
    assert(cb&&i&&layout==VK_IMAGE_LAYOUT_GENERAL&&v->value[0]==0&&n==1&&r==&temporal_color);clears++;last_clear=i;
}
static void vk_img_barrier_all(VkCommandBuffer cb) {assert(cb);barriers++;}
''' + prepare + r'''
int main(void) {
    vk_gsr2 c={.image_driven=1};
    c.images[G2_CLIP].image=1;c.images[G2_HISTORY0].image=2;c.images[G2_HISTORY1].image=3;
    gsr2_prepare_images(&c,1,1);
    assert(transitions==3&&clears==1&&last_clear==3&&barriers==1);
    c.initialized=1;c.next=1;
    gsr2_prepare_images(&c,1,0);
    assert(transitions==3&&clears==1&&barriers==1);
    gsr2_prepare_images(&c,1,1);
    assert(transitions==3&&clears==1&&barriers==1); // shader rejects history on reset
    c.image_driven=0;c.images[G2_LUMA0].image=4;c.images[G2_LUMA1].image=5;
    gsr2_prepare_images(&c,1,1);
    assert(transitions==3&&clears==3&&last_clear==4&&barriers==2);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'case.c';p.write_text(code);exe=p.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
