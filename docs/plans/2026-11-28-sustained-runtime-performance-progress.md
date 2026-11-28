# FFR sustained runtime performance: checkpoint log

Branch: `fix/ffr-performance-audit-20261128`. Verified starting HEAD: `906f30b4e6df87b1068c31878c4857e4896254d1`.
User-reported reference Actions run: 46131889107. No new build has been launched.
Primary target: FFR 1.0.1, S25 FE, Xclipse 940 RADV cc87fdf712, 8 GB, internal scaling Off.

## Status

| Milestone | State |
| --- | --- |
| M0 | Partial: focused recorder regression reproduced; full correlated instrumentation and trace replay outstanding |
| M1 | Partial: recorder synchronization fix, focused host RED/GREEN and stress cases; full native/CI validation outstanding |
| M2 | Partial: fixed-cadence hold/refresh policy and source-clock integration; adaptive admission and integrated validation outstanding |
| M3 | Partial: bounded optional/real acquisition and chronological rejection; source-consumer predicate and dependency pacing pending |
| M4 | Not started |
| M5 | Not started |
| M6 | Not started |
| M7 | Not started |
| M8 | Not started |

This checkpoint's SHA is identified by the Git commit containing this file (git log -1 -- docs/plans/2026-11-28-sustained-runtime-performance-progress.md). No older branch is used.

## M0a: recorder notification reproduction

Read root and nxnative agent guidance, architecture reconstruction/flow-scale sections and existing plans. The queue source at the audited SHA has a shared Boolean g_draining. Both drains set it, and each clears it when leaving. A staggered earlier target can therefore disable notifications for a later target. The recorder broadcasts only if the flag is set. Timeout logs also read a potentially recycled ring header and use CLOCK_REALTIME.

Added devtools/reproduce_dq_drain.py, which extracts queue/drain/recorder functions from the checkout and compiles them with real POSIX threads, a mock Vulkan record executor, and an observed timed wait. Two gated records establish distinct targets and force the first waiter to exit before the second record retires.

Local execution used manually copied source sections from the verified GitHub revision because native git networking was unavailable; not a complete checkout or Android build.

Commands (local copy used --source to point to extracted baseline):
- python3 devtools/reproduce_dq_drain.py: exit 1, expected RED
- python3 devtools/reproduce_dq_drain.py --expect-missed-notification: exit 0

Both reported: completed=32 first_timeouts=0 second_timeouts=1.
Runtime diagnostic: written 32, published 32, read 32, op 0 size 0.
This proves the notification failure in the extracted production path, not the frequency on device. No post-fix measurements yet. No production behavior, shader ABI, memory cap, or game data changed. Harness requires a Linux-like host with pthread_spinlock_t and a C11 compiler; it is not yet registered in the default CI test suite.

## Remaining M0 work and next action

The named diagnostic archive was not accessible in this session; its counts are user-reported evidence, not independently replayed. Full source checkout and Android NDK/build environment unavailable. Do not mark M0 complete until:
- correlate source production/enqueue/dequeue, replay recording, GPU completion, reconstruction, FG, acquisition, accepted present, actual display and retirement by ticket and monotonic time;
- capture cadence/deadlines, oldest queue age, remaining dependencies and resync cause, drain waiter/target/completion/notification sequence, lifecycle epoch, per-heap allocation origin and ADPF worker/wait classification;
- reuse existing capture and timing structures, retain bounded buffers and avoid added GPU waits;
- add deterministic source/WSI/clock traces and replay the supplied archive when accessible. Missing data must be explicit, not synthesized as observed evidence.

Then M1: replace Boolean with correctly synchronized multi-waiter predicate/notification, monotonic timeout, race-free diagnostic counters (audit declarations/readers), and never inspect recycled ring storage in timeout logs. Add tests for simultaneous targets, spurious wake, producer suspension, ring saturation and wrap above 4 GiB; rerun the same red harness without the known-bug flag, then relevant full host tests and Android build. Do not disable two-stage replay.

