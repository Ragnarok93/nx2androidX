# FFR sustained runtime performance: checkpoint log

Branch: `fix/ffr-performance-audit-20261128`. Verified starting HEAD: `906f30b4e6df87b1068c31878c4857e4896254d1`.
User-reported reference Actions run: 46131889107. No new build has been launched.
Primary target: FFR 1.0.1, S25 FE, Xclipse 940 RADV cc87fdf712, 8 GB, internal scaling Off.

## Status

| Milestone | State |
| --- | --- |
| M0 | Partial: focused recorder regression reproduced; full correlated instrumentation and trace replay outstanding |
| M1 | Not started: production unchanged |
| M2 | Not started |
| M3 | Not started |
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
