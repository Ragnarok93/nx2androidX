# Universal internal resolution scaling implementation plan

Baseline: f02ceda. This first checkpoint is intentionally observation-only in the Vulkan backend. It does not enable the universal scaling option or claim a lower physical render resolution.

## Performance and memory acceptance requirements

- At 100% with diagnostics disabled, preserve original Vulkan image allocation, shader modules, descriptor and pipeline keys, draw/dispatch recording, transfers and synchronization. Select the native path once per frame/transaction where possible; no per-image scaling metadata allocation, no per-draw shader scaling work, no image variants and no periodic conversion. A predictable dispatch check is not a measured guarantee of identical CPU cost.
- For adaptive mode at 100%, reuse existing GPU timestamps; controller work must be bounded and evaluated separately from rendering. Do not add GPU timestamps solely to measure the scaling controller when existing queries suffice.
- Below 100%, prefer a single authoritative physical allocation for eligible GPU-only images. Keep logical metadata independent of physical allocation; materialize a logical-size image only at an unavoidable guest-visible boundary, then release unneeded copies after their final GPU consumer. Never automatically allocate a native-sized twin for every render target or copy every target every frame.
- On scale transitions, charge both old and new live allocations until actual fence retirement, with a bounded transient peak and failure rollback. Avoid global idle waits, per-frame image recreation and permanent duplicate histories. If correctness requires frequent conversion or the working set does not decrease, pin that resource/group to native instead.
- Test native vs fixed 100% vs adaptive 100% (diagnostics off), then 75% and 50% on identical scenes: actual Vulkan allocated bytes including temporary/retired images, render GPU time, CPU replay p50/p95, frame pacing, and conversion counts. Require no material 100% regression and lower steady-state memory/work for eligible scaled workloads; report exceptions and reconstruction costs separately.

## Phase 0 — eligibility and timing (this checkpoint)

- Use `DM_VK_RESOLUTION_AUDIT=1` to collect aggregate attachment-group eligibility and reason flags. Candidate counts are necessary, not sufficient evidence for scaling.
- Keep existing image allocation, Vulkan synchronization, shader bundles and engine scaling unchanged.
- Use actual source-frame query identities once in the existing adaptive controller; avoid duplicate GPU timings.
- Compile and run `python -m unittest discover -s tests -p test_resolution_plan.py`.

## Phase 1 — physical images and lifetime

Checkpoint P1a: private physical variants now have logical/physical dimensions, dedicated allocation accounting (64 MiB aggregate ceiling, including deferred retirement), monotonic generation, image views and submission-fence destruction. `DM_VK_RESOLUTION_PROBE=1` records a one-shot isolated clear of a candidate color/depth group at 75%, rolling back all allocations if any fails. This is not guest rendering or a correctness/readback test. No variant becomes authoritative; copy/coherence and real raster integration remain gated.

Checkpoint P1b: private attachment groups support transactional migration, one published variant per slot, no-op same-extent requests, format-feature checked nearest blits (separate depth/stencil regions), and fence-retired old variants. The opt-in probe now exercises 75% -> same extent -> 50% where supported, without touching guest images. Allocation or format failure rolls back staged resources. Probe logs report record-time results only, not GPU completion or content correctness. Native-sized materialization, guest identity/coherence and raster/shader integration remain unimplemented. The 64 MiB budget includes both generations during migration.

- Introduce separate logical extents and physical image variants in `gfx_vk.h` / `gfx_vk_tex.c`; guest storage keys, offsets and sizes remain logical.
- Track image version and authoritative representation, view generation, and deferred retirement fences. Account for actual allocation requirements (not logical guest byte count). Fail closed for unsupported aliases, multisampling, compressed/linear/sparse resources and unclassified accesses.
- Validate with offscreen paired color/depth render targets, copies, partial clears, readbacks and allocation-failure injection. Never use global idle waits per scale change. Do not enable arbitrary game workloads until the next phases pass.

## Phase 2 — raster and shader semantics