M2-M8 retain the requested ordering and scope: independent target/source cadence and optional FG feedback; bounded real-frame-first admission preserving chronological output and Vulkan ownership; explicit lifecycle epochs; per-heap reservation and rollback without third graph; residual GSR2/LSFG budget; bounded pipeline prewarm and descriptor CPU profiling; ADPF attribution and integrated host/build/device gates. No premature completion or device acceptance claims. Runtime-only updates remain preferred unless shader incompatibility is demonstrated.

## Risks and gates

The mock record executor does not validate Vulkan external synchronization, hardware memory pressure, WSI or graphics correctness. Preserve Adreno and Vulkan 1.3 compatibility and existing resource ownership. Keep internal scaling Off. Final hardware acceptance and signed APK artifact checks remain outstanding.

## M0b: isolated cadence feedback reproduction

Previous checkpoint: `26a3f64802f21fb22bb55a6893a769a5b5e6aa0f` (M0a). Added devtools/reproduce_fg_cadence.py, extracting the real smoothing expression from lsfg_pacer.c and hold calculation from dm_framegen.c. Synthetic clock: fixed 60 FPS target, 120 Hz, one generated image per source, 20 frames of transient 40 FPS service, then no external disruption. The mock WSI service floor is two output holds per source; this is a stress model, not a replay of real WSI.

Local manually extracted source, same limitation as M0a. Commands:
- python3 devtools/reproduce_fg_cadence.py --root /tmp/ffr-base: expected RED, exit 1.
- python3 devtools/reproduce_fg_cadence.py --root /tmp/ffr-base --expect-feedback: exit 0.

Both produced:

    frame=19 measured_fps=60.000 hold_ms=8.333 next_service_ms=16.667
    frame=39 measured_fps=40.042 hold_ms=12.487 next_service_ms=24.974
    frame=299 measured_fps=40.042 hold_ms=12.487 next_service_ms=24.974

This confirms the existing hold formula can preserve an artificially lengthened service interval under backpressure. It does not establish that the actual device follows this exact model. The full pacer, queue, refresh vote, GPU costs and GSR2 are not exercised. M2 should replace the source extraction seam with a stable production helper and run integrated clock/WSI tests.

Additional corroborating source observations at the audited runtime:
- dm_framegen.c:665-672 defaults the base floor to 30, sets decoupled=0 and feeds the pacer a presentation-worker monotonic timestamp. Lines 690 and 786-792 prefer the greater of measured/requested period for refresh admission and hold.
- gfx_vk_present.c:1737-1740 presents generated outputs before the real output, and 1794 keeps a queue entry occupied until the whole batch returns. Source timestamp is already captured before enqueue waits at 1810; distinguish it from actual entry and dequeue time. pace_desired (1255) charges recent scene GPU time per output and resets ideal cadence on misses; dependencies need ticket correlation.
- gfx_vk_fg_pressure.h:15 computes the cap as heap_available/10.
- gfx_vk_flow_budget.h:19 allocates 20% of cadence; its cooldown/recovery path must be tested against same-workload recovery, not merely configuration changes.
- gfx_vk_source_continuity.h:39-40 treats a gap greater than 250 ms as a pause, rather than an explicit lifecycle event.

M0 remains partial: no runtime instrumentation landed and no capture was replayed. No M1-M8 production fix or full host/device/build acceptance is claimed. Next action remains correlated instrumentation, then recorder synchronization fix. Preserve the two independent red reproducers and keep a separate trace for genuine WSI backpressure.

## M1a: scoped multi-waiter recorder repair

Previous checkpoint: `c70a7ca5bd40c700e97c6d1cf4d58be4016f58ce`. Ordering decision: M0's focused drain regression is established, so fix that independently verified defect now, before completing the broader M0 cross-stage instrumentation. No other runtime behavior changes. M0 remains open; do not mark M1 complete without full native and integration verification.

