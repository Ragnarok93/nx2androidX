# NVN → Vulkan / Xclipse 940 source-accuracy and compatibility audit

**Audit date:** 2026-11-19–20. **Repository:** \`Ragnarok93/nx2androidX\`.  
**Baseline:** \`android\` at \`e4d1a6176ec380cac5134bea89c5a8df986b628e\`.  
**Integration reviewed:** \`fix/ffrs-xclipse-render-stage-capture-20261119\`, beginning at \`015861910897943fe5339cb3fcb94ebd099c2405\`.  
**Game:** FINAL FANTASY RESONANCE, 0100606026F12000, 1.0.1, UE 5.6.1 / NVN.  
**Test GPU:** Xclipse 940 (SM-S731U, Android 17) with custom RADV MAGELLAN CARL, driverID 3; Vulkan 1.4.  
**Reference:** Adreno 830 / Lenovo Legion Y700; it reached the first battle without the Xclipse scene defects but its profile does **not** certify every effect.

## Important scope and standard

This is an end-to-end **source-level architectural audit of the actual branch** and the observed trace. It is **not** a claim that every Ryujinx instruction emitter, 73k shader, or Vulkan driver path has been exhaustively dynamically validated. Names and counts come from the repository and the latest three player-exported ZIPs, especially
\`nx2android-render-diagnostics-20261119-214247.zip\`; the in-repo 9.2-MB \`shader_map.tsv\` and Android-generated shader bundle cannot be independently validated against an on-device shader dump from this audit alone.

**Completion gate remains open:** the game still renders world/battle 3D incorrectly on Xclipse; UI is correct. No fix is declared without a before/after battle capture on the real hardware.

## Rendering chain actually audited

| Stage | Source locations | What was checked |
|---|---|---|
| Guest API and command recording | \`nxnative/dm/gfx/nvn/hle_nvn.c\`, \`nvn_obj.h\`, \`nvn_rec.h\`, \`nvn_dirty.c\` | Device state, command memory, submit/fence ordering, indirect and render commands, pointer lifetimes, dirty-page tracking, no-op APIs |
| Program inventory and translation | \`games/0100606026F12000/{game,android,shader_facts,shader_facts_manual,shader_pairs,shader_map}.tsv\`, \`gfx/tools/{shader_split.py,dmsh_xlate/Program.cs}\`, \`android/tools/shader_vk.py\` | IoStore shader extraction, 32-lane Maxwell warp emulation, SB alignment, integer input/output type facts, shader pairing and bundles |
| Translator semantics | \`third_party/ryujinx/src/Ryujinx.Graphics.Shader/{StructuredIr/AstOptimizer.cs,Instructions/InstEmitWarp.cs,Translation/Optimizations/*,CodeGen/Spirv/*}\` | Writable-memory propagation safeguards, side effects, subgroup ballots/shuffles, SPIR-V translation stages. Full numerical shader-output equivalence **not** established |
| Vulkan program/pipeline | \`gfx_vk_shader.c\`, \`gfx_vk_subgroup.c\`, \`gfx_vk_exec.c\`, \`gfx_vk_pipeline_cache.inc\` | Reflection, descriptor types/sets, pipeline variants, fragment/depth stages, vertex/index formats, cull/winding, viewport, pipeline key and cache identity |
| Vulkan buffers/VM/memory | \`gfx_vk_core.c\`, \`gfx_vk_dq.c\` | Zero-copy AHardwareBuffer vs copied pools, CPU/GPU visibility, deferred retirement, ring replay, compute/indirect sync, image allocation/alignment |
| Textures/depth/present | \`gfx_vk_tex.c\`, \`gfx_vk_present.c\`, \`gfx_vk_passdump_plan.h\` | Image views, aliasing, texture uploads and transfer, D24/S8 copies, depth load/store, MRT capture, WSI and overlays |
| Packaging/driver | \`front_android.c\`, Android Java activity/driver integration, game profile | Selected RADV identity, opt-in diagnostics/export, scope of Xclipse-only overrides |

## Confirmed defects / observed incompatibilities

### C1 — Xclipse RADV returns the wrong D24S8 transfer format (driver-side behavior)

The manually armed 8x8 control image cleared D24S8 depth to **0.625**. At present **2502**, the GPU-completed image-to-buffer raw output was **64/64 \`0x3f200000\`** (IEEE float32 0.625) rather than the Vulkan-defined packed D24 value in the low 24 bits (~\`0x009fffff\`). This is a concrete copy-representation mismatch, **not evidence that the numeric clear failed**. Vulkan's Copy Commands depth/stencil aspect transfer rules require packed 24-bit UNORM.

\`0158619108\` corrected the previous failing depth decoder test without modifying \`ds_texels\`' public argument contract. The new FFRS Xclipse profile bypasses an *Adreno LRZ* depth texel copy/writeback workaround and repairs the nonconforming **CPU-facing** D24 \`ReadTexels\` value only. The GPU's D24 rendering and sampled depth behavior are *not* repaired or proven correct by that conversion.

**Requirement:** an isolated driver reproducer comparing D24 clear, depth-only triangle raster, image-to-buffer, sampling, and stencil readback under RADV vs Samsung vs Adreno. Report to the Xclipse driver if valid Vulkan commands fail; record driver SHA and exact validation output.

### C2 — Guest texture storage aliasing is not fully reconciled

\`gfx_vk_tex.c\` creates distinct \`VkImage\` objects for some overlapping guest pool layouts (different dimensions/formats/flags). \`alias_check\` logs stale reads when one image is sampled after another has rendered into overlapping guest storage, but no general GPU-to-GPU, layout- and format-aware reconciliation is performed. The failing capture logged **15 stale alias reads**, including 512x512 textures with mip chains. A barrier between unrelated Vulkan images cannot propagate content automatically.

**Requirement:** capture exact producing image, storage byte range, level/layer, timestamp and consuming shader/descriptor; prove affected pixels before implementing conversions. Handle incompatible layouts and compressed formats by legal intermediate transfers, avoid indiscriminate full-image copies. Tests must verify overlapping ranges, partial mips/layers and lifetime.

### C3 — Small-image suballocator can return illegal Vulkan binding offsets

At audit baseline, \`gfx_vk_core.c::vk_image_memory\` chooses a size class (4KiB, 6KiB, 8KiB, 12KiB, ...) but computes \`memoryOffset\` as \`slotIndex * class_size\`, without rounding the stride for the image's actual \`VkMemoryRequirements.alignment\`. A 6KiB-class image with required 4KiB alignment receives a **6,144-byte offset in slot 1**, which is not divisible by 4,096, contrary to \`vkBindImageMemory\` requirements.

**Change in this audit:** aligned slot stride, block reuse segregated by stride as well as type/class, and a runnable host C test checking each class, allowed alignment and all 64 slot positions. This is a source-level correctness correction, **not** proof this exact allocation occurred in the uploaded battle.

### C4 — Memory-pool flag mutation under a reader lock

At audit baseline, \`gfx_vk_core.c::vk_pool_set_flags\` writes \`g_pool[i].flags\` while holding \`pthread_rwlock_rdlock(&g_pool_mu)\`. Multiple readers can coexist, so this is not exclusive against concurrent CPU/GPU pool lookups. Corrected to \`pthread_rwlock_wrlock\`. This is a real synchronization bug, not linked to a specific captured draw.

### C5 — Some NVN APIs are intentional no-ops instead of full semantics

\`hle_nvn.c\` registers \`nvnCommandBufferWaitSync\` and \`nvnCommandBufferSetDepthBounds\` in \`g_nops\`. The game statistics show both are invoked (at least once in early frames). In particular, a GPU-side command-buffer wait is not universally equivalent to no operation where a sync could originate outside the same serial Vulkan queue. The code also ignores custom sample locations and supports incomplete true-sparse virtual pool mappings.

**Classification:** a **confirmed implementation gap**, *not* a demonstrated cause of this game's missing actors. Instrument producer, acquire sync and queue identity before modifying the scheduling model.

## What the failing trace positively rules out

1. The imported RADV is positively identified at runtime (RADV MAGELLAN CARL, Vulkan 1.4, driver ID 3). The system-driver fallback is **not** what the captured run used.
2. The Xclipse run reports subgroup size **64** (supported range 32–64), stages \`0x3fff\`, supported operations \`0x6ff\`; the shader translator defaults to a 128-lane *upper-bound translation model* with 32-lane Maxwell emulation. This deserves shader-specific differential tests but it is **not yet proof** of a subgroup bug. Previous force-32 trials did not change the rendering.
3. The synchronous-pipeline mode is active in the newest run: by present 3000 and later the trace reports zero missing program, missing target or missing pipeline draw skips. These omissions are not the persistent failure.
4. Six MRT color attachments in scene G-buffer passes contain real 3D data; it is not a total failure to submit scene work. UI and HUD are separately rendered and appear correct in the video.
5. The depth-only prepass recorded **92 draws**, with actual pipeline depth test/write enabled, compare ALWAYS, rasterizer discard off and render resolution **1384×780**. Raw depth readback remains all zero; **because C1 is confirmed**, this alone does not distinguish a broken rasterizer/vertex input from a depth-attachment or readback defect.
6. No new missing shader-bundle or pipeline-creation error was recorded in the failing run, despite more than a million draw submissions. Counts do **not** verify successful primitive assembly, rasterization or GPU-produced indirect counts.

## Open high-risk semantic areas (not proven bugs)

| Priority | Mechanism | Required discriminatory test |
|---|---|---|
| P0 | Geometry input / compute culling / GPU-written indirect records incorrect on Xclipse | Fence-completed GPU copies of the exact count, indirect-record, skinning/vertex and index ranges consumed by the *failing battle draw*; compare with guest bytes and matching Adreno frame |
| P0 | Depth prepass rasterization, D24S8 metadata, depth load/store or stencil | Same 92-draw VS/vertex stream and depth-cull state, before/after pass snapshots, isolated depth triangle reproducer (C1 makes old raw depth screenshots insufficient) |
| P1 | Texture storage aliases are sampled stale | Match 15 alias warnings to specific G-buffer-consuming descriptors and producer passes; implement only proven storage migrations |
| P1 | Shader translation / optimizer / warp behavior | CPU reference of radix sort, scan, culling and vertex transforms; \`spirv-val\` plus per-shader output compares; especially FFRS' earlier radix sort fixes and memory-ordering transforms |
| P1 | NVN wait/event barriers, copied and zero-copy guest memory | Queue/producer timeline capture, GPU-visible indirect argument copy, AHardwareBuffer cache visibility, hazard test |
| P2 | Vulkan pipeline variant/cache identity and descriptor memo | Device UUID/driver UUID header already checked; compare bound values/offsets at failing draw, not just pipeline count |
| P2 | Vulkan replay scheduling and fragment/depth ordering | Compare pass boundaries and timing against native NVN/Adreno without relying on FPS or frame generation |

## Source-accuracy notes

- The shader translator's \`AstOptimizer.cs\` explicitly protects loads of writable local/shared/storage/global memory from unsafe propagation, and protects atomics/calls from being dropped as unused expressions. These safeguards were introduced for observed GPU radix-sort and atomic defects; they do not constitute a proof that every SPIR-V optimizer transformation is accurate.
- \`shader_vk.py\` builds from the game's Switch shaders with \`sb_align=256\` and a subgroup translation model of 128; actual RADV subgroup size is 64. This *may* be safe because \`InstEmitWarp.cs\` uses 32-lane masks within host subgroups, but it needs direct output tests for the exact failing kernels. A runtime subgroup-size toggle alone is not a valid complete test when shaders were translated ahead of time.
- Device capability detection checks Vulkan version, optional extensions, memory features and driver ID; the logged driver is correctly identified. Do not infer all required Vulkan features are correct in implementation from their advertised flags.
- \`gfx_vk_dq.c\` records and replays normal Vulkan draws, barriers, copies, indirect and render-pass commands. Submission counts do not verify the GPU sees the same indirect argument bytes after compute.
- Presentation/UI correctness confirms the present and overlay path works for these frames. It does not validate the underlying 3D scene targets.

## Required executable regression and device matrix

1. **Host/CI**: compilation and real execution of image-allocation stride/alignment fixture; old decoder and D24 conversion fixtures; indirect and compute fixtures; shader validation of translated programs; render-pass depth/stencil and aliasing fixtures; no new driver-side validation errors.
2. **Xclipse hardware**: prepass triangle with depth clear 0.625, compare/write variants, 24-bit + 32F depth readback, sample-depth quad, stencil copy; record native and custom driver. Real battle frame with one indexed draw and exact VS, GS, attributes, bounds, base vertex, viewport/cull, GPU-consumed indirect arguments.
3. **Adreno hardware**: same fixtures, Snapdragon known-good first battle and complete effects; verify no regressions in depth LRZ, texture aliasing, pool buffer and memory behavior.
4. **Endurance**: title → opening → traversal → first battle → effects → menus → transitions, 30-FPS correctness baseline, separate p50/p95/p99 CPU/GPU frame times, thermal and physical-memory growth at 8 GB. Frame generation off while verifying correctness.
5. **Release**: a self-contained game APK signed with an appropriate key, upgraded where compatible; the CI builder-only ephemeral APK does **not** satisfy this gate.

## Decision / next engineering move

Do **not** add more global driver toggles. First produce the minimal depth raster reproducer (to separate D24 transfer from depth attachment correctness) and the GPU-visible indirect/vertex capture at a confirmed faulty draw. The precise **first failing scene stage** remains unknown. The highest-confidence source fixes from this audit are C3 and C4; other items require on-device evidence before production changes.

**References:** Vulkan \`vkBindImageMemory\` alignment valid-usage rules (VUID 01046) and Vulkan Copy Commands' depth/stencil aspect buffer representation. Both are public Khronos specifications; all project-specific details above are taken from the repository and attached diagnostic ZIPs.
