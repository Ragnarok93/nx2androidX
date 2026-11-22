"""Exercise the production image suballocation stride/alignment planner in C."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

HEADER = Path(__file__).resolve().parents[1] / "nxnative/dm/android/gfx_vk_img_alloc_plan.h"

@unittest.skipUnless(shutil.which("cc"), "C compiler required")
class ImageSlotAlignmentTests(unittest.TestCase):
    def test_all_class_sizes_and_supported_alignments(self):
        code = r'''
#include <assert.h>
#include <stdint.h>
#include "gfx_vk_img_alloc_plan.h"
static uint64_t cls(unsigned c) { return (c & 1 ? 6144ull : 4096ull) << (c >> 1); }
int main(void) {
    assert(vk_img_aligned_stride(6144,4096) == 8192);
    assert(vk_img_aligned_stride(6144,2048) == 6144);
    for (unsigned c=0; c<18; c++) for (uint64_t a=1;a<=4096;a*=2) {
        uint64_t size=cls(c), stride=vk_img_aligned_stride(size,a);
        assert(stride>=size && stride-size<a);
        assert(stride % a == 0);
        // All 64 possible slots and their bound ranges meet the full
        // original image requirements, not merely the first slot.
        for (uint64_t i=0;i<64;i++) {
            uint64_t offset=i*stride;
            assert(offset % a == 0);
            assert(offset+size <= stride*64);
        }
    }
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="nx2a-vk-img-align-") as d:
            root=Path(d)
            (root/"test.c").write_text(code)
            subprocess.run(["cc","-std=c11","-Wall","-Wextra","-Werror",
                "-I",str(HEADER.parent),str(root/"test.c"),
                "-o",str(root/"test")],check=True,timeout=30)
            subprocess.run([str(root/"test")],check=True,timeout=10)

if __name__ == "__main__":
    unittest.main()
