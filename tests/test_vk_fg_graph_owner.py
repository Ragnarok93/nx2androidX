"""Exercise the actual two-graph owner with delayed slot retirement."""
from pathlib import Path
import subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
class FGGraphOwner(unittest.TestCase):
    def test_publish_cancel_and_retirement(self):
        code=r'''
#include <assert.h>
#include <stdlib.h>
#include <vulkan/vulkan_core.h>
#include "lsfg/lsfg_graph_owner.h"
struct lsfg_chain {unsigned id;};
static unsigned created,freed[8];
static lsfg_modules mods;
static VkImageView views[3]={(VkImageView)(uintptr_t)1,(VkImageView)(uintptr_t)2,(VkImageView)(uintptr_t)3};
lsfg_chain *lsfg_chain_build(const lsfg_modules *m,VkExtent2D e,float f,const VkImageView v[3],uint64_t cap){
    assert(m==&mods&&e.width==1920&&e.height==1080&&f>=.25f&&cap==4096&&v[0]==views[0]);
    lsfg_chain *c=malloc(sizeof *c);assert(c);c->id=++created;return c;
}
lsfg_chain *lsfg_chain_build_cancelable(const lsfg_modules *m,VkExtent2D e,float f,const VkImageView v[3],uint64_t cap,const atomic_int *cancel){
    (void)cancel;return lsfg_chain_build(m,e,f,v,cap); // emulate driver returning a late success
}
void lsfg_chain_free(lsfg_chain *c){if(c){assert(!freed[c->id]++);free(c);}}
static int start(lsfg_graph_owner *o,unsigned pct,uint64_t epoch){
    return lsfg_graph_owner_start(o,&mods,(VkExtent2D){1920,1080},pct,views,4096,epoch);
}
static void poll(lsfg_graph_owner *o,uint64_t epoch){
    int r;do{r=lsfg_graph_owner_poll(o,epoch);}while(!r);assert(r==1);
}
int main(void){
    lsfg_graph_owner o={0};
    lsfg_chain *active=lsfg_chain_build(&mods,(VkExtent2D){1920,1080},.5f,views,4096);
    assert(start(&o,40,1));assert(!start(&o,30,1));poll(&o,1);
    assert(o.phase==LSFG_GRAPH_WARMING&&active->id==1&&o.extra->id==2);
    lsfg_graph_owner_submit(&o,0,1,1,1,1,10);
    lsfg_graph_owner_submit(&o,1,1,1,1,1,11);
    lsfg_graph_owner_submit(&o,2,0,1,1,1,12);
    assert(!lsfg_graph_owner_ready(&o,1,13)&&o.extra_slots==3);
    lsfg_graph_owner_submit(&o,2,1,1,1,1,12);
    assert(lsfg_graph_owner_ready(&o,1,13));
    lsfg_graph_owner_retire_slot(&o,0);
    assert(!lsfg_graph_owner_publish(&o,&active,0,1,13,0)&&active->id==1);
    assert(!lsfg_graph_owner_publish(&o,&active,0,2,13,1));
    assert(lsfg_graph_owner_publish(&o,&active,0,1,13,1)&&active->id==2);
    assert(o.phase==LSFG_GRAPH_RETIRING&&o.extra_slots==6&&o.active_slots==7);
    assert(!freed[1]&&!start(&o,30,1));
    lsfg_graph_owner_retire_slot(&o,1);assert(!freed[1]);
    lsfg_graph_owner_retire_slot(&o,2);assert(freed[1]&&o.phase==LSFG_GRAPH_EMPTY);
    assert(start(&o,30,2));poll(&o,2);
    lsfg_graph_owner_retire_slot(&o,0);
    lsfg_graph_owner_submit(&o,0,1,1,1,2,20);
    lsfg_graph_owner_cancel(&o);assert(!freed[3]&&o.phase==LSFG_GRAPH_RETIRING);
    assert(!start(&o,25,2));
    lsfg_graph_owner_retire_slot(&o,0);assert(freed[3]&&o.phase==LSFG_GRAPH_EMPTY);
    assert(start(&o,25,3));lsfg_graph_owner_cancel(&o);
    int r;do{r=lsfg_graph_owner_poll(&o,3);}while(!r);assert(r==-1&&freed[4]);
    lsfg_graph_owner_clear_idle(&o);lsfg_chain_free(active);
    assert(freed[2]&&created==4);
    return 0;
}'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'case.c';p.write_text(code);exe=p.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-pthread',
                '-I',str(ROOT/'nxnative/dm/third_party/vulkan-headers/include'),
                '-I',str(ROOT/'nxnative/dm/android'),str(p),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True,timeout=10)

    def test_nonblocking_retirement_without_slot_reuse(self):
        source=(ROOT/'nxnative/dm/android/dm_framegen.c').read_text()
        start=source.index('static void fg_collect_retired(')
        end=source.index('static void fg_cancel_request(',start)
        code=r'''
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <vulkan/vulkan_core.h>
enum {NFG=3,LSFG_GRAPH_EMPTY,LSFG_GRAPH_BUILDING,LSFG_GRAPH_RETIRING};
typedef struct {VkFence fence;int pending;} fg_slot;
typedef struct {unsigned phase,extra_slots;struct {atomic_int cancelled;} job;} owner;
static struct {owner graph;uint64_t timing_epoch;fg_slot fr[NFG];} F;
static struct {VkDevice dev;} V;
static unsigned signalled,forgotten,retired,polled;
static VKAPI_ATTR VkResult VKAPI_CALL status(VkDevice dev,VkFence f){
    assert(dev==V.dev);return signalled&(1u<<((uintptr_t)f-1))?VK_SUCCESS:VK_NOT_READY;
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL get_proc(VkDevice dev,const char *name){
    assert(dev==V.dev&&!strcmp(name,"vkGetFenceStatus"));
    return (PFN_vkVoidFunction)status;
}
static struct {PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;} vk={get_proc};
static void vk_fg_fence_retired(VkFence f){forgotten|=1u<<((uintptr_t)f-1);}
static void lsfg_graph_owner_retire_slot(owner *o,unsigned i){
    assert(o==&F.graph);retired|=1u<<i;o->extra_slots&=~(1u<<i);
    if(!o->extra_slots)o->phase=LSFG_GRAPH_EMPTY;
}
static int lsfg_graph_owner_poll(owner *o,uint64_t epoch){
    assert(o==&F.graph&&epoch==F.timing_epoch&&atomic_load(&o->job.cancelled));
    ++polled;o->phase=LSFG_GRAPH_EMPTY;return -1;
}
'''+source[start:end]+r'''
int main(void){
    F.graph.phase=LSFG_GRAPH_BUILDING;fg_collect_retired();assert(!polled);
    atomic_store(&F.graph.job.cancelled,1);fg_collect_retired();assert(polled==1);
    F.graph.phase=LSFG_GRAPH_RETIRING;F.graph.extra_slots=7;
    for(unsigned i=0;i<3;i++)F.fr[i]=(fg_slot){(VkFence)(uintptr_t)(i+1),1};
    signalled=1;fg_collect_retired();
    assert(F.graph.phase==LSFG_GRAPH_RETIRING&&F.graph.extra_slots==6&&forgotten==1&&retired==1);
    assert(!F.fr[0].pending&&F.fr[1].pending&&F.fr[2].pending);
    fg_collect_retired();assert(retired==1);
    signalled=7;fg_collect_retired();
    assert(F.graph.phase==LSFG_GRAPH_EMPTY&&forgotten==7&&retired==7);
    return 0;
}'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'case.c';p.write_text(code);exe=p.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',
                str(ROOT/'nxnative/dm/third_party/vulkan-headers/include'),str(p),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True,timeout=10)
