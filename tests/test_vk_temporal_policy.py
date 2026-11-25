"""Host test of temporal eligibility and history ownership (no Vulkan device)."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ANDROID = Path(__file__).resolve().parents[1] / "nxnative/dm/android"

@unittest.skipUnless(shutil.which("cc"), "host C compiler required")
class TemporalPolicyTests(unittest.TestCase):
    def test_real_frames_only_and_safe_fallback(self):
        source = r'''
#include <assert.h>
#include "gfx_vk_temporal_policy.h"
int main(void) {
    dm_temporal_history h = {0};
    dm_temporal_frame f = { .ticket=1, .stream=7, .epoch=1,
        .iw=1280, .ih=720, .ow=1920, .oh=1080,
        .inputs=DM_TEMPORAL_REQUIRED | DM_TEMPORAL_OPAQUE,
        .variants=DM_TEMPORAL_2FS | DM_TEMPORAL_2CS | DM_TEMPORAL_3CS,
        .real=1, .memory_ok=1 };
    dm_temporal_plan p = dm_temporal_choose(&h, &f, DM_TEMPORAL_3CS);
    assert(p.variant == DM_TEMPORAL_3CS && p.reset);
    dm_temporal_commit(&h, &f, p, 1);
    f.ticket++;
    p = dm_temporal_choose(&h, &f, DM_TEMPORAL_3CS);
    assert(p.variant == DM_TEMPORAL_3CS && !p.reset);
    f.real = 0;
    p = dm_temporal_choose(&h, &f, DM_TEMPORAL_3CS);
    assert(!p.variant);
    dm_temporal_commit(&h, &f, p, 0);
    assert(h.valid && h.ticket == 1);
    f.real = 1;
    f.inputs &= ~DM_TEMPORAL_OPAQUE;
    p = dm_temporal_choose(&h, &f, DM_TEMPORAL_3CS);
    assert(p.variant == DM_TEMPORAL_2FS && p.reset);
    dm_temporal_commit(&h, &f, p, 0);
    assert(!h.valid);
    f.inputs &= ~DM_TEMPORAL_VELOCITY;
    assert(!dm_temporal_choose(&h, &f, DM_TEMPORAL_2CS).variant);
    f.inputs |= DM_TEMPORAL_VELOCITY;
    f.memory_ok = 0;
    assert(!dm_temporal_choose(&h, &f, DM_TEMPORAL_2CS).variant);
    f.memory_ok = 1;
    p = dm_temporal_choose(&h, &f, DM_TEMPORAL_2CS);
    assert(p.variant == DM_TEMPORAL_2CS && p.reset);
    dm_temporal_commit(&h, &f, p, 1);
    assert(!dm_temporal_choose(&h, &f, DM_TEMPORAL_2CS).variant);
    f.ticket++;
    f.iw = 1200;
    assert(dm_temporal_choose(&h, &f, DM_TEMPORAL_2CS).reset);
    f.iw = 1280;
    f.epoch++;
    assert(dm_temporal_choose(&h, &f, DM_TEMPORAL_2CS).reset);
    f.epoch--;
    f.cut = 1;
    assert(dm_temporal_choose(&h, &f, DM_TEMPORAL_2CS).reset);
    f.cut = 0;
    f.ticket += 2;
    assert(dm_temporal_choose(&h, &f, DM_TEMPORAL_2CS).reset);
    f.ow = 0;
    assert(!dm_temporal_choose(&h, &f, DM_TEMPORAL_2CS).variant);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path / "case.c").write_text(source)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ANDROID), str(path / "case.c"), "-o", str(path / "case")], check=True)
            subprocess.run([str(path / "case")], check=True, timeout=10)
