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
#define DM_RUNTIME_SOURCE_COMMIT "0123456789abcdef0123456789abcdef01234567"
#define DM_RELEASE 1
#define NX_LSFG 1
#define VK_CAPTURE_DRIVER_ID 3
#define VK_CAPTURE_DRIVER_VERSION 0x1234u
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
        .scene_image=0xab,.guest_image=0xcd,.scene_crop={1,2,1200,700},
        .allocated_w=2560,.allocated_h=1440,.reconstruction_recorded=1,.completed_ticket=9,
        .final_source_w=2560,.final_source_h=1440,
        .drawable_w=1920,.drawable_h=1080,.swapchain_w=1080,.swapchain_h=1920,
        .timing_mode=SP_SGSR2,.timing_iw=1280,.timing_ih=720,
        .timing_ow=2560,.timing_oh=1440,.timing_budget_ms=2.5,
        .timing_cadence_us=33000,.timing_generation=7,.timing_history_epoch=5,
        .timing_history_reset=1,.sharpness_requested=75,.sharpness_applied=75,
        .stage_ms={0.125,0.5,1.0,0.625},.source_ns=123456789,
        .source_reset=8,.docked=1,.alloc_attempts=3,.allocations=2,
        .alloc_waits=1,.alloc_wait_ns=42,.slot_wait_ns=17,
        .fg_reason="refresh cap",.fg_period_ms=16.667,.fg_measured_ms=18.5,
        .fg_refresh_ns=8333333,.fg_planned=1,.fg_warm_streak=2,
        .fg_requested_flow=0,.fg_active_flow=50,.fg_submission_serial=13,
        .fg_graph_phase=2,.fg_pending_flow=40,.fg_seed_policy=2,.fg_seed_count=1,
        .fg_active_bytes=123456,.fg_extra_bytes=65432,.fg_build_limit=0,
        .fg_governor={.status="build requested",.workload=123,.matched_serial=10,.allocation_cap=456,.budget_ms=5.25,
            .requested_pct=40,.reason=1,.live=1,.recovery_allowed=0,.matched=1},
        .fg_timing={.valid=1,.serial=10,.epoch=2,.scale=50,.width=1920,.height=1080,.generated=1,
            .copy_ms=.25,.mip_ms=.5,.shared_ms=1,.refine_ms=2,.output_ms=.75,.total_ms=4.5},
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
            self.assertIn('# overwritten_rows=0', lines)
            self.assertTrue(any(line.startswith('# runtime_elf_build_id=') for line in lines))
            self.assertIn('# runtime_git_commit=0123456789abcdef0123456789abcdef01234567', lines)
            self.assertIn('# runtime_build_configuration=release', lines)
            self.assertIn('# runtime_lsfg_compiled=1', lines)
            self.assertIn('# vulkan_driver_id=3', lines)
            self.assertIn('# vulkan_driver_version=0x1234', lines)
            rows=list(csv.DictReader((line for line in lines if not line.startswith('#')),delimiter='\t'))
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
            self.assertEqual(a['scene_image'],'ab')
            self.assertEqual(a['guest_image'],'cd')
            self.assertEqual(a['scene_crop_h'],'700')
            self.assertEqual(a['reconstruction_allocated_w'],'2560')
            self.assertEqual(a['reconstruction_recorded'],'1')
            self.assertEqual(a['reconstruction_completed_ticket'],'9')
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
            self.assertEqual(a['fg_reason'],'refresh cap')
            self.assertEqual(a['fg_period_ms'],'16.667')
            self.assertEqual(a['fg_measured_ms'],'18.500')
            self.assertEqual(a['fg_refresh_ns'],'8333333')
            self.assertEqual(a['fg_planned'],'1')
            self.assertEqual(a['fg_warm_streak'],'2')
            self.assertEqual(a['fg_requested_flow'],'0')
            self.assertEqual(a['fg_active_flow'],'50')
            self.assertEqual(a['fg_submission_serial'],'13')
            self.assertEqual(a['fg_timing_serial'],'10')
            self.assertEqual(a['fg_timing_epoch'],'2')
            self.assertEqual(a['fg_timing_scale'],'50')
            self.assertEqual(a['fg_timing_w'],'1920')
            self.assertEqual(a['fg_timing_h'],'1080')
            self.assertEqual(a['fg_timing_generated'],'1')
            self.assertEqual(a['fg_copy_ms'],'0.250')
            self.assertEqual(a['fg_mip_ms'],'0.500')
            self.assertEqual(a['fg_shared_ms'],'1.000')
            self.assertEqual(a['fg_refine_ms'],'2.000')
            self.assertEqual(a['fg_output_ms'],'0.750')
            self.assertEqual(a['fg_total_ms'],'4.500')
            self.assertEqual(a['fg_graph_phase'],'2')
            self.assertEqual(a['fg_pending_flow'],'40')
            self.assertEqual(a['fg_seed_policy'],'2')
            self.assertEqual(a['fg_seed_count'],'1')
            self.assertEqual(a['fg_active_bytes'],'123456')
            self.assertEqual(a['fg_extra_bytes'],'65432')
            self.assertEqual(a['fg_build_limit'],'0')
            self.assertEqual(a['fg_governor_status'],'build requested')
            self.assertEqual(a['fg_governor_workload'],'123')
            self.assertEqual(a['fg_governor_matched_serial'],'10')
            self.assertEqual(a['fg_governor_cap'],'456')
            self.assertEqual(a['fg_governor_budget_ms'],'5.250')
            self.assertEqual(a['fg_governor_requested_pct'],'40')
            self.assertEqual(a['fg_governor_reason'],'1')
            self.assertEqual(a['fg_governor_live'],'1')
            self.assertEqual(a['fg_governor_recovery_allowed'],'0')
            self.assertEqual(a['fg_governor_matched'],'1')
            self.assertEqual(b['fg_governor_status'],'-')
            self.assertEqual(b['fg_timing_serial'],'0')
            self.assertEqual(b['fg_graph_phase'],'0')
            self.assertEqual(b['fg_reason'],'-')
            self.assertEqual(b['presentation_seen'],'0')
            self.assertEqual(b['timing_backend'],'-')

if __name__=='__main__': unittest.main()
