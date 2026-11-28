"""Repeated sampler lookups preserve registry version and filtering identity."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class SamplerMemo(unittest.TestCase):
    def test_registry_generation_snapshot_and_nearest(self):
        src=(ROOT/'nxnative/dm/android/gfx_vk_exec.c').read_text()
        start=src.index('static VkSampler samp_cached(')
        fn=src[start:src.index('\n}',start)+2]
        # Adapt the old function's call shape while keeping its real behavior.
        if 'uint64_t seq' not in fn:
            fn=fn.replace('int sid)', 'int sid, uint64_t seq, int nearest)').replace('    enum { SC', '    if (seq) return vk_samp_at(sid,seq,nearest,NULL,0,NULL);\n    if (nearest) return vk_samp_by_id_nearest(sid);\n    enum { SC')
        code=r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
typedef uintptr_t VkSampler;
#define VK_NULL_HANDLE 0
static uint64_t g_samp_gen=1;
static unsigned calls,fail;
static VkSampler lookup(int sid,uint64_t seq,int nearest){++calls;return fail?0:(VkSampler)(100000*g_samp_gen+100*sid+2*seq+nearest);}
static VkSampler vk_samp_by_id(int sid){return lookup(sid,0,0);}
static VkSampler vk_samp_by_id_nearest(int sid){return lookup(sid,0,1);}
static VkSampler vk_samp_at(int sid,uint64_t seq,int nearest,char *d,size_t n,int *caps){(void)d;(void)n;(void)caps;return lookup(sid,seq,nearest);}
'''+fn+r'''
int main(void){
    VkSampler a=samp_cached(7,0,0);unsigned n=calls;
    for(int i=0;i<100;i++)assert(samp_cached(7,0,0)==a);
    assert(calls==n);
    VkSampler b=samp_cached(7,0,1);assert(a!=b);n=calls;
    for(int i=0;i<100;i++)assert(samp_cached(7,0,1)==b);
    assert(calls==n);
    VkSampler c=samp_cached(7,15,1);assert(c!=b);n=calls;
    for(int i=0;i<100;i++)assert(samp_cached(7,15,1)==c);
    assert(calls==n);
    assert(samp_cached(7,16,1)!=c);
    ++g_samp_gen;assert(samp_cached(7,15,1)!=c);
    fail=1;assert(!samp_cached(8,0,1));fail=0;assert(samp_cached(8,0,1));
    n=calls;assert(!samp_cached(0,0,0)&&calls==n);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'case.c';p.write_text(code);exe=p.with_suffix('')
            subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror',str(p),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
