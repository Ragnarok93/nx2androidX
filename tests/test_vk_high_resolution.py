"""Verify heap-relative admission and exact aspect fitting (no GPU required)."""
import pathlib, subprocess, tempfile, unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]

class HighResolution(unittest.TestCase):
    def test_large_output_admission(self):
        source=r'''
#include <assert.h>
#include "gfx_vk_reconstruction_limits.h"
int main(void) {
    uint64_t heap = 8 * UINT64_C(1024) * DM_RECON_MIB;
    // 5K history alone is 225 MiB. A 192 MiB constant prevents it even
    // if the device heap permits the allocation. 4K history is 126.6 MiB.
    assert(dm_recon_budget(heap) == 256 * DM_RECON_MIB);
    assert(dm_recon_fits(dm_recon_budget(heap), 0, UINT64_C(5120)*2880*16));
    assert(dm_recon_fits(dm_recon_budget(heap), 0, UINT64_C(5120)*2880*8 + UINT64_C(2560)*1440*8));
    assert(!dm_recon_fits(dm_recon_budget(1024*DM_RECON_MIB),0,UINT64_C(3840)*2160*16));
    assert(dm_recon_budget(0)==0);
    uint32_t w,h;
    dm_recon_picture_extent(1920,1080,2560,1440,&w,&h); assert(w==2560&&h==1440);
    dm_recon_picture_extent(1920,1080,3840,2160,&w,&h); assert(w==3840&&h==2160);
    dm_recon_picture_extent(2560,1440,5120,2880,&w,&h); assert(w==5120&&h==2880);
    dm_recon_picture_extent(1920,1080,7680,4320,&w,&h); assert(w==7680&&h==4320);
    dm_recon_picture_extent(1920,1080,3440,1440,&w,&h); assert(w==2560&&h==1440);
    dm_recon_picture_extent(1920,1080,1440,3200,&w,&h); assert(w==1440&&h==810);
    dm_recon_picture_extent(1325,720,3840,2160,&w,&h); assert(w==3840&&h==2086);
    dm_recon_picture_extent(0,720,3840,2160,&w,&h); assert(!w&&!h);
    dm_recon_picture_extent(UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,&w,&h); assert(w==UINT32_MAX&&h==UINT32_MAX);
}
'''
        with tempfile.TemporaryDirectory() as d:
            f=pathlib.Path(d)/'check.c'; f.write_text(source); exe=f.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(ROOT/'nxnative/dm/android'),str(f),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True,cwd=d)

if __name__=='__main__': unittest.main()
