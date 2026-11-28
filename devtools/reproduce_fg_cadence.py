#!/usr/bin/env python3
"""Exercise the existing LSFG hold and smoothing math with a mocked WSI clock.

This deliberately isolates a service-time feedback loop; it does not emulate
FIFO presentation, the full pacer, GPU work or Android display behavior.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

DRIVER = r'''
#include <math.h>
#include <stdint.h>
#include <stdio.h>
static struct { struct { float smoothed_interval; } pacer; } F;
static void observe(float interval) {
    typeof(F.pacer) *p = &F.pacer;
    SMOOTHING_EXPRESSION
}
static double output_hold(double period, unsigned ng, uint64_t rns) {
    double result = period, *hold = &result;
    HOLD_EXPRESSION
    return result;
}
int main(int argc, char **argv) {
    (void)argv;
    const double target = 1.0/60.0;
    const uint64_t refresh = 8333333;
    double service = target;
    F.pacer.smoothed_interval = target;
    for (int frame=0; frame<300; ++frame) {
        /* A brief external disruption. It disappears at frame 40. */
        if (frame>=20 && frame<40) service = 1.0/40.0;
        observe((float)service);
        double hold = output_hold(target, 1, refresh);
        /* Mock service cannot complete a two-output batch before both holds.
         * This is an explicit stress model, not a claim about actual WSI. */
        service = fmax(target, 2*hold);
        if (frame==19 || frame==39 || frame==299)
            printf("frame=%d measured_fps=%.3f hold_ms=%.3f next_service_ms=%.3f\n",
                   frame, 1.0/F.pacer.smoothed_interval, hold*1000, service*1000);
    }
    if (argc==2) return !(service > 0.024 && service < 0.026);
    return service > target + 0.0001;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--expect-feedback', action='store_true')
    args = parser.parse_args()
    android = args.root / 'nxnative/dm/android'
    fg = (android / 'dm_framegen.c').read_text()
    pacer = (android / 'lsfg/lsfg_pacer.c').read_text()
    smoothing = next(line for line in pacer.splitlines() if line.startswith('#define INTERVAL_SMOOTHING '))
    expression = next(line for line in pacer.splitlines() if 'p->smoothed_interval = p->smoothed_interval > 0' in line)
    hold = fg[fg.index('    double interval = F.pacer.smoothed_interval > 0'):fg.index('report:;', fg.index('    double interval = F.pacer.smoothed_interval > 0'))]
    source = smoothing + '\n' + DRIVER.replace('SMOOTHING_EXPRESSION', expression).replace('HOLD_EXPRESSION', hold)
    with tempfile.TemporaryDirectory() as tmp:
        c = Path(tmp) / 'cadence.c'
        exe = Path(tmp) / 'cadence'
        c.write_text(source)
        subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-O2', str(c), '-lm', '-o', str(exe)], check=True)
        return subprocess.run([str(exe)] + (['baseline'] if args.expect_feedback else []), timeout=5).returncode


if __name__ == '__main__':
    raise SystemExit(main())
