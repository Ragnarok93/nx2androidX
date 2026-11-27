"""Exercise the repository's production temporal history policy (host only)."""
import subprocess
import tempfile
import unittest
from pathlib import Path

class TemporalDependencies(unittest.TestCase):
    def test_commit_reset_and_stale_history(self):
        source = r'''
#include <assert.h>
#include "gfx_vk_temporal_policy.h"
int main(void) {
    dm_temporal_history h = {0};
    dm_temporal_frame f = {.ticket=100,.stream=1,.epoch=1,
        .iw=1280,.ih=720,.ow=2560,.oh=1440,.inputs=DM_TEMPORAL_IMAGE_PAIR,
        .variants=DM_TEMPORAL_2FS,.real=1,.memory_ok=1};
    dm_temporal_plan p = dm_temporal_choose(&h,&f,DM_TEMPORAL_2FS);
    assert(p.variant == DM_TEMPORAL_2FS && p.reset && !h.valid);
    dm_temporal_commit(&h,&f,p,1);
    assert(h.valid && h.ticket==100);
    for (uint64_t t=101;t<601;++t) {
        f.ticket=t;
        p=dm_temporal_choose(&h,&f,DM_TEMPORAL_2FS);
        assert(p.variant==DM_TEMPORAL_2FS && !p.reset && h.ticket==t-1);
        dm_temporal_commit(&h,&f,p,1);
        assert(h.ticket==t);
    }
    // A redraw/duplicate cannot publish another history update.
    f.real=0;
    p=dm_temporal_choose(&h,&f,DM_TEMPORAL_2FS);
    assert(!p.variant);
    dm_temporal_commit(&h,&f,p,0);
    assert(h.valid && h.ticket==600);
    f.real=1;
    p=dm_temporal_choose(&h,&f,DM_TEMPORAL_2FS);
    assert(!p.variant);
    dm_temporal_commit(&h,&f,p,0);
    assert(h.valid && h.ticket==600);
    // A failed real-frame submission cannot publish its history.
    ++f.ticket;
    p=dm_temporal_choose(&h,&f,DM_TEMPORAL_2FS);
    assert(p.variant && !p.reset);
    dm_temporal_commit(&h,&f,p,0);
    assert(!h.valid);
    ++f.ticket; ++f.epoch;
    p=dm_temporal_choose(&h,&f,DM_TEMPORAL_2FS);
    assert(p.variant && p.reset);
    dm_temporal_commit(&h,&f,p,1);
    // A genuine source gap invalidates prior motion/history dependencies.
    f.ticket += 2;
    p=dm_temporal_choose(&h,&f,DM_TEMPORAL_2FS);
    assert(p.variant && p.reset);
    dm_temporal_commit(&h,&f,p,1);
    ++f.ticket; ++f.epoch;
    assert(dm_temporal_choose(&h,&f,DM_TEMPORAL_2FS).reset);
    --f.epoch;
    f.cut=1;
    assert(dm_temporal_choose(&h,&f,DM_TEMPORAL_2FS).reset);
    f.cut=0;
    f.ow=3840; f.oh=2160;
    assert(dm_temporal_choose(&h,&f,DM_TEMPORAL_2FS).reset);
    f.inputs=0;
    assert(!dm_temporal_choose(&h,&f,DM_TEMPORAL_2FS).variant);
    return 0;
}
'''
        root=Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as d:
            p=Path(d); (p/'test.c').write_text(source)
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(root/'nxnative/dm/android'),str(p/'test.c'),'-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)
if __name__=='__main__': unittest.main()
