"""Exercise production timestamp decoding, including wrap and stale samples."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]

class FGTiming(unittest.TestCase):
    def test_completed_batch(self):
        code=r'''
#include <assert.h>
#include <math.h>
#include "gfx_vk_fg_timing.h"
int main(void) {
    dm_fg_timing s={.serial=1,.epoch=2,.scale=50,.width=1920,.height=1080,.generated=2};
    uint64_t ticks[]={100,110,120,140,160,170,200,210};
    assert(dm_fg_timing_decode(&s,ticks,8,64,1000000.0));
    assert(s.copy_ms==10 && s.mip_ms==10 && s.shared_ms==20);
    assert(s.refine_ms==50 && s.output_ms==20 && s.total_ms==110);
    assert(dm_fg_timing_fresh(&s,2,0));
    assert(!dm_fg_timing_fresh(&s,1,0));
    assert(!dm_fg_timing_fresh(&s,2,1));
    s.generated=0;
    uint64_t wrap[]={UINT32_MAX-3,2,4,9};
    assert(dm_fg_timing_decode(&s,wrap,4,32,1000000.0));
    assert(s.copy_ms==6 && s.total_ms==13 && !s.refine_ms && !s.output_ms);
    assert(!dm_fg_timing_fresh(&s,2,0)); // history only is not generated evidence
    uint64_t wrap64[]={UINT64_MAX-3,2,4,9};
    assert(dm_fg_timing_decode(&s,wrap64,4,64,1000000.0));
    assert(s.total_ms==13);
    assert(!dm_fg_timing_decode(&s,wrap,3,32,1.0) && !s.valid);
    assert(!dm_fg_timing_decode(&s,wrap,4,0,1.0));
    assert(!dm_fg_timing_decode(&s,wrap,4,65,1.0));
    assert(!dm_fg_timing_decode(&s,wrap,4,32,NAN));
    s.generated=4;assert(!dm_fg_timing_decode(&s,wrap,4,32,1.0));
    return 0;
}'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'check.c';p.write_text(code);exe=p.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(ROOT/'nxnative/dm/android'),str(p),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
