"""Regression for FFR's 384/64 MiB idle pool cache oscillation."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class PoolRecoveryHysteresis(unittest.TestCase):
    def test_capture_boundary_and_safe_recovery(self):
        source = r'''
#include <assert.h>
#include "gfx_vk_pool_pressure.h"
#define KB(n) ((uint64_t)(n) << 10)
#define MB(n) ((uint64_t)(n) << 20)
int main(void) {
    vk_pool_recovery s = {0};
    const uint64_t total = KB(7204);
    uint64_t budget = MB(384);
    budget = vk_pool_reuse_budget_update(&s,budget,KB(1407),total);
    assert(budget == MB(64));
    // The export shows repeated recoveries at 1551, 1461 and 1449 MiB.
    // Even three observations near the boundary must not inflate the cache.
    const unsigned available[] = {1551, 1461, 1449, 1551, 1551, 1551, 1411};
    for (unsigned i = 0; i < sizeof available / sizeof available[0]; ++i) {
        budget = vk_pool_reuse_budget_update(&s,budget,KB(available[i]),total);
        assert(budget == MB(64));
    }
    // Sustained clear headroom still restores the full cache.
    for (unsigned i = 0; i < 3; ++i) {
        budget = vk_pool_reuse_budget_update(&s,budget,KB(1800),total);
        assert(budget == (i == 2 ? MB(384) : MB(64)));
    }
    // Critical pressure tightens immediately, and marginal recovery cannot
    // restart the cache. An interrupted recovery loses its streak.
    budget = vk_pool_reuse_budget_update(&s,budget,KB(800),total);
    assert(budget == 0);
    for (unsigned i = 0; i < 3; ++i)
        assert(vk_pool_reuse_budget_update(&s,0,KB(950),total) == 0);
    assert(vk_pool_reuse_budget_update(&s,0,KB(1250),total) == 0);
    assert(vk_pool_reuse_budget_update(&s,0,KB(800),total) == 0);
    for (unsigned i = 0; i < 3; ++i)
        assert(vk_pool_reuse_budget_update(&s,0,KB(1250),total) == (i == 2 ? MB(64) : 0));
    // Unknown total is not a recovery signal.
    for (unsigned i = 0; i < 3; ++i)
        assert(vk_pool_reuse_budget_update(&s,0,KB(2000),0) == 0);
    return 0;
}'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'case.c'
            path.write_text(source)
            exe = path.with_suffix('')
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-I', str(ROOT / 'nxnative/dm/android'), str(path), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
