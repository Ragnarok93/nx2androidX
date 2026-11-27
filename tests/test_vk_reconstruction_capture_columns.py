"""Compile the production capture writer in a strict POSIX test translation unit."""
import csv
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

class ReconstructionCaptureColumns(unittest.TestCase):
    def test_export_has_exact_column_alignment(self):
        source = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static atomic_int g_pd_user_pending;
static unsigned g_pd_scene_waited;
#define VKLOG(...) ((void)0)
#include "gfx_vk_capture_session.inc"
int main(int argc, char **argv) {
    assert(argc==2 && setenv("DM_ROOT",argv[1],1)==0);
    assert(vk_pass_dump_user_begin(0,1));
    assert(capture_start_slot());
    capture_finish_slot();
    capture_scale_replay(10,(vk_capture_scale_replay_sample){.mode=0});
    capture_scale_replay(11,(vk_capture_scale_replay_sample){.mode=0});
    vk_capture_scale_present(10,(vk_capture_scale_present_sample){
        .requested=SP_SGSR2,.active=SP_SGSR2,.source_w=1280,.source_h=720,
        .output_w=2560,.output_h=1440,.timing_ticket=9,.gpu_ms=2.25,
        .display_w=1920,.display_h=1080,.actual_w=2560,.actual_h=1440,
        .output_height=1440,.supersampling=1,.reconstruction_submitted=1,
        .guest_w=1280,.guest_h=720,.crop={8,4,1264,712},
        .final_source_w=2560,.final_source_h=1440,
        .drawable_w=1920,.drawable_h=1080,.swapchain_w=1080,.swapchain_h=1920,
        .timing_mode=SP_SGSR2,.timing_iw=1280,.timing_ih=720,
        .timing_ow=2560,.timing_oh=1440,.timing_budget_ms=2.5,
        .timing_cadence_us=33000,.timing_generation=7,.timing_history_epoch=5,
        .timing_history_reset=1,.sharpness_requested=75,.sharpness_applied=75,
        .stage_ms={0.125,0.5,1.0,0.625},.source_ns=123456789,
        .source_reset=8,.docked=1,.alloc_attempts=3,.allocations=2,
        .alloc_waits=1,.alloc_wait_ns=42,.slot_wait_ns=17,
        .reason="image-driven motion"});
    assert(vk_pass_dump_export_begin());
    vk_pass_dump_export_end();
    return 0;
}'''
        with tempfile.TemporaryDirectory() as d:
            path=Path(d); (path/'case.c').write_text(source)
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-pthread',
                            '-I',str(ROOT/'nxnative/dm/android'),str(path/'case.c'),'-o',str(path/'case')],check=True)
            subprocess.run([str(path/'case'),d],check=True)
            lines=(path/'scaling_frames.tsv').read_text().splitlines()
            self.assertEqual(lines[0],'# overwritten_rows=0')
            self.assertTrue(lines[1].startswith('# runtime_elf_build_id='))
            rows=list(csv.DictReader(lines[2:],delimiter='\t'))
            self.assertEqual(len(rows),2)
            for row in rows:
                self.assertNotIn(None,row)
                self.assertTrue(all(x is not None for x in row.values()))
            a,b=rows
            self.assertEqual(a['ticket'],'10')
            self.assertEqual(a['display_w'],'1920')
            self.assertEqual(a['display_h'],'1080')
            self.assertEqual(a['actual_reconstruction_w'],'2560')
            self.assertEqual(a['actual_reconstruction_h'],'1440')
            self.assertEqual(a['configured_output_short_edge'],'1440')
            self.assertEqual(a['supersampling'],'1')
            self.assertEqual(a['reconstruction_submitted'],'1')
            self.assertEqual(a['guest_w'],'1280')
            self.assertEqual(a['guest_crop_x'],'8')
            self.assertEqual(a['final_source_w'],'2560')
            self.assertEqual(a['drawable_w'],'1920')
            self.assertEqual(a['swapchain_w'],'1080')
            self.assertEqual(b['reconstruction_submitted'],'0')
            self.assertEqual(a['active_upscaler'],'GSR 2')
            self.assertEqual(a['timing_backend'],'GSR 2')
            self.assertEqual(a['timing_generation'],'7')
            self.assertEqual(a['timing_history_epoch'],'5')
            self.assertEqual(a['timing_history_reset'],'1')
            self.assertEqual(a['sharpness_applied'],'75')
            self.assertEqual(a['stage_motion_ms'],'0.500')
            self.assertEqual(a['source_enqueue_ns'],'123456789')
            self.assertEqual(a['source_reset_flags'],'8')
            self.assertEqual(a['docked_at_enqueue'],'1')
            self.assertEqual(a['reconstruction_alloc_attempts'],'3')
            self.assertEqual(a['reconstruction_allocations'],'2')
            self.assertEqual(a['reconstruction_alloc_wait_ns'],'42')
            self.assertEqual(a['reconstruction_slot_wait_ns'],'17')
            self.assertEqual(b['presentation_seen'],'0')
            self.assertEqual(b['timing_backend'],'-')

if __name__=='__main__': unittest.main()