Changed gfx_vk_dq.c and extended the existing host reproducer. Earliest armed 64-bit target replaces the shared Boolean. Recorder publishes completion and checks armed target with seq_cst atomics; a reached target broadcasts under g_mu and disarms, then incomplete waiters rearm under that mutex. Each waiter rechecks its own predicate after arming and before sleep. No unconditional per-record mutex acquisition. Keep ordered producers and existing Vulkan wrappers. Wrap-marker publication uses the same seq_cst sleep handshake as normal records. Monotonic condition-variable initialization occurs before enabling MT replay; error paths leave the existing single-thread fallback only for initialization failure. Counters are atomic and timeout diagnostics report waiter, target, publication/completion and notification sequence without dereferencing potentially recycled ring memory.

RED: same harness default against copied audited queue/recorder source: exit 1, completed 32, first_timeouts 0, second_timeouts 1.
GREEN: python3 devtools/reproduce_dq_drain.py --source /tmp/ffr-fixed/nxnative/dm/android/gfx_vk_dq.c --stress, exit 0. Six scenarios:
- staggered: completed 32, both timeouts 0;
- equal targets with injected spurious broadcast: completed 16, both timeouts 0;
- wrap: completed 5402263600, both timeouts 0;
- saturation: completed 41943040, timeouts 0;
- suspended publication: completed 16, timeouts 0;
- intentional blocked recording: one true timeout, target 16, published 16, completed 0, notifications 0->0, then prompt retirement after release.
Previous five non-timeout cases also passed 10 consecutive runs (50 cases).

Local source was manually copied from the GitHub revision and transformed identically for the exercised queue and recorder sections. Actual Vulkan wrapper signatures, startup integration, Android link, full host suite, hardware and overhead benchmarks are unverified; do not claim full milestone acceptance. Remaining risk: additional seq_cst load/check per record, Android libc clock support, unrelated external synchronization and device wait behavior. No changes to game data or shader bundle.

Next: finish M0 correlated runtime instrumentation and trace replay when available, run full host and native build on this M1 checkpoint, then implement M2 target-aware scheduling. Preserve chronological interpolation and existing two-graph memory limits.

## M1b: automated test discovery and CI handoff

Prior M1a commit: `84b473583c27a1ef063fe915b113f745e9f7993a`. Verified remote extracted queue and recorder sections match local tested sections by length and FNV-1a hash after trimming trailing whitespace (5390/c8384091 and 915/f21d13b7), and harness Git blob matches local file. Added tests/test_vk_dq_drain.py to the standard unittest discovery used by Android CI; Linux-only because this production queue uses pthread_spinlock_t. Local test against the extracted production fixture: python3 -m unittest discover -s tests -p 'test_vk_dq_drain.py' -v, 1/1 passed (six gated scenarios, including true timeout), 2.085 s. No further production changes.

Baseline Actions run 46131889107 was independently confirmed successful at 906f30b4e6df87b1068c31878c4857e4896254d1. The M1a push triggered Android app run 46136471470; resolution validation was in progress when checked. A subsequent push touching tests triggers another run; inspect the exact checkpoint's run rather than using the older artifact. Full Android and device acceptance remains open. No tag or release change.

M0 and M1 remain partial. Resume at the latest verified branch head, finish the correlated instrumentation, retrieve/replay the original archive, inspect CI for native link and full suite, then proceed with M2. Do not require repeated gameplay captures for the already reproduced drain defect.

## M2a: separate source clock from fixed requested output cadence

Prior checkpoint `6b097ffcd1e2586c30536eaf4be06ca1bdb559e8`. At inspection Android run 46136561472 was still in progress. Re-ran extracted recorder stress unittest: 1/1 passed in 2.002 s. Network clone remains unavailable, and the named capture is still unavailable locally.

RED/GREEN: extended devtools/reproduce_fg_cadence.py before implementing the production helper, ran against extracted audited source: exit 1, frame 299 40.042 FPS, hold 12.487 ms, service 24.974 ms. With the new production helper called from the same hold seam: exit 0, frame 39 measured 40.042 FPS, hold 8.333 ms, next service 16.667 ms; frame 299 60.000 FPS. Additional assertions for 30/40/60 FPS with 1/2/3 generated images and variable-rate measured interval passed. Compiler flags -Wall -Wextra -Werror. Register as test_vk_fg_cadence_feedback.py in normal discovery. This is an isolated mocked-service model, not real WSI or full pacer validation.

