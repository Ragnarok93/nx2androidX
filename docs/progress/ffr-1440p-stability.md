# FFR 1440p follow-up

Baseline: `366c2af2e7b3da46546d861bc705883530573530`. Archive: nx2android-render-diagnostics-20261127-195412.zip. 294/330 GSR 2 requests fell back on sustained overload; 783 real presents, zero generated. Fixed 50% rejected before allocating scaled backing. The 20 CPU read warnings occur in opt-in indirect logging, not an established gameplay coherence failure.

## Checkpoint 1

- Add immutable per-real-frame FG decision, cadence, refresh, planned generation and warm-up fields to scaling_frames.tsv.
- Split attachment provenance refusal by the actual failing predicate without relaxing ownership checks.
- Skip unsynchronized indirect diagnostic samples; GPU copies remain authoritative.
- Trim idle pool reuse immediately under pressure and require three consistent samples to restore the higher tier.
- Add capture-column and pool recovery regression coverage. Native build and device validation remain required.

## Remaining

Resolve attachment ownership using the specific rejection and lifetime proof; validate docked pre-composition source; isolate FG pacing without snapshots; optimize temporal stages against measured budget; make snapshot export nonblocking where lifetime permits; subtract intentional pauses; expose requested/active backend and actual extent. Do not disable safety fallbacks or force speculative scene replacement.

## Checkpoint 2

Preserve GSR 2 measured overload cooldown across pause/gap discontinuities while retiring timing samples and resetting temporal image history. Geometry, encoding, operation mode and surface changes can still invalidate the workload. This removes pause-induced recovery bursts without raising the GPU budget or guaranteeing 1440p GSR 2 performance.

## Checkpoint 3

Initial CI failed in native link (missing GPU pending declaration) and two extracted-code harnesses (missing decision/skip test boundaries). Update declaration and harnesses without changing production behavior; register pool recovery regression in focused runner. Re-run CI before claiming validation.

## Checkpoint 4 — workload reduction

Previous corrected reconstruction CI passed (run 46096506583). The 1440p archive's slot-0 surface usage records submission 16391 pass 134 writing 1728x972 image 765046cc00, then pass 135 fragment 7d05eef41136554f sampling that image at slot 1 into 1920x1080. The preceding 1384x780 scene surfaces are not directly the final composition input. The existing replay certificate admits the source but the sampler predicate rejects it; no exact sampler state is captured, so do not relax that guard speculatively.

LSFG Auto previously mapped 0 to full-resolution optical flow (1920x1080, 152 images in the capture). Resolve Auto to a stable resolution-derived tier targeting at most a 720p flow pixel budget where possible; 1080p uses 50%, 720p uses 100%, explicit settings unchanged. This is an initial policy, not dynamic GPU feedback. GSR 2 motion search stops fetching a candidate's remaining taps when the nonnegative partial error already exceeds its comparison limit. Search, reverse and ambiguity decisions retain their original thresholds. Add host regressions and retain GPU readback validation. Device timings and LSFG generation still need measurement.
