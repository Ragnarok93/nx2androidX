"""Run the graph's actual shared image-layout traversal with a fake resource factory."""
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

class FGPreflightPlan(unittest.TestCase):
    def test_same_graph_layout_used_for_measurement_and_allocation(self):
        android=ROOT/'nxnative/dm/android'
        source=(android/'lsfg/lsfg_chain.c').read_text()
        modern='static int graph_images_new(' in source
        if modern:
            start=source.index('static VkExtent2D halve(')
            end=source.index('\n}',source.index('static int graph_images_new('))+2
            block=source[start:end]
        else:
            block=''
        code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#define LSFG_FORMAT 37
#define LSFG_FLOW_FORMAT 9
#define LSFG_MOTION_FORMAT 97
enum {HIST=3,MIPS=7,BETA_OUTPUTS=6,DELTA_N=3,FIRST_DELTA_LEVEL=4,MAX_IMAGES=192};
typedef int VkFormat;
typedef struct {uint32_t width,height;} VkExtent2D;
typedef struct {VkExtent2D ext;VkFormat fmt;} limg;
typedef struct {
    int bad,nimg;
    limg imgs[MAX_IMAGES];limg *frames[2],*mip[MIPS];VkExtent2D flow_ext;
    struct {limg *input,*t1,*t2,*t3[2],*out[HIST][2];} alpha[MIPS];
    limg *beta_t1[2],*beta_t2[2],*beta_out[BETA_OUTPUTS];
    struct {limg *t1[3],*t2[2],*out;} gamma[MIPS];
    struct {limg *t1[3],*t2[2],*out1,*out2;} delta[DELTA_N];
} lsfg_chain;
static int chain_stopped(lsfg_chain *c){return c->bad;}
static int fail_at;
static limg *create(lsfg_chain *c,VkExtent2D e,VkFormat f){
    if(c->bad)return NULL;
    if(fail_at&&c->nimg+1==fail_at){c->bad=1;return NULL;}
    assert(c->nimg<MAX_IMAGES);
    limg *im=&c->imgs[c->nimg++];im->ext=(VkExtent2D){e.width?e.width:1,e.height?e.height:1};im->fmt=f;return im;
}
''' + block + r'''
int main(void){
    lsfg_chain a={0},b={0};
#ifndef SHARED_LAYOUT
    (void)a;(void)b;(void)chain_stopped;(void)create;
    fprintf(stderr,"no shared graph layout for allocation-free preflight\n");return 1;
#else
    for(unsigned pct=25;pct<=100;pct+=5){
        memset(&a,0,sizeof a);memset(&b,0,sizeof b);
        assert(graph_images_new(&a,(VkExtent2D){1921,1081},pct/100.f,create));
        assert(graph_images_new(&b,(VkExtent2D){1921,1081},pct/100.f,create));
        assert(a.nimg==152&&b.nimg==152);
        assert(!memcmp(a.imgs,b.imgs,sizeof a.imgs));
        assert(a.frames[0]->ext.width==1921&&a.frames[0]->fmt==LSFG_FORMAT);
        assert(a.flow_ext.width==(uint32_t)(1921*(pct/100.f)));
        assert(a.gamma[MIPS-1].out->ext.width==a.alpha[0].out[0][0]->ext.width);
        assert(a.delta[DELTA_N-1].out1->fmt==LSFG_MOTION_FORMAT);
    }
    // Failure in every creation position must not dereference a null image.
    for(fail_at=1;fail_at<=152;fail_at++){
        memset(&a,0,sizeof a);
        assert(!graph_images_new(&a,(VkExtent2D){1,1},.25f,create)&&a.bad);
        assert(a.nimg==fail_at-1);
    }
#endif
}
'''
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'plan.c';p.write_text(code);exe=p.with_suffix('')
            cmd=['cc','-std=c11','-Wall','-Wextra','-Werror',str(p),'-o',str(exe)]
            if modern:cmd.append('-DSHARED_LAYOUT')
            subprocess.run(cmd,check=True)
            r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=5)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