Implementation: use already available source_ns from dm_fg_prepare_workload even for nonadaptive Flow Scale, instead of worker invocation timestamp for lsfg_pacer_plan. Explicit 30/40/60 source tickets use requested period for refresh admission and output hold; only variable-rate modes use max(measured, requested). Avoid rounding fixed holds up except sub-microsecond noise. The default min_base becomes 90% of explicit target (27/36/54 FPS); variable rate retains 30, user override retained. This initial tolerance is a tuning decision, not device-proven. Reset pacer and warm streak on explicit rate transitions, not graph ownership. Current fixed configuration is matched to ticket period; exact enqueue-time settings snapshot and explicit lifecycle epochs are still open. Unlocked mode remains the existing no-FG behavior, not an artificial 60 FPS lock.

No new shader format, graph allocation, WSI ownership or display vote changes. The helper/hold seam was executed locally; source-clock hookup, default-floor integration and Android link were reviewed but not run in full checkout. M0 remains partial: queue entry/dequeue/WSI and other timing instrumentation still outstanding. M2 remains partial: adaptive output target continued-overload protection, source ticket-based admission, attributed GPU/WSI evidence and jitter/overload integrated tests remain. Next: finish those seams before claiming the entire cadence feedback architecture fixed.

## M0c: ticket-correlated software presentation queue trace

Previous checkpoint `9631861e383885cb6226432e8a543c0cedec9a34`. Add 12 appended queue columns and actual FG output hold to scaling_frames.tsv, joined by existing source ticket. No additional capture history or unbounded queue: 4096 rows, extra 80 bytes per trace plus hold and alignment (~352 KiB). Capture-only monotonic stamps distinguish source-before-backpressure, actual FIFO entry, dequeue, worker service start/end and CPU-side entry retirement. Occupancy includes in-service entry; entry occupancy is after admission. Oldest source age sampled at admission/dequeue. Synchronous mode is explicit, not represented as FIFO entry/dequeue. No-image entries remain distinguishable. Zero denotes missing observations. Capture lock is acquired outside the queue lock; export may intentionally omit samples not yet published. CPU retirement is NOT a GPU fence or display completion. No queue scheduling behavior changed.

Tests-after instrumentation: local production queue trace writer compiled with strict C11 flags; python3 -m unittest discover -s /tmp/ffr-m0queue/tests -p test_vk_present_queue_trace.py -v: 1/1 passed, 0.052 s. Tests cover column alignment, age arithmetic, async/sync and unavailable fields. Extended existing full capture-column test with actual joined ticket sample and unmatched row; full checkout execution unavailable, CI required. No full instrumentation milestone claim.

M3 audit: mtl_present_wait currently exits when a one-second timedwait returns nonzero even if g_pq_done < tk. This can allow NVN window texture reuse before its CPU-side consumer submission; confirm caller contract and replace with predicate-based retirement or explicit safe failure, not blind timeout success. Also generated WSI calls use ticket 0; their actual display timing needs separate output identity while preserving real-ticket stats.

Remaining M0: individual generated/real WSI acquisition/fence/submit/accept and actual display timestamps; pacing reason/remaining dependencies; replay/FG/reconstruction GPU correlation; lifecycle epoch, memory allocation origins and ADPF worker/wait classes. Archive still unavailable. M2a integration CI and target-aware adaptive protection remain outstanding. Continue from latest verified HEAD.

## M2b: protect explicit source cadence in fixed and adaptive FG

