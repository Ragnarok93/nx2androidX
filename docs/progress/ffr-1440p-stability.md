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

The prior APK workflow 46096506618 failed in the full test suite because the legacy memory-pressure source assertion checked the old direct threshold call. Updated that assertion to verify the hysteresis monitor wiring, retained all existing completed-fence checks, and added the legacy suite to the focused runner.

## Checkpoint 5 — asynchronous full-image diagnostics

Transfer only retired full-image readbacks to a single background writer. No full-frame RAM copy, no extra in-flight batch, and no completion marker before successful raw/manifest publication. A stopped/exporting session omits its unfinished slot; replay can continue without waiting for the writer. Small input and surface reports remain synchronous, as does the legacy manual dump path or thread-allocation failure fallback. GPU readback recording itself can still perturb frame pacing. Added capture lifecycle regression for completion, export during write, and write failure. CI and on-device timing remain required.

## Checkpoint 6 — pause accounting

Sample Android host wall and pause-adjusted active clocks together under the existing guest freeze-reason lock. Long-frame and adaptive-scale CPU work use the active interval; trace retains wall duration and excluded pause time. The deterministic guest clock, presentation pacing, and other platform paths are unchanged. Regression exercises overlapping freeze reasons and an unpaused 100 ms stall. Mark asynchronous capture helpers inline for extracted test translation units.

## Checkpoint 7 — snapshot timeline and docked admission evidence

Initial asynchronous writer native link passed; isolated capture-column test failed on unused helper warnings (addressed by inline helpers in checkpoint 6). Preserve timeline rows while the asynchronous writer runs and initialize its crash stack. Both earlier archives omit sampler descriptor rows for certified fragment 7d05eef41136554f. Log the exact sampler state under its registration lock with a bounded refusal bitmask: 1 missing Vulkan sampler, 2 missing registration, 4 forced nearest, 8 mag filter, 16 min filter, 32 wrap, 64 anisotropy, 128 comparison, 256 reduction. Admission is unchanged. Need this state or an equivalent proven contract to resolve docked replay safely.

## Checkpoint 8 — truthful graphics menu

Prior combined CI and signed APK workflows passed at 42ff776. The Graphics menu now displays the selected upscaler and target separately from the last accepted real frame's output pass and actual reconstruction extent (before display/FG resolve). A replay temporal flag identifies GSR 2 replay separately from the final pass. A paused setting edit, rejected real present, or redraw cannot publish a new last-frame result. No new rows, touch-layout changes, or settings format changes. Host present regression covers status publication and native bypass. Last-frame status does not claim that an untracked guest spatial draw ran.

## Checkpoint 9 — shader-product provenance

Carry the validated DMSV product receipt key, recorded SHA-256, header, size and translation completeness into newly generated data manifests. Runtime-only upgrades check the existing bundle header/size against recorded provenance and reject missing or structurally mismatched bundles. No full bundle reread or mandatory translation on a runtime upgrade; legacy manifests continue to use the original structural checks. The receipt key includes translation inputs, not a declared incompatible shader ABI; it is not compared blindly with a newer translator. Recorded digest is identity, not a fresh hash of the upgrade payload. Tests cover valid, legacy, missing and mismatched cases.

## Checkpoint 10 — provenance fast-path repair

APK CI at 81d2f39 failed two existing zero-read shader cache assertions. Persist the validated DMSV header in the same content receipt on first use; subsequent reuse reads only receipt metadata. The metadata-change path still verifies the complete digest before updating stat identity. Preserve original cache tests and assert provenance remains identical on the zero-read path.

## Checkpoint 11 — skip rejected GSR 2 history work

Optimize only the image-driven fragment variant (generated from the unchanged vendor import). For reset, fully rejected tile or out-of-bounds reprojection, preserve the five current-color Lanczos taps and min/max clamp, but omit variance exponential weights, history sample, normalization and blend. Reuse the center RGBA tap and bound source coordinates before the flow tile fetch. Valid-history and per-pixel rejection paths retain existing arithmetic. Extend real Vulkan readback fixture to compare a rejected scene with a reset of the same scene, including alpha. No change to output history format, budget or vendor authoritative-depth variants. Hardware GPU timing still required.

## Checkpoint 12 — bounded LSFG fixed-mode cooldown

