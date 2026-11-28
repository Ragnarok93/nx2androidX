"""Exercise source continuity across explicit hold epochs and real stalls."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class LifecycleEpoch(unittest.TestCase):
    def test_short_hold_and_long_active_stall_are_distinct(self):
        android = ROOT / 'nxnative/dm/android'
        modern = 'lifecycle_epoch' in (android / 'gfx_vk_source_continuity.h').read_text()
        code = r'''
#include <assert.h>
#include <stdio.h>
#include "gfx_vk_source_continuity.h"
#ifdef EXPLICIT_EPOCH
#define EPOCH(f,v) ((f).lifecycle_epoch=(v))
#else
#define EPOCH(f,v) ((void)(f),(void)(v))
#endif
int main(void){
    dm_recon_continuity s={0};
    dm_recon_source f={.ticket=1,.source_ns=1000000000,.width=1280,.height=720};
    unsigned flags;
    EPOCH(f,2);
    assert(dm_recon_source_accept(&s,&f,&flags)&&flags==DM_RECON_SOURCE_INITIAL);
    // Menu opened and closed in under 250 ms, no intervening source.
    EPOCH(f,4);++f.ticket;f.source_ns+=20000000;
    if(!dm_recon_source_accept(&s,&f,&flags)||flags!=DM_RECON_SOURCE_PAUSE){
        fprintf(stderr,"short explicit hold missed: flags=%u\n",flags);return 1;
    }
#ifdef EXPLICIT_EPOCH
    // One second of active execution is a stall, not an intentional pause.
    ++f.ticket;f.source_ns+=1000000000;
    assert(dm_recon_source_accept(&s,&f,&flags)&&flags==DM_RECON_SOURCE_STALL);
    // Multiple hold/resume pairs still break chronology exactly once.
    EPOCH(f,8);++f.ticket;f.source_ns+=10000000;
    assert(dm_recon_source_accept(&s,&f,&flags)&&flags==DM_RECON_SOURCE_PAUSE);
    ++f.ticket;f.source_ns+=16666667;
    assert(dm_recon_source_accept(&s,&f,&flags)&&!flags);
    // Held and stale queued epochs do not advance the real-source history.
    EPOCH(f,9);++f.ticket;assert(!dm_recon_source_accept(&s,&f,&flags));
    EPOCH(f,6);assert(!dm_recon_source_accept(&s,&f,&flags));
    assert(dm_lifecycle_source_current(10,10));
    assert(!dm_lifecycle_source_current(8,10));
    assert(!dm_lifecycle_source_current(9,9));
    assert(dm_lifecycle_source_current(0,0));
    assert(dm_lifecycle_next_epoch(2,1)==3);
    assert(dm_lifecycle_next_epoch(3,1)==3);
    assert(dm_lifecycle_next_epoch(3,0)==4);
    assert(dm_lifecycle_next_epoch(4,0)==4);
    // Legacy frontends with no explicit lifecycle still invalidate on a gap.
    s=(dm_recon_continuity){0};EPOCH(f,0);f.ticket=1;
    assert(dm_recon_source_accept(&s,&f,&flags));
    ++f.ticket;f.source_ns+=300000000;
    assert(dm_recon_source_accept(&s,&f,&flags)&&flags==DM_RECON_SOURCE_PAUSE);
#endif
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as d:
            c=Path(d)/'epoch.c';c.write_text(code);exe=c.with_suffix('')
            cmd=['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(android),str(c),'-o',str(exe)]
            if modern:cmd.append('-DEXPLICIT_EPOCH')
            subprocess.run(cmd,check=True)
            run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=5)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
