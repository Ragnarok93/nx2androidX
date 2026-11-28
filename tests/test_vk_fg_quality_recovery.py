"""Exercise the real atomic recovery lease under simultaneous requests."""
from pathlib import Path
import subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
class QualityRecovery(unittest.TestCase):
    def test_recovery_exclusion_and_expiry(self):
        code=r'''
#include <assert.h>
#include <pthread.h>
#include "gfx_vk_quality_recovery.h"
static dm_quality_recovery gate;
static atomic_uint ready,go,wins;
static void *request(void *unused){
    (void)unused;atomic_fetch_add(&ready,1);
    while(!atomic_load(&go)){}
    if(dm_quality_recovery_claim(&gate,100,4000))atomic_fetch_add(&wins,1);
    return 0;
}
int main(void){
    pthread_t a,b;
    assert(!pthread_create(&a,0,request,0)&&!pthread_create(&b,0,request,0));
    while(atomic_load(&ready)!=2){}
    atomic_store(&go,1);pthread_join(a,0);pthread_join(b,0);
    assert(atomic_load(&wins)==1);
    assert(!dm_quality_recovery_claim(&gate,4000000099ULL,4000));
    assert(dm_quality_recovery_claim(&gate,4000000100ULL,12000));
    dm_quality_recovery_hold(&gate,5000000000ULL,4000);
    assert(!dm_quality_recovery_claim(&gate,15999999999ULL,4000));
    dm_quality_recovery_hold(&gate,15000000000ULL,12000);
    assert(!dm_quality_recovery_claim(&gate,26999999999ULL,4000));
    assert(dm_quality_recovery_claim(&gate,27000000000ULL,4000));
    assert(dm_quality_recovery_claim(&gate,UINT64_MAX-1,4000));
    assert(!dm_quality_recovery_claim(&gate,UINT64_MAX,4000));
    return 0;
}'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'case.c';p.write_text(code);exe=p.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-pthread',
                '-I',str(ROOT/'nxnative/dm/android'),str(p),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True,timeout=10)
