"""Submission-local descriptor cache must retain colliding live keys."""
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
class SetCache(unittest.TestCase):
    def test_collision_equality_and_epoch(self):
        source = r'''
#include <assert.h>
#include <stdint.h>
typedef uintptr_t VkDescriptorSet;
#include "gfx_vk_set_cache.h"
int main(void) {
    vk_set_cache_entry entries[8]={0};
    uint64_t arena[]={1,2,3,4,5,6,7,8};
    VkDescriptorSet hit;
    vk_set_cache_entry *a=vk_set_cache_find(entries,8,arena,1,1,arena,2,&hit);
    assert(!hit); *a=(vk_set_cache_entry){1,1,11,0,2};
    vk_set_cache_entry *b=vk_set_cache_find(entries,8,arena,1,9,arena+2,2,&hit);
    assert(!hit && a!=b); *b=(vk_set_cache_entry){9,1,22,2,2};
    assert(vk_set_cache_find(entries,8,arena,1,1,arena,2,&hit)==a && hit==11);
    assert(vk_set_cache_find(entries,8,arena,1,9,arena+2,2,&hit)==b && hit==22);
    // Even equal hashes must compare full descriptor content.
    vk_set_cache_entry *c=vk_set_cache_find(entries,8,arena,1,1,arena+4,2,&hit);
    assert(!hit && c!=a && c!=b); *c=(vk_set_cache_entry){1,1,33,4,2};
    assert(vk_set_cache_find(entries,8,arena,1,1,arena+4,2,&hit)==c && hit==33);
    vk_set_cache_entry *d=vk_set_cache_find(entries,8,arena,1,17,arena+6,2,&hit);
    assert(d && !hit); *d=(vk_set_cache_entry){17,1,44,6,2};
    assert(!vk_set_cache_find(entries,8,arena,1,25,arena,2,&hit) && !hit);
    assert(vk_set_cache_find(entries,8,arena,1,1,arena,2,&hit)==a && hit==11);
    assert(vk_set_cache_find(entries,8,arena,2,1,arena,2,&hit) && !hit);
    return 0;
}'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'case.c';p.write_text(source);exe=p.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(ROOT/'nxnative/dm/android'),str(p),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
