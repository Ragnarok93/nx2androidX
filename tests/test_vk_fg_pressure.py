"""Exercise conservative live graph admission with the production policy."""
import subprocess,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class FGPressure(unittest.TestCase):
    def test_memory_and_thermal_admission(self):
        source=r'''
#include <assert.h>
#include "gfx_vk_fg_pressure.h"
int main(void){
    uint64_t now=UINT64_C(10000000000);
    dm_fg_pressure s={now,2048*FG_MIB,2048*FG_MIB,0};
    assert(dm_fg_pressure_cap(&s,now,1)==128*FG_MIB);
    dm_fg_pressure_report r=dm_fg_pressure_describe(&s,now+100);
    assert(r.fresh&&r.age_ns==100&&r.down_cap==128*FG_MIB&&r.up_cap==128*FG_MIB&&r.thermal==0);
    r=dm_fg_pressure_describe(&s,now+UINT64_C(5000000001));
    assert(!r.fresh&&!r.down_cap&&!r.up_cap&&r.os_available==2048*FG_MIB);
    r=dm_fg_pressure_describe(&(dm_fg_pressure){0},now);
    assert(!r.fresh&&r.thermal==-1&&r.age_ns==UINT64_MAX);
    s.heap_available=500*FG_MIB;
    assert(dm_fg_pressure_cap(&s,now,0)==50*FG_MIB);
    s.heap_available=0;assert(!dm_fg_pressure_cap(&s,now,0));
    s.heap_available=2048*FG_MIB;s.os_available=800*FG_MIB;
    assert(dm_fg_pressure_cap(&s,now,0)==32*FG_MIB);
    assert(!dm_fg_pressure_cap(&s,now,1));
    s.os_available=768*FG_MIB;assert(!dm_fg_pressure_cap(&s,now,0));
    s.os_available=1024*FG_MIB;assert(!dm_fg_pressure_cap(&s,now,1));
    s.os_available=2048*FG_MIB;s.thermal=-1;
    assert(dm_fg_pressure_cap(&s,now,0)&&!dm_fg_pressure_cap(&s,now,1));
    s.thermal=2;assert(dm_fg_pressure_cap(&s,now,0)&&!dm_fg_pressure_cap(&s,now,1));
    s.thermal=4;assert(!dm_fg_pressure_cap(&s,now,0));
    s.thermal=0;assert(!dm_fg_pressure_cap(&s,now+UINT64_C(5000000001),0));
    assert(!dm_fg_pressure_cap(&s,now-1,0));s.sampled_ns=0;
    assert(!dm_fg_pressure_cap(&s,now,0));
    return 0;
}'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'case.c';p.write_text(source);exe=p.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',
                '-I',str(ROOT/'nxnative/dm/android'),str(p),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
