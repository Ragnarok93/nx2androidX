"""Check the generated image-driven shader's early full-rejection contract."""
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "nxnative/dm/android/tools"))
import sgsr2_shaders


class ImageDrivenEarlyReject(unittest.TestCase):
    def test_generated_history_path(self):
        vendor = ROOT / "nxnative/dm/third_party/sgsr2/include"
        with tempfile.TemporaryDirectory() as tmp:
            with patch.dict(sgsr2_shaders.VARIANTS,
                            {"2fs": ("glsl_2_pass_fs", {"upscale": "fs"})},
                            clear=True):
                paths = sgsr2_shaders.shader_sources(vendor, Path(tmp))
            flow = next(p for p in paths if p.name == "sgsr2_flow_upscale.frag").read_text()
            reject = flow.index("pixelReject = smoothstep(0.08, 0.18, mismatch);")
            self.assertLess(reject, flow.index("float boxweight = exp("))
            self.assertLess(reject, flow.index("vec3 HistoryColor = textureLod(PrevOutput"))
            self.assertIn("rejectHistory = pixelReject >= 1.0;", flow)
            self.assertEqual(flow.count("textureLod(PreviousColor, PrevUV, 0.0)"), 1)
            self.assertIn("alpha = mix(alpha, 1.0, pixelReject);", flow)
            self.assertIn("Upsampledcw.xyz = mix(HistoryColor, Upsampledcw.xyz, alpha);", flow)
            self.assertEqual(flow.count("vec4 live = flowColor(InputPos);"), 1)
            # Keep the vendor-derived native temporal variant unchanged by
            # the image-driven optimization.
            native = next(p for p in paths if p.name == "sgsr2_2fs_upscale.frag").read_text()
            self.assertNotIn("pixelReject", native)
