"""The shipped tree's content, including late-staged hooks, controls environment refresh."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('phone_assets', Path(__file__).resolve().parents[1] / 'packaging/android-apk/build_assets.py')
assets = importlib.util.module_from_spec(spec)
spec.loader.exec_module(assets)


class PayloadStampTest(unittest.TestCase):
    def test_same_size_and_late_runtime_components_change_stamp(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'tool/porter/revision.py'
            source.parent.mkdir(parents=True)
            source.write_bytes(b'old')
            assets.finalize_metadata(root)
            before = (root / 'build-id').read_text()
            source.write_bytes(b'new')
            assets.finalize_metadata(root)
            after = (root / 'build-id').read_text()
            self.assertNotEqual(before, after)
            hook = root / 'tool/tools/adrenotools/libhook_impl.so'
            hook.parent.mkdir(parents=True)
            hook.write_bytes(b'hook')
            assets.finalize_metadata(root)
            self.assertNotEqual(after, (root / 'build-id').read_text())
            self.assertIn('libhook_impl.so', (root / 'modes.json').read_text())

    def test_runtime_commit_stamp_requires_clean_sources(self):
        commit = '0123456789abcdef0123456789abcdef01234567'
        with tempfile.TemporaryDirectory() as directory:
            tool = Path(directory)
            stamp = tool / 'nxnative/dm/android/runtime-source-commit.txt'
            stamp.parent.mkdir(parents=True)
            with patch.object(assets.subprocess, 'check_output', side_effect=[commit, '']):
                assets.stamp_runtime_source(tool)
            self.assertEqual(stamp.read_text(), commit + '\n')
            with patch.object(assets.subprocess, 'check_output',
                              side_effect=[commit, ' M nxnative/dm/android/gfx_vk_exec.c']):
                assets.stamp_runtime_source(tool)
            self.assertEqual(stamp.read_text(), 'unavailable\n')
