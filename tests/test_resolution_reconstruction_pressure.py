"""Only fresh, affirmative critical pressure is a GSR2 suppression signal."""
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class ReconstructionPressure(unittest.TestCase):
    def test_critical_and_stale_observations(self):
        source=r'''
#include <assert.h>
#include "gfx_vk_fg_pressure.h"
int main(void){
    dm_fg_pressure s={.sampled_ns=1,.os_available=1100*FG_MIB,.heap_available=60*FG_MIB,.thermal=3};
    dm_fg_pressure_report r=dm_fg_pressure_describe(&s,1000000);
    assert(r.fresh&&!r.up_cap&&r.down_cap==6*FG_MIB);
    assert(!dm_recon_pressure_critical(&r));
    s.thermal=4;r=dm_fg_pressure_describe(&s,1000000);
    assert(dm_recon_pressure_critical(&r));
    r=dm_fg_pressure_describe(&s,6000000000);
    assert(!r.fresh&&!dm_recon_pressure_critical(&r));
    s.thermal=-1;s.os_available=768*FG_MIB;r=dm_fg_pressure_describe(&s,1000000);
    assert(dm_recon_pressure_critical(&r));
    s.os_available=0;r=dm_fg_pressure_describe(&s,1000000);
    assert(!dm_recon_pressure_critical(&r));
    return 0;
}'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'case.c';p.write_text(source);exe=p.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(ROOT/'nxnative/dm/android'),str(p),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
