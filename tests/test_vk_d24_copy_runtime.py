"""Execute C conversion used by the real NVN D24 readback path."""
import shutil, subprocess, tempfile, unittest
from pathlib import Path
HEADER = Path(__file__).resolve().parents[1] / "nxnative/dm/android/gfx_vk_d24_copy.h"
@unittest.skipUnless(shutil.which("cc"), "host C compiler required")
class D24TransferTests(unittest.TestCase):
    def test_float_to_nvn_packed_d24(self):
        code = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "gfx_vk_d24_copy.h"
int main(void) {
    uint32_t fbits=0x3f200000u; float f=0;
    memcpy(&f,&fbits,4);
    uint32_t d=vk_d24_copy_float_to_unorm(f);
    assert(d>=0x9ffffeu && d<=0xa00000u);
    assert(vk_d24_copy_float_to_unorm(0.f)==0);
    assert(vk_d24_copy_float_to_unorm(-2.f)==0);
    assert(vk_d24_copy_float_to_unorm(1.f)==0xffffffu);
    assert(vk_d24_copy_float_to_unorm(2.f)==0xffffffu);
    uint32_t qnan=0x7fc00000u;memcpy(&f,&qnan,4);
    assert(vk_d24_copy_float_to_unorm(f)==0);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as path:
            d=Path(path);(d/"test.c").write_text(code)
            subprocess.run(["cc","-std=c11","-Wall","-Wextra","-Werror","-I",str(HEADER.parent),str(d/"test.c"),"-o",str(d/"test")],check=True)
            subprocess.run([str(d/"test")],check=True,timeout=10)
if __name__ == "__main__":
    unittest.main()
