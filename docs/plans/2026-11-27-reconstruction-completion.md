# Universal reconstruction completion

Work on `feature/universal-gsr2-20261124` in Ragnarok93/nx2androidX, based on
`android`. Use FFR 1.0.1 with internal scaling Off as the primary regression.
Keep Fixed and Adaptive internal scaling, APK upgrades and signing outside the
implementation scope. Preserve the original guest rendering path whenever a
replacement cannot be verified.

| Milestone | Depends on | Design and acceptance |
| --- | --- | --- |
| 0. Diagnostic integrity | Baseline | Correlate source tickets, runtime Git and ELF identity, selected driver, crop and extents. Record command recording, submission, completion, consumption and present acceptance independently. Keep rolling capture bounded and verify legacy archive parsing. |
| 1. Source discovery | 0 | Extend replay resource tracking with bounded, observation-only producer/consumer traversal from the presented image. Resolve mip, layer, alias, valid region and write order before proposing scene color. Record evidence and reject ambiguous candidates. Measure replay CPU and memory overhead. |
| 2. Guest composition | 1 | Prove enlargement eligibility from operation and shader semantics, then replace transactionally at the guest's expected extent. Preserve color transforms, temporal effects, blending, UI, layouts and later consumers. Prefer an intact guest path on ambiguity or failure. |
| 3. Spatial processing | 2 | Share crop, orientation, format, target and lifetime contracts across Bilinear, FSR 1 and GSR 1. Separate optional sharpening from enlargement and test native 1:1 sharpening and monotonic slider response with GPU readback. |
| 4. GSR 2 history | 3 | Define image-driven and verified engine-input capability levels. Validate motion, depth, exposure and jitter conventions. Commit history once per submitted real ticket. Invalidate on incompatible source or output changes and failed submissions. Keep sharpened and LSFG-generated images out of history. |
| 5. GSR 2 pacing | 4 | Use measured workload, queue backlog, deadlines and 30/40/60 FPS pacing. Model timing availability explicitly. Use hysteresis and minimum stable intervals for controlled fallback and recovery. |
| 6. Output and resources | 5 | Keep scene, guest, reconstruction, composition, LSFG, drawable and swapchain extents distinct. Validate Vulkan limits, arithmetic, dispatch and memory budgets through 2160p and supported higher targets. Reuse and retire allocations under 8 GB device constraints. |
| 7. LSFG | 6 | Feed LSFG the completed real image intended for display. Validate resource ordering, fences, chain rebuilds, generated-frame accounting and display-oriented resolve of above-display targets. |
| 8. Regression coverage | 7 | Run host, SPIR-V and numerical Vulkan checks and the device matrix below. Compare CPU replay, GPU time, deadline misses, p50/p95/p99 frame time, history stability, backend residency and peak/sustained memory. |
| 9. Delivery | 8 | Audit synchronization and ownership, consolidate reconstruction paths, update ARCHITECTURE.md, run regression validation and dispatch the signed Android workflow on the verified development checkpoint. Record run and artifact identity. |

The existing physical-input handoff is the reference for image lifetime and
synchronization. A naturally lower-resolution source must carry equally strong
provenance. A pass-level binding is insufficient to authorize replacement.
Guest composition extents remain governed by guest resource contracts. The
reconstruction target can exceed the drawable through offscreen supersampling.

Test FFR on Xclipse 940 with the Samsung and RADV drivers, Adreno 650 and newer
Adreno hardware when available. Include a second rendering architecture.
Exercise handheld and docked, internal scaling Off, each backend, Auto/1080p/
1440p/2160p, sharpness 0/50/100%, LSFG Off and on, 30/40/60 FPS, backend and
mode transitions, surface recreation, orientation and sustained gameplay.
Target 20 minutes for primary configurations when practical. Archive exact
runtime identity and trace outputs. Use Awaiting device validation for host
results with hardware acceptance outstanding.

For each coherent checkpoint, review the diff, run applicable host, shader,
static and build tests, append results to the progress log, commit and update
the remote branch using a fast-forward. Verify the remote ref before starting
the next milestone. On resume, fetch the development branch, read both records,
inspect intervening commits and resume from the recorded next action.
