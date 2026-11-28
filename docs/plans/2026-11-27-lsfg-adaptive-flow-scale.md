# LSFG Adaptive Flow Scale Implementation Plan

> **For agentic workers:** Use the host's available task-by-task implementation workflow. Steps use checkbox (`- [ ]`) syntax for tracking. Commit and record evidence at each milestone; resume from the remote HEAD, not the initial baseline.

**Goal:** Make Flow Scale Auto a memory-bounded, performance-adaptive LSFG policy without sacrificing real-frame cadence, visual continuity, or Android stability.

**Architecture:** Port the useful policy ideas (not the Vulkan layer) from GameNative's native LSFG controller to the C present runtime. Start from the existing resolution-derived scale, observe completed GPU samples, run a shadow governor, then enable transitions only after a bounded graph lifecycle is proven. At most two live graphs including pending/retiring; keep the current fixed Auto behavior as the fallback.

**Tech Stack:** C11, Vulkan, existing Android native runtime, Python unittest/extracted C harnesses, software Vulkan, Android APK Actions.

## Global constraints

- Android only; preserve Adreno and Xclipse paths, user-provided Lossless.dll, manual 25–100% flow values, existing fixed multiplier/target behavior and immediate deadline protections. No guest shader translation or data format changes.
- Use actual LSFG input extent, not selected reconstruction target; preserve output extent, color encoding and real/generated ordering.
- No waits added for timing, no queue-wide idle for adaptive transitions, no unbounded cached graphs. Do not bypass existing source/consumer lifetime or refresh/floor checks.
- Base: `519133439d9f65abd5dc15624b53b32409d2acf7` on `feature/universal-gsr2-20261124`. Preserve newer NVN sync lifetime repair and unrelated work.
- References: GameNative master `8dae64ef4f2ea67f9c54f69676a9599094576447`; native `Ragnarok93/lsfg-vk-android@c0e1103550c61c9893a17aa2ecccb197d91b9db5`, `src/adaptive_flow_controller.cpp`, `framegen/v3.1p_src/context.cpp`, `src/context.cpp`. Do not import all-state prebuild.
- For every task: add focused failing regression; record failure, implement, rerun focused and affected integration suites, commit with Conventional Commits; report SHA, Actions links, pass/fail and unresolved gates. If remote-only environment prevents local test, use CI and label the missing red/green evidence honestly.
- Keep production Auto fixed until tasks 1–4 pass; activation initially opt-in via `DM_LSFG_ADAPTIVE_AUTO=1`. Default enable only after device acceptance. Proposed thresholds are initial tuning decisions, not hardware-proven.

---

## Task 1: Completed LSFG GPU telemetry

**Files:** modify `nxnative/dm/android/dm_framegen.c`, `lsfg/lsfg_chain.c`, `lsfg/lsfg.h` (relative to Android directory); create proposed `gfx_vk_fg_timing.h`; tests `tests/test_vk_fg_timing.py`, existing slot-failure harness; register in `tests/run_reconstruction_host.py`.

**Interfaces:** per-submission sample with monotonically increasing serial, workload epoch, scale, extent, generation count, copy/shared/refinement/generate/total GPU durations; actual accepted output counts recorded separately. Query storage owned by the existing retired FG command slots.

- [ ] Red: test counter wrap (valid bits 32/64), missing query, duplicate serial, old epoch, history-only, failed submit, no timestamp support, slot reuse; no stale timing published.
- [ ] Implement nonblocking timestamp collection: reset per-slot range after its existing retirement, record boundaries for copy, shared mipmaps/alpha/beta, gamma/delta and output generation per generated image, read only completed slots without WAIT. Check supported bits/function pointers; no added fence waits. Sum disjoint intervals, count mipmaps separately where available. Never interpret CPU wait time as GPU work.
- [ ] Log completed sample identity/scale/count and stage times, expose requested/effective scale. Capture data must distinguish generated batches from history-only. Do not change pacing or scale.
- [ ] Green: `python3 -m unittest discover -s tests -p 'test_vk_fg_timing.py'`; existing slot regression and `PYTHONPATH=. python3 tests/run_reconstruction_host.py`; Android native link. Confirm capture support on driver with no timestamps falls back cleanly.

## Task 2: Pure shadow governor

**Files (proposed):** `nxnative/dm/android/gfx_vk_fg_governor.h`, `tests/test_vk_fg_governor.py`; integrate into `dm_framegen.c` and focused runner.
**Interfaces:** reset(initial_pct, epoch); observe(completed sample, generation count, elapsed active seconds, budget, attributed pressure, transition status) -> requested_pct/reason. No Vulkan calls or ownership.

- [ ] Red: steady headroom, sustained attributable overload, isolated spikes, no generation, stale/duplicate/mismatched samples, WSI-only loss, unreachable refresh, scheduler/geometry resets, manual bypass, no-profit rollback and recovery.
- [ ] Implement bounded discrete scales 25..100%, initially 10-point changes clamped to endpoints, start at existing resolution policy. Use total LSFG versus available budget, predict only scale-sensitive work as squared scale ratio. Initial pressure 90%, recovery 82%, 0.4 s sustained down, 4 s sustained up; use actual completed generated work and clamp active observation deltas to 250 ms. No evidence from pauses or rejected batches. No scale change on WSI-only/CPU-only pressure. Keep source-floor and target protections authoritative. Thermal/memory pressure may veto recovery; do not infer benefit from saturation alone.
- [ ] Implement shadow reporting only, no chain rebuilds. Before a live transition, governor must have a distinct request/acknowledgement boundary: applied scale is changed only after backend success; rejected request backoff; evaluate benefit at comparable cadence/workload, roll back unprofitable steps.
- [ ] Green: `python3 -m unittest discover -s tests -p 'test_vk_fg_governor.py'`; full focused runner and native link. Verify manual and Auto produce identical commands to baseline in shadow mode.

