"""Host regression for the production image-driven reconstruction budget policy."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ANDROID = Path(__file__).resolve().parents[1] / 'nxnative/dm/android'


@unittest.skipUnless(shutil.which('cc'), 'host C compiler required')
class FlowBudgetTests(unittest.TestCase):
    def test_sustained_overload_and_generation_scoped_recovery(self):
        source = r'''
#include <assert.h>
#include "gfx_vk_flow_budget.h"
int main(void) {
    dm_flow_budget p = {0};
    dm_flow_workload key = {.iw=1280, .ih=720, .ow=1920, .oh=1080,
        .cadence_us=33333, .source_format=1, .output_format=1};
    dm_flow_budget_set_workload(&p, &key);
    assert(dm_flow_budget_allow(&p, 100));
    uint64_t gen = p.generation;
    // Affordable over several former retry intervals, with isolated spikes.
    for (uint64_t t=100; t<600; ++t) {
        dm_flow_budget_observe(&p, gen, t, t+1, 2.5f, t%13==0 ? 4.0 : 1.8);
        assert(dm_flow_budget_allow(&p, t+1));
    }
    // A missing sample is not a measurement, and a duplicate cannot count twice.
    for (uint64_t t=600; t<604; ++t) {
        dm_flow_budget_observe(&p, gen, t, t+1, 2.5f, 3.8);
        dm_flow_budget_observe(&p, gen, t, t+1, 2.5f, 3.8);
        assert(dm_flow_budget_allow(&p, t+1));
    }
    dm_flow_budget_observe(&p, gen, 604, 605, 2.5f, 3.8);
    assert(!dm_flow_budget_allow(&p, 605));
    assert(p.retry_after == 725);
    // In-flight results from before the demotion cannot extend the cooldown.
    dm_flow_budget_observe(&p, gen, 605, 610, 2.5f, 9.0);
    assert(p.retry_after == 725);
    assert(!dm_flow_budget_allow(&p, 724));
    assert(dm_flow_budget_allow(&p, 725));
    uint64_t probe = p.generation;
    assert(probe != gen);
    for (uint64_t t=725; t<730; ++t)
        dm_flow_budget_observe(&p, probe, t, t+1, 2.5f, 3.8);
    assert(!dm_flow_budget_allow(&p, 730));
    assert(p.retry_after == 970);
    // Two more failed probes end the recurring bursts under an unchanged workload.
    assert(dm_flow_budget_allow(&p, 970));
    uint64_t probe2 = p.generation;
    for (uint64_t t=970; t<975; ++t)
        dm_flow_budget_observe(&p, probe2, t, t+1, 2.5f, 3.8);
    assert(p.retry_after == 1455);
    assert(dm_flow_budget_allow(&p, 1455));
    uint64_t probe3 = p.generation;
    for (uint64_t t=1455; t<1460; ++t)
        dm_flow_budget_observe(&p, probe3, t, t+1, 2.5f, 3.8);
    assert(p.retry_after == UINT64_MAX && !dm_flow_budget_allow(&p, 5000));
    // A geometry or cadence change can trigger a new probe immediately.
    key.iw = 1024;
    dm_flow_budget_set_workload(&p, &key);
    assert(dm_flow_budget_allow(&p, 5001));
    dm_flow_budget_observe(&p, probe, 5001, 5002, 2.5f, 20.0);
    assert(p.slow == 0);
    uint64_t recovered = p.generation;
    for (uint64_t t=5001; t<5070; ++t)
        dm_flow_budget_observe(&p, recovered, t, t+1, 2.5f, 1.8);
    assert(p.backoff == 120 && dm_flow_budget_allow(&p, 5070));
    // A post-sharpen pass changes cost generation, not temporal history identity.
    key.sharpen = 1;
    dm_flow_budget_set_workload(&p, &key);
    assert(p.generation != recovered && dm_flow_budget_allow(&p, 5071));
    uint64_t current = p.generation;
    dm_flow_budget_retire_samples(&p);
    dm_flow_budget_observe(&p, current, 5071, 5072, 2.5f, 9.0);
    assert(p.slow == 0 && p.valid);
    unsigned failures = 0;
    assert(dm_flow_resource_defer(&failures, 100) == 220);
    assert(dm_flow_resource_defer(&failures, 220) == 460);
    assert(dm_flow_resource_defer(&failures, 460) == 940);
    assert(dm_flow_resource_defer(&failures, 940) == 1900);
    assert(dm_flow_resource_defer(&failures, 1900) == 2860);
    dm_flow_budget_invalidate(&p);
    assert(!p.valid && p.generation != recovered);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'case.c').write_text(source)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ANDROID), str(root / 'case.c'), '-o', str(root / 'case')], check=True)
            subprocess.run([str(root / 'case')], check=True, timeout=10)


if __name__ == '__main__':
    unittest.main()
