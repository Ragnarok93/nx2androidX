import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

SOURCE = r"""
#include <assert.h>
#include "gfx_vk_resolution_compute.h"
int main(void) {
    uint32_t w[]={0x07230203u,0x10000,0,10,0, (3u<<16)|14,0,1, (1u<<16)|0};
    assert(nx_rs_compute_logical(w,9));
    w[6]=5348; assert(!nx_rs_compute_logical(w,9));
    w[6]=0; assert(!nx_rs_compute_logical(w,7));
    w[8]=0; assert(!nx_rs_compute_logical(w,9));
    assert(!nx_rs_compute_logical(w,5));
    assert(!nx_rs_compute_logical(0,9));
    uint32_t duplicate[]={0x07230203u,0x10000,0,10,0,(3u<<16)|14,0,1,(3u<<16)|14,0,1};
    assert(!nx_rs_compute_logical(duplicate,11));
    const uint32_t g[]={160,90,1}, l[]={8,8,1};
    uint32_t out[]={99,99,99};
    assert(nx_rs_compute_grid(g,l,1280,720,640,360,out));
    assert(out[0]==80 && out[1]==45 && out[2]==1);
    assert(nx_rs_compute_grid(g,l,1280,720,1280,720,out));
    assert(out[0]==160 && out[1]==90);
    assert(!nx_rs_compute_grid(g,l,1280,720,960,540,out));
    assert(out[0]==160 && out[1]==90);
    assert(!nx_rs_compute_grid(g,l,1280,720,640,352,out));
    assert(!nx_rs_compute_grid(g,l,1279,720,640,360,out));
    assert(!nx_rs_compute_grid(g,l,1280,720,0,0,out));
    const uint32_t z[]={8,8,2}, huge[]={UINT32_MAX,90,1};
    assert(!nx_rs_compute_grid(g,z,1280,720,640,360,out));
    assert(!nx_rs_compute_grid(huge,l,1280,720,640,360,out));
    return 0;
}
"""

class ResolutionComputeTests(unittest.TestCase):
    def test_addressing_model(self):
        cc = shutil.which("cc") or shutil.which("clang")
        if not cc:
            self.skipTest("C compiler required")
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "compute.c"
            exe = Path(tmp) / "compute"
            src.write_text(SOURCE, encoding="utf-8")
            subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT / "nxnative/dm/gfx/nvn"),
                            str(src), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

if __name__ == "__main__":
    unittest.main()