## Task 3: Bounded graph ownership and transition mechanism

**Files:** `nxnative/dm/android/lsfg/lsfg_chain.c`, `lsfg/lsfg.h`, `dm_framegen.c`, `gfx_vk_present.c`; proposed `tests/test_vk_fg_graph_transition.py` and Vulkan integration fixture.
**Interfaces:** build uninitialized graph without queue wait; record initialization on owned submission; explicit active/pending/retiring state; completed generation and presentation-consumer retirement tokens; resource byte accounting.

- [ ] Red: fail each allocation/bind/pipeline/submit phase, active graph stays valid, no output destruction while consumer pending, no third live graph, generation failure and discontinuity during handoff, disable/reconfigure races.
- [ ] Split shared full-resolution input/output resources from flow-dependent storage where safe. Record image initialization in queue order without touching already-live shared outputs. Pipeline construction must not stall present; use bounded background construction with device lifetime synchronization or proven incremental construction. Count real VkMemoryRequirements allocations (plus buffers) and reject pending graph before exceeding explicit memory cap. Initial conservative cap: min(128 MiB additional graph bytes, 10% of fresh available heap budget); also require OS available memory >1 GiB for an upward probe; unknown budget vetoes speculative recovery. No duplicate output allocations unless explicitly charged.
- [ ] Seed history using consecutive real sources, never generated frames. Keep active graph generating while pending history builds if budget permits; under pressure defer or use source-only warmup rather than forcing double work. Publish only after valid initialization/warmup submission. Keep old resources alive until all GPU and downstream present reads retire. Failed allocation/submit cancels pending only, backs off >=2 s, retains active graph; device lost uses existing unavailable path.
- [ ] Green: run graph transition tests, synchronization validation and real Vulkan readbacks for output continuity, repeated up/down, memory/fault rollback, no queue-wide idle in adaptive path. Preserve fixed path and existing allocation tests.

## Task 4: Live Auto integration and coordination

**Files:** `dm_framegen.c`, `gfx_vk_present.c`, `gfx_vk_spatial.h`, `gfx_vk_capture_scale.h`, `gfx_vk_capture_session.inc`, existing pause menu and host capture/pacing tests.
**Interfaces:** applied/requested flow, reason, epoch, pending/retiring bytes, timing serial, delivered frames, source cadence; controller arbitration at real present boundary.

- [ ] Red: opt-in disabled/manual/FG off, no timestamp support, resize, handheld/docked switch, pause/resume, refresh cap, GSR2 fallback, LSFG fixed/adaptive target, competing internal scaling changes, pressure and failure injection.
- [ ] Enable governor only for Auto and explicit rollout flag after Task 3 gate. Base-game deadline protections stay independent. Prefer flow reduction only if LSFG is a material contributor; reconstruction and internal scale retain correctness vetoes. One quality recovery probe at a time; hold evidence on workload epoch changes. Do not reset pacing solely for successful scale handoff. Manual selection remains exact and ignores governor.
- [ ] Capture requested/active flow, graph lifecycle, GPU stage cost, actual submitted/accepted generated count, transition/rejection reason. Menu shows Auto and last applied scale without affecting settings ABI.
- [ ] Green: focused runner, real Vulkan readbacks, native link, signed APK; verify no changed behavior with rollout off. Document rollback via fixed Auto and flag.

## Task 5: Device acceptance and rollout

- [ ] Matrix: S25 FE Xclipse 940 custom RADV and system driver where supported; S20+ Adreno 650 Turnip. FFR 1.0.1 handheld/docked, Auto and explicit 1440p, None/GSR1/GSR2, internal scaling Off then eligible Adaptive, fixed 2x/3x/4x and adaptive target where refresh permits; compare manual scales and old fixed Auto. Include lightweight title for recovery.
- [ ] Run metadata-only captures, controlled motion scene and 30-minute soak. Record exact runtime/driver/extent/scale, source and generated accepted FPS, p50/p95/p99 active frame time, stage GPU time, graph allocation peak, memory pressure, thermal status, transition count and visual ghosting. Full snapshots separate from performance run.
- [ ] Gates: no new validation errors, corruption, crash, LMK or unbounded allocations; no transition-induced queue idle; no persistent quality oscillation; fixed modes unchanged; no regression in base FPS or frame pacing versus fixed Auto under comparable workloads; demonstrate useful generated delivery or measured GPU relief in at least one pressured case and recovery in a lighter case. Reject misleading FPS when refresh is limiting. Keep opt-in if any gate is not met.
- [ ] Update architecture and progress evidence, record per-device limitations, build signed APK at exact verified SHA. Default activation requires hardware evidence; no release/tag changes without authorization.

## Progress log

- Roadmap created from verified remote baseline. No implementation or validation claimed by this planning commit.