The pacer previously treated a planned generation count as issued GPU work even if warm-up or the refresh cap suppressed it, and repeatedly extended the fixed-mode deficit cooldown while already suppressed. Publish the submitted count after the actual command path. During fixed-mode cooldown, clear deficit accounting and honor the original deadline; once expired, permit a new bounded probe, requiring actual prior generation before accumulating a new deficit. The 30 FPS floor, refresh cap, target-mode policy and generated-frame lifetime remain unchanged. Added sustained-low-rate and rejected-plan regressions to the actual pacer harness and registered that suite in focused CI. This is a source-level defect, not proof that it caused the entire 1440p capture's zero-generation interval.

## Checkpoint 13 — direct GSR 2 RCAS output

For linear RGBA8/BGRA8 UNORM only, sample the unsharpened FP16 history directly in the RCAS output pass using the retired slot's private descriptor. Emulate the prior UNORM8 quantization before the nonlinear filter; preserve alpha and history, and retain the original intermediate conversion for sRGB, other encodings or descriptor failure. `DM_GSR2_STAGED_RCAS=1` provides an A/B and driver compatibility override. The direct path avoids one full-resolution blit and does not require a new intermediate image. Existing cached intermediate images remain reusable by other backends. Extend fault-injected allocation and Vulkan pixel comparison coverage; no hardware timing claim.

## Checkpoint 14 — explicit identity swizzle admission

The native Vulkan sampler view resolver composes NVN R/G/B/A with the format's component mapping; an explicit identity therefore has the same semantics as no selector. The scaling scope previously rejected any explicit texture swizzle, even this equivalent form. Admit the exact packed identity in replay lifetime checks, sampled-wrapper matching and compute eligibility, and enforce the same rule in backing allocation. Nonidentity selectors, explicit layout, packaged content, CPU-accessible pools, alias, full-overwrite and bounded lifetime checks remain unchanged. Add positive/negative view and sampled-wrapper controls. Existing FFR captures do not show which provenance predicate actually failed, so this is a proven source-level false rejection, not a confirmed FFR fix.

## Checkpoint 15 — fresh allocation harness integration

Reconstruction CI at 79c4533 passed; Android workflow 46103476777 passed real Vulkan raster but failed its extracted fresh-owner transaction harness on the missing identity-swizzle declaration. Include the production view header and exercise the real allocator's rejection of nonidentity swizzle and acceptance of explicit identity while preserving the complete rollback assertions. Earlier cache fast-path APK at a176a39 passed.

## Checkpoint 16 — independent temporal sampler admission

The docked rejection conflated two contracts: spatial guest filters sample with the original sampler and need linear-clamp, but GSR 2 takes a proven full RGBA8 source snapshot and independently samples it. Its composite shader uses the original sampler only for the original smoothstep fallback when the fragment full-grid/center test fails. Separate normal-color sampler capability from linear-clamp capability. Permit the independent GSR 2 route for registered noncomparison, nonreduction color samplers even if their original address/filter/aniso differs; retain the full source identity, transfer, current-clear, pipeline, ordering and deferred-pass proofs. If temporal fails and the original sampler is not linear-clamp, use the original guest shader, never the incompatible spatial replacement. Bilinear/FSR1/GSR1 admission is unchanged. Host tests cover both capabilities and fallback; actual FFR sampler state and sustained replay remain unknown. This is an intentional opt-in algorithm change on the certified full grid, not a claim that all sampling modes are equivalent.

## Checkpoint 17 — independent spatial sampling and complete cache harness wiring

Reconstruction and resolution CI passed at 391c99c, but the APK's general texture cache harness lacked the new identity-swizzle declaration. Include the production view helper. Spatial reconstruction now binds a submission-owned third descriptor set containing the proven source view and a dedicated linear-clamp sampler. FSR1/GSR1 use it for their taps; Bilinear uses the existing certified SPIR-V patch with a compatible guest sampler, or the guarded full-grid shader with the independent sampler otherwise. Native fallback continues using the original guest set; no sampler replacement or guest descriptor mutation. GSR2 can fall back to the independent GSR1 path. Add Vulkan readbacks with nearest/repeat original sampler, original cropped fallback, coverage/alpha and reference comparison for all three spatial modes. No proof yet of actual FFR admission or speed; CI pending.
