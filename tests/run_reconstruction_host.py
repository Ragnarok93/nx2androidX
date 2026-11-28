#!/usr/bin/env python3
"""Run required reconstruction host tests; absent or skipped tests fail CI."""
import sys
import unittest
from pathlib import Path

REQUIRED = (
    'test_overlay_recorded_scale', 'test_overlay_settings_scroll', 'test_overlay_scroll_touch', 'test_vk_output_target', 'test_vk_output_present', 'test_vk_output_menu', 'test_overlay_layout', 'test_vk_high_resolution', 'test_vk_lsfg_large_allocation', 'test_vk_flow_budget', 'test_vk_flow_cadence', 'test_vk_reconstruction_limits',
    'test_overlay_recorded_scale', 'test_overlay_settings_scroll', 'test_overlay_scroll_touch', 'test_vk_output_target', 'test_vk_output_present', 'test_vk_output_menu', 'test_overlay_layout', 'test_vk_high_resolution', 'test_vk_lsfg_large_allocation', 'test_vk_pool_recovery', 'test_vk_flow_cadence', 'test_vk_reconstruction_limits',
    'test_vk_reconstruction_retry', 'test_vk_reconstruction_allocation', 'test_vk_guest_temporal_replay',
    'test_vk_reconstruction_capture_columns', 'test_vk_temporal_dependencies',
    'test_vk_temporal_policy', 'test_vk_runtime_identity', 'test_vk_source_continuity',
    'test_trace_reconstruction', 'test_vk_capture_session', 'test_vk_surface_capture',
    'test_vk_present_timing', 'test_vk_reconstruction_timing', 'test_vk_rcas_gain',
    'test_reconstruction_host_runner', 'test_vk_presentation_marker',
)


def run(names, directory, stream=None):
    directory = Path(directory).resolve()
    sys.path.insert(0, str(directory))
    try:
        suite = unittest.TestSuite()
        for name in names:
            if not (directory / (name + '.py')).is_file():
                raise RuntimeError(f'Missing required test module: {name}')
            tests = unittest.defaultTestLoader.loadTestsFromName(name)
            if not tests.countTestCases():
                raise RuntimeError(f'No tests collected from required module: {name}')
            suite.addTests(tests)
        result = unittest.TextTestRunner(stream=stream, verbosity=2).run(suite)
        return result.wasSuccessful() and not result.skipped and result.testsRun > 0
    finally:
        sys.path.remove(str(directory))


if __name__ == '__main__':
    raise SystemExit(0 if run(REQUIRED, Path(__file__).parent) else 1)
