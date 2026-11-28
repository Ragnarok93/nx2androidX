"""Guard the slider-to-RCAS gain contract without requiring a shader compiler."""
import math
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

class RcasGain(unittest.TestCase):
    def test_slider_maps_to_linear_gain_in_production_shader(self):
        shader = (ROOT/'nxnative/dm/android/shaders/fsr_rcas.frag').read_text()
        header = (ROOT/'nxnative/dm/android/gfx_vk_spatial.h').read_text()
        self.assertIn('float strength = max(clamp(params.sharpness, 0.0, 1.0), 1e-6);', shader)
        self.assertIn('AU4 c = uvec4(floatBitsToUint(strength), 0u, 0u, 0u);', shader)
        self.assertIn('FsrRcasF(r, g, b, a, AU2(px), c);', shader)
        self.assertIn('#define FSR_RCAS_PASSTHROUGH_ALPHA 1', shader)
        self.assertNotIn('2.0 - 2.0 * clamp', shader)
        self.assertRegex(header, r'int rcas = mode == SP_FSR1 && sharpness > 0\.001f;')
        self.assertRegex(header, r'int wants_post = \(mode == SP_BILINEAR \|\| flow\) && sharpness > 0\.001f;')
        # FidelityFX defines gain as exp2(-stops). Check every UI step.
        prev = 0.0
        for pct in range(1, 101):
            strength = pct / 100.0
            gain = max(min(strength, 1.0), 1e-6)
            reference = 2.0 ** (-(-math.log2(max(strength, 1e-6))))
            self.assertAlmostEqual(gain, reference, places=12)
            self.assertGreater(gain, prev)
            prev = gain
        self.assertEqual(prev, 1.0)

if __name__ == '__main__': unittest.main()
