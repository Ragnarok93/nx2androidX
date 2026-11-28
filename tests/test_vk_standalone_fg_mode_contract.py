"""Keep standalone fixed-multiplier FG independent of Adaptive Flow Scale."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class StandaloneFGModeContract(unittest.TestCase):
    def test_standalone_updates_never_select_adaptive_output_target(self):
        front = (ROOT / 'nxnative/dm/android/front_android.c').read_text()
        # Both direct setting updates and startup apply go through target=0.
        calls = re.findall(r'\bdm_fg_set\(([^;]+)\);', front)
        calls = [c for c in calls if 'atomic_load' in c]
        self.assertEqual(len(calls), 2)
        for call in calls:
            self.assertRegex(call, r'atomic_load\(&g_set_fg_mult\)\s*,\s*0\s*,')
        self.assertIn('settings_load(); fg_apply();', front)
        flow = front.split('static void fg_flow_select_mode(', 1)[1].split('static void fg_flow_step(', 1)[0]
        preset = front.split('static void fg_preset_step(', 1)[1].split('static void fg_fixed_set(', 1)[0]
        for update in (flow, preset):
            self.assertIn('fg_apply();', update)
            self.assertNotIn('g_set_fg_mult', update)
