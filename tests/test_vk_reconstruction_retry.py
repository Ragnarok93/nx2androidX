"""Host test of shared-output retry policy used by the spatial allocator."""
import subprocess
import tempfile
import unittest
from pathlib import Path

class SharedOutputRetry(unittest.TestCase):
    def test_retry_and_geometry(self):
        source = r'''
#include <assert.h>
#include "gfx_vk_reconstruction_limits.h"
int main(void) {
    dm_recon_shared_retry r = {0};
    assert(dm_recon_shared_allowed(&r, 37, 1920, 1080, 100));
    dm_recon_shared_failed(&r, 37, 1920, 1080, 100);
    assert(!dm_recon_shared_allowed(&r, 37, 1920, 1080, 101));
    assert(!dm_recon_shared_allowed(&r, 37, 1920, 1080, 219));
    assert(dm_recon_shared_allowed(&r, 37, 1920, 1080, 220));
    assert(dm_recon_shared_allowed(&r, 37, 2560, 1440, 101));
    assert(dm_recon_shared_allowed(&r, 43, 1920, 1080, 101));
    dm_recon_shared_failed(&r, 37, 2560, 1440, 101);
    assert(!dm_recon_shared_allowed(&r, 37, 2560, 1440, 102));
    assert(dm_recon_shared_allowed(&r, 37, 1920, 1080, 102));
    dm_recon_shared_succeeded(&r);
    assert(dm_recon_shared_allowed(&r, 37, 2560, 1440, 102));
    dm_recon_shared_failed(&r, 37, 3840, 2160, UINT64_MAX-2);
    assert(!dm_recon_shared_allowed(&r, 37, 3840, 2160, UINT64_MAX-1));
    return 0;
}'''
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            (d/'case.c').write_text(source)
            inc = Path(__file__).resolve().parents[1]/'nxnative/dm/android'
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(inc),str(d/'case.c'),'-o',str(d/'case')],check=True)
            subprocess.run([str(d/'case')],check=True)

if __name__=='__main__': unittest.main()
