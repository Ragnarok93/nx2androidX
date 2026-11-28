"""Verify exact selected-heap admission for a cold upward Flow Scale replacement."""
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

class FGHeapAdmission(unittest.TestCase):
    def test_cold_delta_fits_when_second_graph_does_not(self):
        android=ROOT/'nxnative/dm/android'
        modern='dm_fg_heap_admit(' in (android/'gfx_vk_fg_pressure.h').read_text()
        code=r'''
#include <assert.h>
#include <stdio.h>
#include "gfx_vk_fg_pressure.h"
int main(void){
    uint64_t now=10000000000ull;
    dm_fg_heap_snapshot s={.sampled_ns=now,.os_available=2048*FG_MIB,.thermal=0,
        .count=2,.eligible=3,.budget={1024*FG_MIB,64*FG_MIB},.usage={964*FG_MIB,63*FG_MIB}};
    uint64_t req[DM_FG_MAX_HEAPS]={80*FG_MIB},old[DM_FG_MAX_HEAPS]={76*FG_MIB};
#ifndef EXACT_HEAPS
    (void)s;(void)req;(void)old;
    fprintf(stderr,"no selected-heap cold replacement admission\n");return 1;
#else
    dm_fg_heap_admission p=dm_fg_heap_admit(&s,now,req,NULL,0,1);
    assert(p.reason==DM_FG_HEAP_CAP);
    p=dm_fg_heap_admit(&s,now,req,old,now-1,1);
    assert(p.reason==DM_FG_HEAP_OK&&p.total==80*FG_MIB&&p.growth==4*FG_MIB);
    // Reclaim credit on heap zero cannot pay for a new allocation on heap one.
    req[1]=FG_MIB;p=dm_fg_heap_admit(&s,now,req,old,now-1,1);
    assert(p.reason==DM_FG_HEAP_CAP);
    req[1]=0;
    assert(dm_fg_heap_admit(&s,now,req,old,now+1,1).reason==DM_FG_HEAP_RECLAIM);
    assert(dm_fg_heap_admit(&s,now+5000000001ull,req,old,now-1,1).reason==DM_FG_HEAP_STALE);
    s.thermal=2;assert(dm_fg_heap_admit(&s,now,req,old,now-1,1).reason==DM_FG_HEAP_PRESSURE);
    s.thermal=0;s.os_available=1027*FG_MIB;
    assert(dm_fg_heap_admit(&s,now,req,old,now-1,1).reason==DM_FG_HEAP_CAP);
    s.os_available=2048*FG_MIB;s.usage[0]=75*FG_MIB;
    assert(dm_fg_heap_admit(&s,now,req,old,now-1,1).reason==DM_FG_HEAP_RECLAIM);
    s.usage[0]=964*FG_MIB;s.eligible=2;
    assert(dm_fg_heap_admit(&s,now,req,old,now-1,1).reason==DM_FG_HEAP_UNKNOWN);
    s.eligible=3;s.budget[0]=0;
    assert(dm_fg_heap_admit(&s,now,req,old,now-1,1).reason==DM_FG_HEAP_UNKNOWN);
    s.budget[0]=1024*FG_MIB;
    req[0]=76*FG_MIB;assert(dm_fg_heap_admit(&s,now,req,old,now-1,1).reason==DM_FG_HEAP_OK);
    req[0]=83*FG_MIB;assert(dm_fg_heap_admit(&s,now,req,old,now-1,1).reason==DM_FG_HEAP_CAP);
    req[0]=80*FG_MIB;
    assert(!dm_fg_heap_reserved_fits(&s,now,req,1));
    s.usage[0]-=76*FG_MIB;
    assert(dm_fg_heap_reserved_fits(&s,now,req,1));
    s.usage[0]+=60*FG_MIB;
    assert(!dm_fg_heap_reserved_fits(&s,now,req,1));
    s.usage[0]-=60*FG_MIB;s.os_available=1090*FG_MIB;
    assert(!dm_fg_heap_reserved_fits(&s,now,req,1));
#endif
}
'''
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'admission.c';p.write_text(code);exe=p.with_suffix('')
            cmd=['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(android),str(p),'-o',str(exe)]
            if modern:cmd.append('-DEXACT_HEAPS')
            subprocess.run(cmd,check=True)
            r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=5)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
