import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

SOURCE = r"""
#include <assert.h>
#include "gfx_vk_resolution_preflight.h"
int main(void) {
    uint64_t a=0,n=0;
    assert(nx_rs_buffer_span(0x1000,0x2000,0x1180,0x100,256,&a,&n));
    assert(a==0x1100 && n==0x180);
    assert(nx_rs_range_overlap(a,n,0x1100,1));
    assert(!nx_rs_range_overlap(a,n,0x1280,1));
    assert(!nx_rs_buffer_span(0x1000,0x2000,0x1f80,0x100,256,&a,&n));
    assert(!nx_rs_buffer_span(0x1000,0x2000,0x1000,0,256,&a,&n));
    assert(!nx_rs_buffer_span(0x1000,0x2000,0xfff,4,256,&a,&n));
    assert(!nx_rs_buffer_span(0x1000,0x2000,0x1100,4,0,&a,&n));
    assert(!nx_rs_buffer_span(0x1000,0x2000,0x1100,UINT64_MAX,256,&a,&n));
    assert(nx_rs_buffer_span(0x1003,0x2000,0x1100,4,256,&a,&n));
    assert(a==0x1003 && n==0x101);
    assert(nx_rs_buffer_span(UINT64_MAX-511,UINT64_MAX,UINT64_MAX-15,15,256,&a,&n));
    assert(nx_rs_indirect_span(1,0x2000,0x3000,0,4,20,&n) && n==80);
    assert(nx_rs_indirect_span(1,0x203c,0x3000,3,4,20,&n) && n==20);
    assert(nx_rs_indirect_span(0,0x2000,0,0,0,0,&n) && n==16);
    assert(!nx_rs_indirect_span(1,0x2000,0x3000,4,4,20,&n));
    assert(!nx_rs_indirect_span(1,0x2000,0x3000,0,4,16,&n));
    assert(!nx_rs_indirect_span(1,0x2001,0,0,0,0,&n));
    assert(!nx_rs_indirect_span(1,0,0x3000,0,4,20,&n));
    return 0;
}
"""

class ResolutionBufferSpanTests(unittest.TestCase):
    def test_descriptor_span(self):
        cc = shutil.which("cc") or shutil.which("clang")
        if not cc:
            self.skipTest("C compiler required")
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "span.c"
            exe = Path(tmp) / "span"
            src.write_text(SOURCE, encoding="utf-8")
            subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT / "nxnative/dm/gfx/nvn"),
                            str(src), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

if __name__ == "__main__":
    unittest.main()
