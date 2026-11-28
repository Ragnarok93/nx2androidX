"""Test the production warmup admission policy against overloaded and light cases."""
from pathlib import Path
import subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
class FGWarmup(unittest.TestCase):
    def test_budget(self):
        code=r'''
#include <assert.h>
#include "gfx_vk_fg_warmup.h"
int main(void){
    dm_fg_timing s={.valid=1,.generated=1,.scale=50,.copy_ms=1,.mip_ms=1,
        .shared_ms=2,.refine_ms=4,.output_ms=1,.total_ms=9};
    assert(dm_fg_seed_policy(&s,40,16,1)==FG_SEED_PARALLEL);
    assert(dm_fg_seed_policy(&s,40,9,1)==FG_SEED_INSTEAD_OF_GENERATION);
    assert(dm_fg_seed_policy(&s,40,6,1)==FG_SEED_DEFER);
    assert(dm_fg_seed_policy(&s,60,9,0)==FG_SEED_DEFER);
    assert(dm_fg_seed_policy(&s,60,20,0)==FG_SEED_PARALLEL);
    assert(dm_fg_seed_policy(&s,40,0,1)==FG_SEED_DEFER);
    s.generated=0;assert(dm_fg_seed_policy(&s,40,20,1)==FG_SEED_DEFER);
    return 0;
}'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'case.c';p.write_text(code);exe=p.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',
                '-I',str(ROOT/'nxnative/dm/android'),str(p),'-lm','-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
