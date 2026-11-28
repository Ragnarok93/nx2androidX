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
