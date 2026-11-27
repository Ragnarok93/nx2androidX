"""Host test for production frame-boundary continuity (not GPU image validation)."""
import subprocess
import tempfile
import unittest
from pathlib import Path

class SourceContinuity(unittest.TestCase):
    def test_mode_crop_gap_pause_and_redraw(self):
        code = r'''
#include <assert.h>
#include "gfx_vk_source_continuity.h"
int main(void) {
    dm_recon_continuity s = {0};
    dm_recon_source f = {.ticket=10,.source_ns=1000000000,
        .width=1280,.height=720,.output_w=1920,.output_h=1080,.format=37,
        .crop={0,0,1280,720}};
    unsigned flags;
    assert(dm_recon_source_accept(&s,&f,&flags) && flags == DM_RECON_SOURCE_INITIAL);
    // Same ticket, overlay redraw, stale ticket: no mutation.
    assert(!dm_recon_source_accept(&s,&f,&flags) && !flags);
    f.ticket=0; assert(!dm_recon_source_accept(&s,&f,&flags));
    f.ticket=9; assert(!dm_recon_source_accept(&s,&f,&flags));
    assert(s.previous.ticket==10);
    // Delayed consumption does not affect the source timestamp interval.
    f.ticket=11; f.source_ns+=33333333;
    assert(dm_recon_source_accept(&s,&f,&flags) && !flags);
    // Operation mode can change even if the game keeps the same dimensions.
    ++f.ticket; f.source_ns+=33333333; f.docked=1;
    assert(dm_recon_source_accept(&s,&f,&flags) && flags==DM_RECON_SOURCE_MODE);
    ++f.ticket; f.source_ns+=33333333;
    assert(dm_recon_source_accept(&s,&f,&flags) && !flags);
    ++f.ticket; f.source_ns+=33333333; f.crop[0]=1;
    assert(dm_recon_source_accept(&s,&f,&flags) && flags==DM_RECON_SOURCE_GEOMETRY);
    ++f.ticket; f.source_ns+=33333333; f.flip_y=1;
    assert(dm_recon_source_accept(&s,&f,&flags) && flags==DM_RECON_SOURCE_ENCODING);
    ++f.ticket; f.source_ns+=33333333; f.format=43; f.output_w=2560;
    assert(dm_recon_source_accept(&s,&f,&flags) && flags==(DM_RECON_SOURCE_ENCODING|DM_RECON_SOURCE_GEOMETRY));
    ++f.ticket; f.source_ns+=300000000;
    assert(dm_recon_source_accept(&s,&f,&flags) && flags==DM_RECON_SOURCE_PAUSE);
    f.ticket+=2; f.source_ns+=33333333;
    assert(dm_recon_source_accept(&s,&f,&flags) && flags==DM_RECON_SOURCE_GAP);
    dm_recon_source_invalidate(&s);
    assert(!dm_recon_source_accept(&s,&f,&flags));
    ++f.ticket; f.source_ns+=33333333;
    assert(dm_recon_source_accept(&s,&f,&flags) && flags==(DM_RECON_SOURCE_SURFACE|DM_RECON_SOURCE_INITIAL));
    ++f.ticket; f.source_ns+=33333333;
    assert(dm_recon_source_accept(&s,&f,&flags) && !flags);
    // Nonmonotonic source time cannot produce unsigned underflow or reuse history.
    ++f.ticket; --f.source_ns;
    assert(dm_recon_source_accept(&s,&f,&flags) && flags==DM_RECON_SOURCE_PAUSE);
    return 0;
}'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)
            (p/'case.c').write_text(code)
            inc=Path(__file__).resolve().parents[1]/'nxnative/dm/android'
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(inc),str(p/'case.c'),'-o',str(p/'case')],check=True)
            subprocess.run([str(p/'case')],check=True)

if __name__=='__main__': unittest.main()
