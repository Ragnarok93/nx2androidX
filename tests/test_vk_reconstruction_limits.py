"""Host-only resource and sharpness policy regression; not GPU readback."""
import pathlib
import subprocess
import tempfile
import unittest

class ReconstructionLimits(unittest.TestCase):
    def test_extent_memory_and_slider(self):
        source = r'''
#include <assert.h>
#include "gfx_vk_reconstruction_limits.h"
int main(void) {
    const uint64_t heap = UINT64_C(8) * 1024 * DM_RECON_MIB;
    assert(dm_recon_extent_ok(2560,1440,8192,8192));
    assert(dm_recon_extent_ok(3840,2160,8192,8192));
    assert(!dm_recon_extent_ok(8193,2160,8192,8192));
    assert(!dm_recon_extent_ok(0,2160,8192,8192));
    uint64_t budget = dm_recon_budget(heap);
    assert(budget == 256 * DM_RECON_MIB);
    assert(dm_recon_fits(budget, 0, UINT64_C(3840)*2160*16));
    assert(!dm_recon_fits(dm_recon_budget(1024*DM_RECON_MIB),0,UINT64_C(2560)*1440*16));
    assert(!dm_recon_fits(10,11,0));
    assert(!dm_recon_fits(10,5,6));
    assert(dm_recon_sharpness(-5) == 0.0f);
    assert(dm_recon_sharpness(0) == 0.0f);
    assert(dm_recon_sharpness(50) == 0.5f);
    assert(dm_recon_sharpness(100) == 1.0f);
    assert(dm_recon_sharpness(120) == 1.0f);
    return 0;
}
'''
        root = pathlib.Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as d:
            f = pathlib.Path(d) / 'test.c'; f.write_text(source)
            exe = pathlib.Path(d) / 'test'
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(root/'nxnative/dm/android'),str(f),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
if __name__ == '__main__': unittest.main()
