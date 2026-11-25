# Universal internal resolution scaling implementation plan

Baseline: f02ceda. This first checkpoint is intentionally observation-only in the Vulkan backend. It does not enable the universal scaling option or claim a lower physical render resolution.

## Phase 0 — eligibility and timing (this checkpoint)

- Use `DM_VK_RESOLUTION_AUDIT=1` to collect aggregate attachment-group eligibility and reason flags. Candidate counts are necessary, not sufficient evidence for scaling.
- Keep existing image allocation, Vulkan synchronization, shader bundles and engine scaling unchanged.
- Use actual source-frame query identities once in the existing adaptive controller; avoid duplicate GPU timings.
- Compile and run `python -m unittest discover -s tests -p test_resolution_plan.py`.

## Phase 1 — physical images and lifetime

Checkpoint P1a: private physical variants now have logical/physical dimensions, dedicated allocation accounting (64 MiB aggregate ceiling, including deferred retirement), monotonic generation, image views and submission-fence destruction. `DM_VK_RESOLUTION_PROBE=1` records a one-shot isolated clear of a candidate color/depth group at 75%, rolling back all allocations if any fails. This is not guest rendering or a correctness/readback test. No variant becomes authoritative; copy/coherence and real raster integration remain gated.

- Introduce separate logical extents and physical image variants in `gfx_vk.h` / `gfx_vk_tex.c`; guest storage keys, offsets and sizes remain logical.
- Track image version and authoritative representation, view generation, and deferred retirement fences. Account for actual allocation requirements (not logical guest byte count). Fail closed for unsupported aliases, multisampling, compressed/linear/sparse resources and unclassified accesses.
- Validate with offscreen paired color/depth render targets, copies, partial clears, readbacks and allocation-failure injection. Never use global idle waits per scale change. Do not enable arbitrary game workloads until the next phases pass.

## Phase 2 — raster and shader semantics

- Transform render areas, viewport/scissor and clear rectangles using per-image physical extents. Retain guest-coordinate semantics in shader translation, with descriptor-local scaling metadata for integer fetches, texture queries and offsets.
- Index shader coordinate dependencies during recompilation, keep unmodified 1x modules available, and validate SPIR-V. Reject unresolved coordinate provenance and shader writes until proved safe.
- Compare identity-scale outputs and test odd dimensions, fractional factors, depth/stencil and multiple attachments.

## Phase 3 — compute and coherence

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
