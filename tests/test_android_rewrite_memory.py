"""Regression checks for Android's native rewriter memory budget."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'nxnative/scripts/build_ipa_from_nsp.py'
spec = importlib.util.spec_from_file_location('nsp_rewrite_budget', SCRIPT)
nsp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(nsp)


class RewriteWorkerBudgetTests(unittest.TestCase):
    def test_android_eight_gib_budget_serializes_rewrite(self):
        self.assertEqual(nsp.rewrite_workers({'DM_REWRITE_JOBS': '1'}, cpu_count=10), 1)

    def test_larger_android_devices_keep_the_requested_ceiling(self):
        self.assertEqual(nsp.rewrite_workers({'DM_REWRITE_JOBS': '2'}, cpu_count=10), 2)

    def test_desktop_rewrite_keeps_original_parallelism(self):
        self.assertEqual(nsp.rewrite_workers({}, cpu_count=10), 10)

    def test_bad_budget_fails_explicitly(self):
        for v in ('0', '-1', 'nope'):
            with self.subTest(value=v), self.assertRaisesRegex(nsp.BuildError, 'positive integer'):
                nsp.rewrite_workers({'DM_REWRITE_JOBS': v}, cpu_count=10)

    def test_pipeline_passes_android_rewrite_budget(self):
        source = (ROOT / 'porter/pipeline.py').read_text(encoding='utf-8')
        self.assertIn("self.env['DM_REWRITE_JOBS'] = str(self.o.jobs)", source)
        self.assertIn("self.o.jobs = resources.safe_jobs(self.o.jobs)", source)
        driver = SCRIPT.read_text(encoding='utf-8')
        self.assertIn('list(names), rewrite_workers(env)', driver)

    def test_android_uses_smaller_disassembly_pieces(self):
        rewriter = (ROOT / 'nxnative/dm/tools/patch_nso.py').read_text(encoding='utf-8')
        self.assertIn("0x10000, 0x10000, 0x2000) if TARGET == 'android'", rewriter)
        self.assertIn('del insl, ws, inwin, heads, rmid', rewriter)


if __name__ == '__main__':
    unittest.main()
