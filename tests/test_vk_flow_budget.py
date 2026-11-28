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
static void check_cadence_pressure(void) {
    const int rates[] = {30, 40, 60};
    for (unsigned rate = 0; rate < sizeof rates / sizeof rates[0]; ++rate) {
        dm_flow_cadence cadence = dm_flow_cadence_estimate(0.0, rates[rate]);
        assert(cadence.period_us && cadence.budget_ms > 3.3f);
        dm_flow_workload workload = {.iw = 1280, .ih = 720, .ow = 1920, .oh = 1080,
            .cadence_us = cadence.period_us};
        dm_flow_budget p = {0};
        dm_flow_budget_set_workload(&p, &workload);
        // Overlapped CPU/GPU work uses their maximum. One transient miss
        // does not change residency, including with extra generated presents.
        unsigned work_us = (cadence.period_us * 8) / 10;
        assert(!dm_flow_frame_stressed(cadence.period_us, work_us, work_us, 1, 0));
        for (uint64_t frame = 1; frame <= 200; ++frame) {
            unsigned queued = frame == 20 ? 2 : frame % 2;
            dm_flow_budget_pressure(&p, frame,
                dm_flow_frame_stressed(cadence.period_us, work_us, work_us, queued, frame == 55));
            dm_flow_budget_observe(&p, p.generation, frame, frame + 1, cadence.budget_ms,
                frame % 2 ? 3.35 : 2.6);
            assert(dm_flow_budget_allow(&p, frame + 1));
        }
        for (uint64_t frame = 201; frame <= 204; ++frame) {
            assert(dm_flow_frame_stressed(cadence.period_us, work_us, work_us, 2, 1));
            dm_flow_budget_pressure(&p, frame, 1);
            // Multiple generated presents from this real frame do not
            // count as additional pressure.
            dm_flow_budget_pressure(&p, frame, 1);
            assert(dm_flow_budget_allow(&p, frame) == (frame != 204));
        }
        assert(!dm_flow_budget_allow(&p, 323));
        assert(dm_flow_budget_allow(&p, 324));
        assert(dm_flow_frame_stressed(cadence.period_us, 0, cadence.period_us, 0, 0));
    }
    assert(!dm_flow_frame_stressed(0, 0, 0, 0, 0));
    assert(dm_flow_waiting_sources(0, 1) == 0);
    assert(dm_flow_waiting_sources(2, 1) == 1);
    assert(dm_flow_waiting_sources(3, 1) == 2);
    assert(dm_flow_waiting_sources(2, 0) == 2);
    assert(dm_flow_cadence_estimate(0.0, 0).budget_ms == 2.0f);
    assert(dm_flow_cadence_estimate(1.0 / 60, 30).period_us == 33333);
}

int main(void) {
    check_cadence_pressure();
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
    // Pause and skipped source tickets reset history, not measured overload.
    uint64_t before_pause=p.generation;
    dm_flow_budget_source_reset(&p,DM_RECON_SOURCE_PAUSE);
    assert(p.valid && p.generation!=before_pause && p.retry_after==725);
    dm_flow_budget_source_reset(&p,DM_RECON_SOURCE_GAP);
    assert(p.valid && p.retry_after==725 && p.overload_events==1);
    // In-flight results from before the demotion cannot extend the cooldown.
    dm_flow_budget_observe(&p, gen, 605, 610, 2.5f, 9.0);
    assert(p.retry_after == 725);
    assert(!dm_flow_budget_allow(&p, 724));
    assert(!dm_flow_budget_recovery_due(&p,724));
    assert(dm_flow_budget_recovery_due(&p,725));
    // A competing recovery veto leaves the original retry epoch intact.
    assert(dm_flow_budget_recovery_due(&p,726)&&p.retry_after==725);
    assert(dm_flow_budget_allow(&p, 725));
    assert(!dm_flow_budget_recovery_due(&p,726));
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
    dm_flow_budget_source_reset(&p,DM_RECON_SOURCE_PAUSE);
    assert(p.retry_after==UINT64_MAX && !dm_flow_budget_allow(&p,5000));
    // A genuine geometry change can trigger a new probe immediately.
    dm_flow_budget_source_reset(&p,DM_RECON_SOURCE_GEOMETRY);
    assert(!p.valid);
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
