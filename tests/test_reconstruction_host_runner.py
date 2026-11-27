import io
import tempfile
import unittest
from pathlib import Path
from run_reconstruction_host import run

class RequiredHostRunner(unittest.TestCase):
    def test_plain_unittest_module_runs_without_main(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            (p/'recon_runner_fixture_ok.py').write_text('import unittest\nclass T(unittest.TestCase):\n def test_one(self): self.assertEqual(1,1)\n')
            out = io.StringIO()
            self.assertTrue(run(['recon_runner_fixture_ok'], p, out))
            self.assertIn('Ran 1 test', out.getvalue())

    def test_missing_empty_and_skipped_are_not_success(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            with self.assertRaisesRegex(RuntimeError, 'Missing required'):
                run(['recon_runner_fixture_missing'], p, io.StringIO())
            (p/'recon_runner_fixture_empty.py').write_text('# no tests\n')
            with self.assertRaisesRegex(RuntimeError, 'No tests collected'):
                run(['recon_runner_fixture_empty'], p, io.StringIO())
            (p/'recon_runner_fixture_skip.py').write_text('import unittest\nclass T(unittest.TestCase):\n @unittest.skip("required capability missing")\n def test_one(self): pass\n')
            self.assertFalse(run(['recon_runner_fixture_skip'], p, io.StringIO()))

if __name__ == '__main__': unittest.main()
