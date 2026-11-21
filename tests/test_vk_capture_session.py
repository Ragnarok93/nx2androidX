"""Exercise the native rolling-capture and export lifecycle."""
import subprocess
import tempfile
import unittest
from pathlib import Path

import host_cc


ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(host_cc.posix(), "POSIX C compiler required")
class CaptureSession(unittest.TestCase):
    def test_recording_survives_export_failure_and_stops_on_success(self):
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
#include "gfx_vk_capture_session.inc"

static int complete(const char *root, unsigned slot) {
    char path[1024];
    snprintf(path, sizeof path, "%s/diagnostic-capture-%u/complete", root, slot);
    return access(path, F_OK) == 0;
}

int main(int argc, char **argv) {
    assert(argc == 2 && setenv("DM_ROOT", argv[1], 1) == 0);
    assert(vk_pass_dump_user_begin(120) && g_capture_delay == 121);
    assert(!vk_pass_dump_user_begin(0));
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
    assert(vk_pass_dump_user_active());
    vk_pass_dump_export_end(0);
    assert(vk_pass_dump_user_active());
    capture_finish_slot();
    assert(complete(argv[1], 0));
    assert(vk_pass_dump_export_begin());
    vk_pass_dump_export_end(1);
    assert(!vk_pass_dump_user_active() && !g_capture_timeline);
    assert(complete(argv[1], 0) && complete(argv[1], 1) && complete(argv[1], 2));
    assert(vk_pass_dump_user_begin(0));
    assert(!complete(argv[1], 0) && !complete(argv[1], 1) && !complete(argv[1], 2));
    assert(capture_start_slot());
    assert(vk_pass_dump_export_begin());
    vk_pass_dump_export_end(1);
    capture_finish_slot();
    assert(!complete(argv[1], 0));
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path / "capture.c").write_text(source, encoding="utf-8")
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pthread",
                            "-I", str(ROOT / "nxnative/dm/android"), str(path / "capture.c"),
                            "-o", str(path / "capture")], check=True)
            subprocess.run([str(path / "capture"), tmp], check=True)