Prior checkpoint `4507241bb65412a514123a4674429c7e31a990db`. Adaptive output demand now uses explicit base target (if configured) instead of a slowed measured rate. Probe profitability continues to use measured actual cadence. A target-aware guard blocks optional work when below floor without previous generated work, or after a sustained 0.5 s deficit, then recovers after 0.5 s healthy source cadence. Adaptive mode re-probes from zero after overload; fixed 2x/3x/4x retain requested multiplier. While blocked, dm_fg_frame does not submit input-copy/seed-only work, resets history count/streak and logs explicit base cadence protection; graph ownership and selected mode remain. Variable-rate policy unchanged. Initial dwell values are tuning decisions; only source cadence (not yet attributed mandatory GPU or WSI costs) drives this guard. M3/M6 must refine optional admission using fresh timing and presentation pressure to avoid unnecessary suppression.

RED/GREEN: test_vk_fg_target_guard.py executed against manually copied actual pacer body/header declarations, with comments omitted in local fixture; no full checkout or Android link. Baseline exit 1, 'slow base added optional frames at output=base target'. Corrected pacer 1/1 unittest passed, 0.080 s, exit 0, strict C11 -Wall -Wextra -Werror. Includes explicit 60/60 slow-start, 60/120 -> 40 -> 60 adaptive recovery, all 30/40/60 and fixed 2x/3x/4x, brief jitter, 1.2-second gap, variable 30->60. Runtime glue and full CI are not locally executed. Previous recorder and M2a resolution host/SPIR-V and software Vulkan jobs succeeded; signed APK job still running at last check. No device claims or game data changes.

Remaining M2: exact source-setting snapshot and ticket continuity in producer/consumer, integrated queue admission and actual generated-output deadlines (M3), explicit lifecycle (M4), mandatory/optional cost correlation (M6). M0 remains partial. Next action: verify exact-head CI, then M3 bounded acquisition, source-consumer wait correctness, and remaining-dependency display pacing. Do not simply change present order or grow FIFO.

## M3a: bounded WSI acquisition and real-frame priority

Prior checkpoint `1e884dfaf60142a6726f11457764d123c290c1c3`. Generated outputs retain chronological prefix ordering but no longer block indefinitely on a busy presentation slot or image acquisition. The optional deadline is derived from pre-backpressure source timestamp plus planned batch interval minus up to 2 ms reserved for the real output; acquisition waits no longer than one output hold or remaining optional deadline. Optional fence wait is a zero-time poll. Skip the remaining generated suffix after first failed attempt, then present real with the remaining interpolation interval. Real/redraw acquisition uses finite 50–100 ms request (one existing recreate/retry allowed); explicit TIMEOUT/NOT_READY return without command recording or fence reset. Only SUCCESS/SUBOPTIMAL enter acquired-image consumer code. Once an image is acquired it MUST be submitted/presented; do not abandon on deadline expiry. No new queue-wide idle and no change to semaphore/slot ownership. Export separate fg_present_reason alongside submission and accepted counts.

RED/GREEN: test_vk_present_admission.py against manually copied actual present_one prefix (before command recording) and mocked Vulkan WSI: baseline exit 1 'acquire timeout reached recording' (positive VK_TIMEOUT would be mishandled by previous r<0 check, exposed by bounded-timeout mock); corrected exit 0, 1/1 test passed in 0.066 s. Covers expired deadline before any wait, busy fence, timeout/not-ready/out-of-date, success/suboptimal, finite real timeout and interpolation interval after skipped prefix/suffix. Extended full present_window mock and capture column tests; not executed locally because full checkout unavailable. Driver can still block inside submit/present/swap recreation and required GPU fence wait is unbounded. M3 incomplete. This is a conservative first admission policy: 2 ms reserve and 50–100 ms real acquire are initial tuning, not measured hardware-optimal. GPU work already submitted by FG is not canceled by WSI rejection; M3/M6 must pre-admit optional GPU work using queue pressure and attributed costs.

M3 next: fix mtl_present_wait so timeout never grants unsafe texture reuse (NVN caller unconditionally returns SUCCESS and publishes sync after wait). Use predicate and monotonic wait diagnostics; keep queue bounds and safe ownership. Then remaining GPU dependency display pacing, per-output ticket correlation and integrated WSI tests. Exact-head CI/native link and device validation pending. No game data or shader bundle changes.
