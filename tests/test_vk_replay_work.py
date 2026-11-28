"""A source workload may publish only after closure and every timed submit retires."""
import subprocess,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class ReplayWork(unittest.TestCase):
    def test_complete_ticket_samples(self):
        android=ROOT/'nxnative/dm/android'
        modern=(android/'gfx_vk_replay_work.h').exists()
        source=r'''
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#ifdef SOURCE_LEDGER
#include "gfx_vk_replay_work.h"
#endif
int main(void){
#ifndef SOURCE_LEDGER
    fprintf(stderr,"no complete source-ticket replay timing ledger\n");return 1;
#else
    vk_replay_work w=VK_REPLAY_WORK_INIT;
    uint64_t a=vk_replay_work_submit(&w),b=vk_replay_work_submit(&w);
    assert(a==1&&b==1);
    vk_replay_work_complete(&w,a,3000000,1);
    assert(vk_replay_work_ms(&w,1)<0);
    vk_replay_work_close(&w,1,1);
    assert(vk_replay_work_ms(&w,1)<0);
    uint64_t c=vk_replay_work_submit(&w);assert(c==2);
    vk_replay_work_complete(&w,b,4000000,1);
    assert(vk_replay_work_ms(&w,1)==7.0);
    vk_replay_work_complete(&w,c,9000000,0);
    vk_replay_work_close(&w,2,1);assert(vk_replay_work_ms(&w,2)<0);
    vk_replay_work_close(&w,3,1);assert(vk_replay_work_ms(&w,3)<0);
    c=vk_replay_work_submit(&w);assert(c==4);
    vk_replay_work_close(&w,4,0);vk_replay_work_complete(&w,c,1000000,1);
    assert(vk_replay_work_ms(&w,4)<0);
    // A stale completion must not corrupt the slot reused after wrap.
    c=vk_replay_work_submit(&w);assert(c==5);vk_replay_work_close(&w,5,1);
    for(uint64_t i=6;i<=69;i++){uint64_t t=vk_replay_work_submit(&w);assert(t==i);
        vk_replay_work_complete(&w,t,2000000,1);vk_replay_work_close(&w,i,1);}
    vk_replay_work_complete(&w,c,9999999,1);
    assert(vk_replay_work_ms(&w,5)<0&&vk_replay_work_ms(&w,69)==2.0);
    // Inconsistent source boundary fails closed and realigns the next ticket.
    c=vk_replay_work_submit(&w);assert(c==70);
    vk_replay_work_close(&w,71,1);vk_replay_work_complete(&w,c,1000000,1);
    assert(vk_replay_work_ms(&w,70)<0&&vk_replay_work_ms(&w,71)<0);
    c=vk_replay_work_submit(&w);assert(c==72);
    vk_replay_work_complete(&w,c,1000000,1);vk_replay_work_close(&w,72,1);
    assert(vk_replay_work_ms(&w,72)==1.0);
#endif
}
'''
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'case.c';p.write_text(source);exe=p.with_suffix('')
            cmd=['cc','-std=c11','-Wall','-Wextra','-Werror','-pthread','-I',str(android),str(p),'-o',str(exe)]
            if modern:cmd.append('-DSOURCE_LEDGER')
            subprocess.run(cmd,check=True)
            r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=5)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
