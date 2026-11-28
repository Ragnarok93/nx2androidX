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
