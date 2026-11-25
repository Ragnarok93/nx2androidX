"""Live 30/40/60 and adaptive *internal* resolution require actual game hooks."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HOST = ROOT / "nxnative/dm/host"
GFX = ROOT / "nxnative/dm/gfx/nvn"
ANDROID = ROOT / "nxnative/dm/android"

class LiveGraphicsPolicyTests(unittest.TestCase):
    def test_dynres_hysteresis_compiles_and_scales_only_after_sustained_overload(self):
        cc = shutil.which('cc') or shutil.which('clang')
        if not cc:
            self.skipTest('C compiler required')
        prog = r"""
#include <assert.h>
#include "nvn_dynres_policy.h"
int main(void) {
    nx_dynres_policy p; nx_dynres_reset(&p, 70);
    for (int k = 0; k < 30; k++) assert(!nx_dynres_sample(&p, 20000, 14000));
    assert(p.scale == 100);
    for (int k = 0; k < 30; k++) nx_dynres_sample(&p, 20000, 14000);
    assert(p.scale == 95);
    for (int j = 0; j < 20; j++)
        for (int k = 0; k < 30; k++) nx_dynres_sample(&p, 20000, 14000);
    assert(p.scale == 70);  // the user's floor, not a return to 30 fps
    for (int j = 0; j < 3; j++)
        for (int k = 0; k < 30; k++) nx_dynres_sample(&p, 4000, 14000);
    assert(p.scale == 70);
    for (int k = 0; k < 30; k++) nx_dynres_sample(&p, 4000, 14000);
    assert(p.scale == 75);
    for (int k = 0; k < 100; k++) nx_dynres_sample(&p, -1, 14000);
    assert(p.scale == 75);
    nx_dynres_reset(&p, 95);
    assert(p.scale == 100 && p.floor == 95);
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as d:
            s = Path(d)/'policy.c'; out = Path(d)/'policy'
            s.write_text(prog)
            build = subprocess.run([cc, '-std=c11','-Wall','-Wextra','-Werror','-I',str(GFX),str(s),'-o',str(out)], capture_output=True, text=True)
            self.assertEqual(build.returncode,0,build.stderr)
            run = subprocess.run([str(out)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)

    def test_legacy_scaling_uses_verified_hooks_and_menu_uses_renderer(self):
        loader=(HOST/'loader.c').read_text()
        nvn=(GFX/'hle_nvn.c').read_text()
        fps=(HOST/'hle_fpslock.c').read_text()
        front=(ANDROID/'front_android.c').read_text()
        self.assertIn('dm_fps_state && g_render_scale_stubs',loader)
        self.assertIn('dm_ue_screenpct_supported()',loader)
        self.assertIn('dm_render_scale_supported()',nvn)
        self.assertIn('if (!dm_render_scale_supported())',nvn)
        self.assertIn('DM_UE_SCREENPCT_OFF',fps)
        self.assertIn('b7f965b9d7227db8e29f04e84ea7331b',fps)
        self.assertIn('int dm_menu_drs_supported(void) { return dm_menu_rs_live(); }',front)
        self.assertIn('if (dm_menu_rs_live()) menu_rs_mode_step(1);',front)
        self.assertIn('dyn_min=%d',front)

    def test_fps_switch_writes_engine_and_presentation_policy(self):
        fps=(HOST/'hle_fpslock.c').read_text()
        nvn=(GFX/'hle_nvn.c').read_text()
        perf=(ANDROID/'perf_android.c').read_text()
        self.assertIn('dm_graphics_target_fps()',fps)
        self.assertIn('target == 30 ? 2u : 1u',fps)
        self.assertIn('if (fixed == 30 || fixed == 40 || fixed == 60)',nvn)
        self.assertIn('if (fixed == 30 || fixed == 40 || fixed == 60)',perf)
        self.assertIn('adaptive downshift bypassed',perf)

    def test_controller_and_touch_menu_match(self):
        front=(ANDROID/'front_android.c').read_text()
        layout=(ANDROID/'overlay_layout.h').read_text()
        present=(ANDROID/'gfx_vk_present.c').read_text()
        self.assertIn('OVM_DRS, OVM_DRS_MIN, OVM_RS_MAX, OVM_DRS_NOW, OVM_DRS_HOOK',layout)
        self.assertIn('ov_menu_dyn_min_value',layout)
        self.assertIn('OVM_DRS_MIN) menu_drs_min(',front)
        self.assertIn('OVM_RATE) { menu_rate_step(',front)
        self.assertIn('int dm_menu_drs_actual(void)',front)
        self.assertIn('dm_menu_drs_supported()',present)
        self.assertIn('dm_menu_rate_supported()',present)

if __name__ == '__main__':
    unittest.main()