Checkpoint P2a: the vendored Ryujinx translator already emits fragment-coordinate division, integer texel-fetch correction and texture-size unscaling through its 73-slot SupportBuffer. Reuse this ABI rather than introduce a new shader pass. Shader recompilation appends optional RSCL v1 metadata to DMSV reflection (original 20-byte records and C1 unchanged), preserving translator descriptor order and usage flags. Old bundles have no metadata and must remain native for unproven bindings. Per-stage support-buffer initialization and bounded slot writes are implemented; vertex stage-local buffers use fragment scale count zero. Array, storage, multisampled, unsupported and unknown bindings must remain unscaled. Actual per-draw support-buffer binding, extent rounding validation, and raster integration remain gated. Native rendering continues to use its immutable all-ones buffer; no new per-draw work at 1x.


Checkpoint P2b: exact uniform extent ratios, logical scissor clipping and flipped viewport scaling are implemented in a tested host-independent helper. Nonuniform rounded dimensions fail closed because the existing shader ABI is scalar. The opt-in private probe now records a real dynamic-rendering pass with a mapped partial color/depth/stencil clear, without disturbing guest state; this is not a gameplay draw or GPU readback test. Added an explicit per-stage support-buffer builder: native input returns the original buffer; nonnative input validates binding metadata and uses submission-owned uniform storage. Actual guest draw routing and descriptor memoization integration are still gated; no new calls on the 1x draw path.

- Transform render areas, viewport/scissor and clear rectangles using per-image physical extents. Retain guest-coordinate semantics in shader translation, with descriptor-local scaling metadata for integer fetches, texture queries and offsets.
- Index shader coordinate dependencies during recompilation, keep unmodified 1x modules available, and validate SPIR-V. Reject unresolved coordinate provenance and shader writes until proved safe.
- Compare identity-scale outputs and test odd dimensions, fractional factors, depth/stencil and multiple attachments.

## Phase 3 — compute and coherence

Checkpoint P3a: optional ownership/consumer graph is now wired to real cache owners, render attachment groups, alias discovery (including overlaps beyond the legacy eight-entry list), sampled/storage descriptors (including view-cache hits), raw texture lookups, CPU readback, and presentation. It propagates sticky restrictions across connected attachment groups; expired owners are retired from pointer lookup and pointer reuse starts a new lifetime. A bounded 4096-lifetime capacity fails closed on exhaustion; no graph memory allocated at normal 1x. The audit disables descriptor memo hits only when explicitly enabled, so shader-specific consumer metadata is observed. `DM_VK_RESOLUTION_AUDIT=1` emits ownership summaries every 300 presents. This is observation, not a complete proof of future consumer behavior or production ownership replacement. Compute, raw access, presentation, unclassified metadata, format/special views and host-visible resources remain pinned until their scaled paths exist. Guest memory access via unobserved APIs and aliases created before tracking are not proven safe.


- Classify dispatch geometry, storage-image accesses, mip chains, copies, resolves, guest readback and alias groups. Pin or reconcile incompatible connected resource groups at logical resolution. Do not infer workgroup coverage from texture extent alone.
- Validate representative UE deferred passes and Xclipse D24S8 behavior, including alias and depth-pyramid cases. Preserve the Adreno path.

## Phase 4 — adaptive integration

- Only advertise the universal backend when workload coverage is proven. Preserve the verified game-engine path as an alternative, never multiply the two scales.
- Introduce requested vs committed scale with frame-boundary transactions, bounded allocation and rollback. Feed the existing controller with distinct source GPU samples and separate reconstruction costs; avoid downscaling CPU-bound frames.
- Expose manual scale and adaptive minimum/maximum, with an independent budget for Unlocked. Keep 1x as fallback and report the actual committed scale, not the requested value.

## Phase 5 — reconstruction and device validation

- Propagate actual physical source dimensions and logical crop to FSR/GSR. Validate GSR2 depth/motion/jitter coordinate conventions and invalidate incompatible temporal history on scale changes; preserve reconstruction before LSFG.
- Verify 1x correctness, fixed and adaptive scale, source-frame timing, memory high-water mark, pause/resume, saved settings and upgrade compatibility on Xclipse and Adreno. No automatic enablement without these results.

Architectural references: Eden-xclipse `dce1290` texture-cache and shader rescaling mechanisms; implementation here is independent and scoped to the NVN/Vulkan backend.
