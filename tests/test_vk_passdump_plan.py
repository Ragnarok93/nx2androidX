"""Compile and execute the production Vulkan pass-dump slot/budget/hash helpers."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "nxnative/dm/android/gfx_vk_passdump_plan.h"

@unittest.skipUnless(shutil.which("cc"), "host C compiler required")
class PassDumpSelectionTests(unittest.TestCase):
    def test_sparse_mrt_selection_bounds_and_byte_exact_hash(self):
        c = r'''
#include <assert.h>
#include <stdint.h>
#include "gfx_vk_passdump_plan.h"
int main(void) {
    uint8_t slots[8] = {0};
    const uint8_t sparse[8] = {0,1,0,1,1,0,0,1};
    assert(vk_pd_select_colors(sparse,8,-1,0,8,slots)==1 && slots[0]==1);
    assert(vk_pd_select_colors(sparse,8,4,0,8,slots)==1 && slots[0]==4);
    assert(vk_pd_select_colors(sparse,8,2,0,8,slots)==1 && slots[0]==1);
    assert(vk_pd_select_colors(sparse,8,-1,1,8,slots)==4);
    assert(slots[0]==1 && slots[1]==3 && slots[2]==4 && slots[3]==7);
    assert(vk_pd_select_colors(sparse,8,-1,1,2,slots)==2 && slots[0]==1 && slots[1]==3);
    assert(vk_pd_select_colors(sparse,8,-1,1,0,slots)==0);
    assert(vk_pd_select_colors(sparse,0,-1,1,8,slots)==0);
    uint8_t empty[8]={0}; assert(vk_pd_select_colors(empty,8,-1,0,8,slots)==0);
    assert(vk_pd_pass_has_draws(0,0));
    assert(!vk_pd_pass_has_draws(0,1));
    assert(!vk_pd_pass_has_draws(1,10));
    assert(vk_pd_pass_has_draws(10,10));
    assert(vk_pd_pass_has_draws(107,10));
    assert(vk_pd_pass_has_draws(UINT64_MAX,10));
    assert(vk_pd_depth_prepass_eligible(0,1,91,10,0));
    assert(!vk_pd_depth_prepass_eligible(0,1,0,10,0));
    assert(!vk_pd_depth_prepass_eligible(2,1,91,10,0));
    assert(!vk_pd_depth_prepass_eligible(0,0,91,10,0));
    assert(!vk_pd_depth_prepass_eligible(0,1,91,10,1));
    uint32_t ctrl[] = {0x009fffffu,0xab9ffffeu,0x009fffffu,0x00a00000u,0x00000000u};
    assert(vk_pd_d24_matches(ctrl,5,0x9fffffu,1)==4);
    assert(vk_pd_d24_matches(ctrl,5,0x9fffffu,0)==2);
    assert(vk_pd_d24_matches(ctrl+4,1,0x9fffffu,1)==0);
    assert(vk_pd_budget_allows(0,64,64));
    assert(!vk_pd_budget_allows(0,65,64));
    assert(!vk_pd_budget_allows(UINT64_MAX,1,64));
    assert(!vk_pd_budget_allows(0,0,64));
    assert(vk_pd_hash("",0)==UINT64_C(0xcbf29ce484222325));
    assert(vk_pd_hash("hello",5)==UINT64_C(0xa430d84680aabd0b));
    uint8_t r[4]={0,1,2,3}; assert(vk_pd_hash(r,4)!=vk_pd_hash(r,3));
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / "case.c"
            exe = Path(folder) / "case"
            source.write_text(c)
            subprocess.run(["cc","-std=c11","-Wall","-Wextra","-Werror",
                            "-I",str(HEADER.parent),str(source),"-o",str(exe)],check=True)
            subprocess.run([str(exe)],check=True,timeout=10)

if __name__ == "__main__":
    unittest.main()
