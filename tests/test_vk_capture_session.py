"""Exercise the native rolling-capture and export lifecycle."""
import subprocess
import tempfile
import unittest
from pathlib import Path

import host_cc


ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(host_cc.posix(), "POSIX C compiler required")
class CaptureSession(unittest.TestCase):
    def test_stop_export_and_retry_preserve_completed_reports(self):
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

static int g_pd_state, g_npd, g_pd_full, g_pd_bytes;
static int g_input_capture, g_input_count, g_input_pending;
static unsigned retired;
static struct { int dev; } V;
static struct { int b, m; } g_pd[] = {{1, 2}};
static void vkDestroyBuffer(int d, int b, void *a) { assert(!d && b == 1 && !a); retired++; }
static void vkFreeMemory(int d, int m, void *a) { assert(!d && m == 2 && !a); retired++; }
static void input_capture_free(void) { retired++; }
static void surface_capture_abort(void) { retired++; }
static uint64_t mtl_last_fence(void) { return 42; }
'''
        replay = (ROOT / "nxnative/dm/android/gfx_vk_exec.c").read_text(encoding="utf-8")
        source += replay[replay.index("static void pass_dump_retire_stopped("):
                         replay.index("static void pass_dump_poll_locked(")]
        source += r'''
static int complete(const char *root, unsigned slot) {
    char path[1024];
    snprintf(path, sizeof path, "%s/diagnostic-capture-%u/complete", root, slot);
    return access(path, F_OK) == 0;
}

int main(int argc, char **argv) {
    assert(argc == 2 && setenv("DM_ROOT", argv[1], 1) == 0);
    assert(vk_pass_dump_user_begin(120, 1) && g_capture_delay == 121);
    assert(!vk_pass_dump_user_begin(0, 1));
    assert(capture_start_slot() && !complete(argv[1], 0));
    capture_finish_slot();
    assert(complete(argv[1], 0) && vk_pass_dump_user_active());
    for (unsigned i = 1; i < 3; i++) {
        assert(capture_start_slot() && !complete(argv[1], i));
        capture_finish_slot();
        assert(complete(argv[1], i));
    }
    assert(capture_start_slot() && !complete(argv[1], 0));
    assert(complete(argv[1], 1) && complete(argv[1], 2));
    fputs("100\t1\t2\t3\t4\t1\n", g_capture_timeline);
    assert(vk_pass_dump_export_begin());
    assert(!vk_pass_dump_export_begin());
    assert(!vk_pass_dump_user_active() && !g_capture_timeline);
    assert(!vk_pass_dump_user_begin(0, 1));
    g_pd_state = g_npd = g_input_capture = g_input_count = g_pd_full = 1;
    g_pd_bytes = 4096;
    uint64_t fence = 0;
    pass_dump_retire_stopped(41, &fence);
    assert(g_pd_state == 2 && fence == 42 && !retired && g_capture_rolling_dump);
    pass_dump_retire_stopped(42, &fence);
    assert(retired == 4 && !g_pd_state && !g_npd && !g_pd_bytes);
    assert(!g_input_capture && !g_input_count && !g_pd_full);
    assert(!complete(argv[1], 0));
    vk_pass_dump_export_end(); // failed or canceled export
    assert(!vk_pass_dump_user_active());
    assert(vk_pass_dump_export_begin());
    vk_pass_dump_export_end();
    assert(!vk_pass_dump_user_active() && !g_capture_timeline);
    assert(!complete(argv[1], 0) && complete(argv[1], 1) && complete(argv[1], 2));
    assert(vk_pass_dump_user_begin(0, 1));
    assert(!complete(argv[1], 0) && !complete(argv[1], 1) && !complete(argv[1], 2));
    assert(capture_start_slot());
    vk_pass_dump_user_stop();
    assert(!vk_pass_dump_user_active() && !g_capture_timeline);
    assert(!vk_pass_dump_user_begin(0, 1)); // old GPU slot still in flight
    capture_finish_slot();
    assert(!complete(argv[1], 0));

    assert(vk_pass_dump_user_begin(0, 0));
    assert(vk_pass_dump_user_active() == CAPTURE_SCALING && !g_capture_timeline);
    vk_capture_scale_present(1, (vk_capture_scale_present_sample){.requested = SP_FSR1});
    assert(g_scale_capture[1].ticket == 0); // a previous session's queued present cannot join
    for (uint64_t t = 1; t <= SCALE_CAPTURE_FRAMES + 1; t++) {
        capture_scale_replay(t, (vk_capture_scale_replay_sample){
            .mode = 1, .min = 50, .max = 75, .target = 75, .passes = 2,
            .logical_w = 1280, .logical_h = 720, .physical_w = 960, .physical_h = 540,
        });
    }
    uint64_t last = SCALE_CAPTURE_FRAMES + 1;
    assert(g_scale_capture_overwritten == 1);
    vk_capture_scale_present(last, (vk_capture_scale_present_sample){
        .requested = SP_SGSR2, .active = SP_SGSR1, .physical = 1,
        .source_w = 960, .source_h = 540, .output_w = 1280, .output_h = 720,
        .reason = "temporal reconstruction unavailable or over budget",
    });
    assert(g_scale_capture[1].seen_present);
    // A temporary file error retains stopped samples for the next export.
    char unavailable[1024];
    snprintf(unavailable, sizeof unavailable, "%s/no-such-directory", argv[1]);
    assert(setenv("DM_ROOT", unavailable, 1) == 0);
    assert(vk_pass_dump_export_begin());
    assert(g_scale_capture && !vk_pass_dump_user_active());
    capture_scale_replay(last + 1, (vk_capture_scale_replay_sample){0});
    assert(g_scale_capture_ticket == last); // no writes while the archive is reading
    vk_pass_dump_export_end();
    capture_scale_replay(last + 1, (vk_capture_scale_replay_sample){0});
    assert(g_scale_capture_ticket == last); // cancellation does not resume capture
    assert(setenv("DM_ROOT", argv[1], 1) == 0);
    assert(vk_pass_dump_export_begin() && !g_scale_capture);
    char path[1024], line[1024];
    snprintf(path, sizeof path, "%s/scaling_frames.tsv", argv[1]);
    FILE *trace = fopen(path, "r");
    assert(trace && fgets(line, sizeof line, trace) && strstr(line, "overwritten_rows=1"));
    assert(fgets(line, sizeof line, trace) && strstr(line, "requested_upscaler"));
    assert(fgets(line, sizeof line, trace) && !strncmp(line, "2\tFixed", 7));
    while (fgets(line, sizeof line, trace)) {
        if (strstr(line, "GSR 2\tGSR 1\t1\t960\t540\t1280\t720")) break;
    }
    assert(!feof(trace) && !fclose(trace));
    vk_pass_dump_export_end();
    assert(!g_scale_capture && !vk_pass_dump_user_active());
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path / "capture.c").write_text(source, encoding="utf-8")
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pthread",
                            "-I", str(ROOT / "nxnative/dm/android"), str(path / "capture.c"),
                            "-o", str(path / "capture")], check=True)
            subprocess.run([str(path / "capture"), tmp], check=True)
