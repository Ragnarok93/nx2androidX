"""Exercise the production cross-workload recovery guard."""
import subprocess,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class FGCoordination(unittest.TestCase):
    def test_recovery_requires_stability(self):
        code=r'''
#include <assert.h>
#include "gfx_vk_fg_coordination.h"
int main(void){
    dm_fg_coordination c={0};
    assert(!dm_fg_coordination_recover(&c,0));
    assert(dm_fg_coordination_update(&c,10,100,0));
    assert(!dm_fg_coordination_recover(&c,4000000099ULL));
    assert(dm_fg_coordination_recover(&c,4000000100ULL));
    assert(!dm_fg_coordination_update(&c,10,5000000000ULL,1));
    assert(!dm_fg_coordination_recover(&c,9000000000ULL));
    assert(!dm_fg_coordination_update(&c,10,9000000000ULL,0));
    assert(!dm_fg_coordination_recover(&c,12000000000ULL));
    assert(dm_fg_coordination_recover(&c,13000000000ULL));
    assert(dm_fg_coordination_update(&c,11,14000000000ULL,0));
    assert(!dm_fg_coordination_recover(&c,14000000000ULL));
    assert(dm_fg_coordination_update(&c,11,100,0));
    assert(!dm_fg_coordination_recover(&c,100));
    return 0;
}'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'case.c';p.write_text(code);exe=p.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',
                '-I',str(ROOT/'nxnative/dm/android'),str(p),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
