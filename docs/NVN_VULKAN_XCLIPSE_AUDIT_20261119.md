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

## P0 follow-up on the existing draft

Repository baseline `android` resolved to `e4d1a6176ec380cac5134bea89c5a8df986b628e`.
PR #4 remained open and draft on integration HEAD `d0430843f26767b963ece309aecba8ba124389c7`.
This follow-up starts from that head and retains its allocator alignment, pool
locking, FFRS Xclipse D24 CPU conversion, and scoped LRZ policy.

### Rechecked player evidence

| Archive | GPU-completed evidence | Diagnostic limit |
|---|---|---|
| `194209`, present 3665 | Three scene passes each supplied six MRTs. Saved D24 and S8 planes for passes 3–5 were all zero. | No early depth-only capture and no independent transfer control. |
| `214247`, present 2502 | Pass 1 recorded 92 depth-only draws. The phase trace shows depth test and writes enabled, compare ALWAYS, rasterizer discard disabled, and vertex shader `406bc59dec6d556d` on the first recorded draw. The trace and archive contain populated color targets in later passes. | The 24-target budget captured scene depth/stencil from passes 3 and 4. It did not capture pass 5 depth/stencil. All captured scene depth planes were zero, a byte pattern compatible with either D24 or float representation of zero. |
| `214247`, isolated pass -200 | All 64 words were `0x3f200000`, float32 0.625. The expected D24 word is approximately `0x009fffff` in the low 24 bits. | This test uses a transfer clear and a depth copy. Attachment rasterization, preservation, and sampling require independent tests. |
| `214247`, indexed indirect trace | 96 CPU/shadow diagnostic lines across two presents. The first at present 2502, pass 5, vertex shader `1a970a5d8e09b226`, showed 1188 indices and one instance in guest memory with no copied-pool shadow. | The archive contains no fence-completed indirect buffers. These counts cannot establish GPU-visible geometry. |

Both ZIPs passed their CRC checks. All 48 manifest-indexed raw files matched
the recorded lengths and FNV-1a hashes, and all ZIP entries were nonempty. The
39.35-second video shows broken battle world/characters with readable HUD
and dialogue. The native logs identify custom `libvulkan.radeon.so`, RADV
MAGELLAN CARL, driver ID 3, Vulkan 1.4.368, and driver version
`0x06863063`. They contain no binary digest or source revision sufficient
to bind the imported driver to a Mesa commit. Treat `cc87fdf` as a candidate
source attribution until verified against the binary.

The newer archive contains 15 stale-image warnings. A representative pair
shares the pool-relative start 593920 but has overlapping ranges
[593920, 659456) and [593920, 618496) with different formats and dimensions.
The warning contains neither a consumer draw nor mip/layer write provenance.
This proves overlap detection reached its warning path. Linking those reads
to the missing actors requires a pass-correlated producer and consumer trace.

### NVN and Vulkan operation coverage

These classifications describe the reviewed path. Device-level accuracy
requires the scene comparison for each operation used by the affected draws.

| Operation | Source to Vulkan path | Coverage |
|---|---|---|
| Record, end and call command handles | `hle_nvn.c` segmented records and `OP_CALL` → Vulkan replay; asynchronous submissions flatten a copy before replay | Implemented, device lifetime coverage unverified |
| Queue submit and CPU sync waits | Ordered replay → submission fence / Linux completion; `SyncWait` checks the captured timeline value | Implemented, cross-producer visibility unverified |
| Queue and command-buffer GPU waits | Queue wait returns success even for a future signal; command-buffer wait has no replay opcode | Unimplemented for arbitrary future or cross-queue producers |
| Record command fence and signal event | `OP_FENCE_SYNC` and `OP_SIGNAL_EVENT` → replay and submission completion | Partial, NVN condition/location/flags require verification |
| Wait event | Registered no-op | Unimplemented |
| Memory barriers | Barrier bits recorded; backend may close a pass or use a conservative Vulkan memory barrier | Partial, exact visibility for each guest hazard unverified |
| Set depth bounds | Registered no-op | Unimplemented. Early call count exists, enable and bounds require startup trace. |
| Depth test/write, stencil, bias and clamp | Recorded state → graphics pipeline and dynamic state; depth/stencil aspects selected from target | Implemented, device raster and sample accuracy unverified |
| Viewport, depth mode, front face and scissor | NVN origin and depth mode drive viewport conversion, clip-control variant and Vulkan winding | Implemented, exact target draw position and clipping unverified |
| Program stage bindings | Program keys saved from live shader metadata → DMSV lookup → modules, specialization and pipeline stages | Implemented, individual translation equivalence unverified |
| Vertex and index binding | Reflection and NVN stream metadata → pipeline attributes, rate/divisor and pool buffers | Implemented with explicit fallback paths, failing-draw input bytes unverified |
| Draw, indexed draw, indirect count | Replay chooses Vulkan direct or indirect commands; `mdi_group` selects a real count buffer only when supported and usable | Partial, fallback draws individual records and does not enforce a GPU count |
| Dispatch and indirect dispatch | Compute pipeline/set, ordered execution and GPU buffer arguments, including multi-chunk scratch reconstruction | Executable host fixture covers packing and lifetime, hardware visibility unverified |
| Transform feedback | Vulkan transform feedback and indirect byte count where supported | Partial, unsupported topology and indirect combinations reported |
| Uniform/storage/image descriptors | Reflection → `stage_set`, dynamic uniform offsets, memoization and typed views | Implemented, actual failing draw's effective ranges and stage bindings unverified |
| Render targets and clears | Track aspects, color slots, views and subresources → render pass/dynamic rendering, folded clear and load/store policy | Implemented, NVN pass equivalence still unverified |
| Texture copy and overlapping guest storage | Texture staging, block layouts and aspect copies; compatible aliases may share storage, other overlaps warn | Partial, independent overlapping Vulkan images lack full reconciliation |
| Memory pools and CPU/GPU visibility | AHardwareBuffer import or copied chunks, dirty tracking and deferred UBO shadow/landing | Partial, sparse mapping and alias cases remain open |
| Present and UI composition | Render target → Vulkan present queue and Android overlays | Observed for these captures, earlier scene output remains defective |
| Patch size, custom sample locations, scratch state and debug annotations | Selected operations ignored or used for tracing | Unimplemented or partial depending on operation |

`StructuredProgram.cs` initially builds most expression sources from
`AstOperand` values and emits vector extract against a temporary. Consequently,
the top-level checks in `AstOptimizer` establish a source-level concern
when handed a nested memory operation, but a reachable nested counterexample
in the FFRS translation chain has yet to be demonstrated. Existing writable
load and atomic guards remain in place. The only hardware subgroup evidence
is wave64 with a 32–64 supported range. Translation's model of 128 partitions
into Maxwell warps in the emitter. Ballots, shuffles, divergent lanes and
radix histogram/scan/scatter outputs require device comparison. The scalar
fallback cannot independently establish cross-lane equivalence.

### What the new diagnostics measure

- With **Trace details** enabled when the player arms **Capture next frame**,
  Vulkan copies indexed indirect argument records and the count word from
  their actual draw allocations. It records pool identity/generation, guest
  and Vulkan buffer offsets, draw and submission ordinals, preceding dispatch
  count, shader keys and pipeline vertex format/stride/divisor information.
  Index and vertex rows sample binding prefixes. The first element consumed
  may lie outside a sampled prefix.
- The copies run at the natural pass end before attachment writeback or the
  next producer. Passes detected with shader memory writes or transform
  feedback are excluded. Coverage is up to four draws per pass, 96 ranges
  and 24 KiB of logical output, charged at actual allocation size against
  the same readback budget as scene images. The report is published after
  the submission fence and host-cache invalidation as
  `metadata/gpu_draw_inputs.tsv`. An absent row does not imply a zero draw.
- A pass-end copy can observe correct producer bytes even if a faulty
  producer-to-draw dependency let the draw see stale bytes earlier. The
  preceding dispatch count is a correlation point, not a unique last-writer
  identity. Resolve that ambiguity with matching traces, exact descriptor
  ranges, vertex output capture and a targeted hazard test if needed.
- Runtime startup records the selected physical device's driver ID, name,
  driverInfo, version, UUID and loader identity. Hash the imported driver
  separately on the device to tie it to a specific binary.
- `DM_NVN_SEQ=4` at process startup logs the ignored depth-bounds arguments,
  command wait sync identity and observed submit/fence context. These APIs
  retain their previous no-op behavior. Startup tracing matters because the
  existing logs record some calls in the first frames.

### Independent depth discriminator

The `devtools/vk_depth_probe` executable has no game or recompiler dependency.
It uses a separate transfer clear and render-pass clear, two depth/stencil
passes with STORE/LOAD, indexed triangles, reversed-Z GREATER, early and
late fragment-test variants, color readback, stencil copy and depth sampling
through a separate compute pipeline. The D32F control uses the depth aspect
of D32FS8 where supported.

Linux llvmpipe 25.0.7 ran all four cases with 64/64 passing pixels for
color, clear and drawn depth/stencil transfers, and sampled depth. Vulkan
synchronization validation produced no diagnostics on that run. All probe
SPIR-V passed `spirv-val --target-env vulkan1.2`. The probe also compiled
for Android ARM64 API 31. The device comparison remains open.

The local host suite ran 633 tests with 12 skipped and no failures. The
production input-capture fixture checks range rejection, slab offsets, budget
accounting, exclusion of writable passes, delayed Vulkan-copy execution,
cache invalidation and report bytes. The production Java archive fixture
checks export of completed GPU samples and omission of pending/stale files.
The Android NDK r27d FFRS host built and linked all 200 source units into
`dmhost`, `libdmhost.so` and `libdmplay.so`. These are build and host tests.

| Result on Xclipse | What it would establish |
|---|---|
| Color and sampling pass, D24 copy fails, D32F control passes | Isolated transfer-format defect within the tested path |
| Color fails or differs by early/late variant | Rasterization, depth/stencil test or LOAD/STORE behavior requires isolation before attributing the result to transfer |
| Color passes but sampled depth fails | Attachment writes reach the tests. Inspect depth view/sample visibility and representation. |
| Probe passes but the scene fails | Inspect recorded/gpu-visible scene inputs and rendering state, or a more specific driver feature interaction. |

### Performance and acceptance status

The only quantified baseline is the prior Xclipse trace, with replay CPU
7.30 ms/frame, descriptors 1.95 ms, pipeline keys 1.37 ms, submission
0.78 ms and GPU busy 12.40 ms. Descriptors and pipeline keys account for about
45% of replay CPU. This is a diagnostic run and does not establish current
performance or an optimization gain. Paired p50/p95/p99, thermal and 8 GB
endurance measurements require the same scene and a build whose renders
match. P2 and P3 remain gated behind P0 and P1.

Open evidence includes the matching imported-RADV, Samsung and Adreno runs,
the first differing scene stage, underlying gameplay root cause, exact
driver source for the transfer discrepancy, end-to-end shader regression
vectors, stencil resolve, captured clip positions and exact uniform/storage
bindings. No ADB-connected hardware, FFRS game files or standalone signing
identity were available for this iteration. Validation and signing of a
self-contained game export are separate acceptance gates from builder CI.

References: [Vulkan depth/stencil copy representations](https://docs.vulkan.org/spec/latest/chapters/copies.html)
and [JimVulkan driver sources](https://github.com/JimVulkan/mesa-unified-drivers).

### D24 capability and driver provenance

The older 8×8 in-game control used `vkCreateImage` directly. The original
archive therefore establishes what bytes the command returned, while
the device's advertised support for that format and usage is unknown.
The public [RADV Xclipse v1.9.0 source at `cc87fdf`](https://github.com/JimVulkan/radv-xclipse/blob/cc87fdf71267dd0acc3b470920a6566a1a46f0be/src/amd/vulkan/radv_formats.c#L249)
excludes `D24_UNORM_S8_UINT` and `X8_D24_UNORM_PACK32` in
`radv_is_zs_format_supported`. The tested binary's hash is still needed.

The in-game control now logs the selected device's format features and
queries the exact optimal-tiled attachment and transfer usage before
creating its scratch image. An unsupported result means an identical
request would be invalid for that driver. A supported result with
float32 output would isolate a driver transfer discrepancy. On a binary
that claims support, the source path to trace is
`radv_CmdCopyImageToBuffer2` → `compute_copy_image_to_memory` →
`radv_meta_image_to_buffer`, including source-aspect and destination
buffer-view format selection.

The game path also needs attention if the query rejects D24S8. The NVN
`DEPTH24_STENCIL8` mapping in `vk_fmt` selects D24S8 directly, whereas
depth-only D24 checks support and can select D32F. A future combined
depth/stencil fallback requires proof of the exact installed driver
capabilities and tests for guest precision, stencil, sampled views and
pack/unpack. The existing CPU conversion and LRZ policies remain scoped.

### Gameplay D24S8 fallback on the captured RADV device

The Nov 20 capture identifies `Mesa 27.0.0-devel (git-cc87fdf712)`.
At present 3346, the device reported zero optimal-tiling features for
`VK_FORMAT_D24_UNORM_S8_UINT` and returned
`VK_ERROR_FORMAT_NOT_SUPPORTED` for the attachment/transfer query. The
game's first depth-only pass still bound that Vulkan format for 92 draws.
Depth readbacks for the prepass and subsequent G-buffer passes were zero.
This establishes a Vulkan format-selection error in the renderer.

NVN D24S8 now selects a supported sampled/transfer-capable depth/stencil
backing. Native D24S8 gets first preference. The fallback uses D32F+S8
with both aspects present, including for GPU rendering and sampled views.
CPU uploads unpack the guest's 24-bit UNORM depth and eight-bit stencil
into separate Vulkan transfer planes. CPU readback repacks them into the
original guest layout. Same-size copies between fallback images transfer
both aspects. Direct copies between a guest GPU buffer and a fallback
image require a GPU pack/unpack path and are reported as unsupported.

The new capture contained 60 completed GPU buffer samples. All 20 sampled
indexed indirect commands had nonzero index and instance counts. Its ZIP ended
mid-entry; the native log and completed entries were recovered. The log
shows a late 600-present window averaging 20.40 ms replay CPU, including
15.87 ms in pipeline keys/creation, and 13.82 ms GPU busy. This is a
baseline from the failing build. Correctness and performance of D32F+S8
still need a matched first-battle run on the device.

### Follow-up Xclipse capture at present 5493

The `023353` archive passed ZIP integrity checks. The selected imported
RADV driver again reported Mesa commit `cc87fdf712`. The first geometry
depth pass used Vulkan format 130 for 47 draws. Its D32F sample contains
1,075,974 values in (0, 1] out of 1,079,520 texels. The player confirmed
substantial visual improvement and reported some missing textures and effects.

The log contains 76 overlapping-storage warnings, including BC3 over
B10G11R11 render targets and unequal extents even for same-format pairs.
Those overlaps may be allocator reuse. The warning alone cannot identify
the shader's expected bytes. A host reproduction found an independent
stale-texture path: a new NVN serial can resolve to the existing cached
image over identical BC storage but bypass its dirty-page check. Sampled
compressed images initialized from the CPU now check the existing tracked
range across serial replacement as long as the GPU has never written them.
The executable texture cache regression reproduces the old stale bytes
and checks both the refresh and preservation of GPU-owned images.

The app began with zero binary cache bytes and zero recorded pipelines.
After frame generation was enabled, the window ending at frame 11,400
reported frame p95 96.73 ms and p99 308.66 ms, NVN submit/present p95
92.02 ms and GPU p95 17.80 ms. Pipeline processing averaged 9.23 ms
of 12.72 ms replay CPU in the corresponding 600-present window. The
capture itself preceded frame generation. These figures identify a
stutter window but do not quantify a native-only performance improvement.

The standalone binary pipeline cache now lives in the private files
directory outside the replaceable game pack. Existing cache data retains
device and driver validation on load. Measure cold and warm launches of
the same scene with frame generation off to determine the effect.

### Warm-cache captures at presents 1193 and 1826

Both archives passed integrity checks. The 120501 capture was taken with
frame generation off. The run loaded 4,106,272 cache bytes and prewarmed
632 graphics pipelines. There were no additional graphics pipeline builds
between presents 600 and 2400. The frame-time p99 ending at frames 900
and 1200 was 185.23 and 137.32 ms. Graphics pipeline processing averaged
0.26 ms per frame for presents 601–1200. Those hitches cannot all be
attributed to new graphics pipelines. Compute prewarm declined the
2,934-shader archive and several integer-view compute variants, including
the FFRS radix sort variants, were created on demand during the interval.

The scoped Xclipse profile now remembers up to 96 successful compute
keys, including the exact signed and unsigned view masks, and prewarms
them on the existing background compute worker on subsequent runs. First
use still builds synchronously if a key is absent. Slow on-demand compute
builds produce a bounded timing line. A cold run seeds the history, and
the next launch can verify the new prewarm count and its effect. Graphics
pipeline accounting now counts each build once.

Both exports spent all 24 raw attachment slots on early depth and MRT
passes. The FG-off trace lists the layered fog and subsequent composition
passes, but the archive did not contain their image outputs. The
Diagnostics menu now offers **Capture effects / shadows**. It captures
distinct depth-only targets and selects smaller dual-output effects passes
and half-resolution or larger single-output passes after the first MRT.
The same 24-image and 128 MiB bounds apply. Manifest rows include the
submission number. Opt-in alias-consumer lines give the actual shader
binding and overlapping guest ranges for the selected frame.

The FG-off run contains 19 potential aliases, including an 87x49
R16G16_FLOAT consumer with no CPU upload over the exact range of an
87x49 B10G11R11 attachment. This merits a producer/consumer comparison.
Other warnings involve partial overlaps or CPU-uploaded textures and
cannot establish which contents the game expected. No cross-format copy
is justified by those records alone. The shared descriptor view cache
now distinguishes sampled resources from float storage images so prior
sampling cannot bypass GPU-write tracking at a subsequent storage bind.

The reference profile reports that the first-battle background flames
are absent on Adreno as well, while haze, fog and the fire lighting are
present. That observation does not identify the opening flames described
by the player. A matched capture of that location is still needed.

### Effects capture at present 14905 and compute-history follow-up

The FG-off warm run loaded 3,954,896 pipeline-cache bytes and prewarmed
96 of 96 compute variants in 258 ms. The windows ending at frames 900
and 1200 reported 78.79 and 59.66 ms p99, versus 185.23 and 137.32 ms
in the earlier FG-off capture. These are different traversals, so the
figures do not isolate the prewarm effect. At present 17414, a first use
of shader `87a863fa726e88f1` with masks `0x100/0x420` still cost
78.42 ms. A full history previously stopped admitting observations. It
now admits slow misses after reaching 96 records, preserving the first
16 startup keys and replacing the oldest remaining key. This preserves
the existing cache format and memory bound.

The effects ZIP ends partway through a raw attachment and lacks its
central directory. The completed ZIP entries supplied two logs and 14
complete raw attachments. CRC and logged GPU hashes verified all 14.
Recovered outputs include nonempty scene and character depth, HDR
lighting, and finite values for the first attachment in the 32x32
fog-volume passes. Pass 13's first output was zero and passes 14–16
were nonzero. The second output was outside the capture selection, and
these values alone cannot establish the correct fog or flame output.
The four recorded sampled overlaps were CPU-uploaded compressed
textures over differently formatted attachment ranges. The evidence
does not justify copying those attachment bytes into the textures.

The partial archive also contained a stale float sidecar for slot 07,
which had changed from depth to HDR color. The native capture now
removes stale sidecars and publishes the manifest after readback files
are closed. The exporter prioritizes metadata, logs and exact readbacks
ahead of previews and ignores files older than the armed capture.

### Follow-up after the rendering improvement

The descriptor allocator now snapshots the descriptor counts while the layout registry
mutex owns them. A re-registration of a deduplicated Vulkan layout handle can occur
during allocation. The executable regression performs that replacement from the
mock Vulkan allocation call and checks the actual pool accounting.

For sampled combined depth/stencil images, an explicit NVN depth/stencil view mode
takes precedence over the texture's default. The same selection drives the Vulkan
view aspect and the integer shader variant. The executable view test verifies
explicit depth over a stencil default, explicit stencil over a depth default, and
the returned range on a cached view.

In the standalone game's Diagnostics tab, **Capture graphics** records one gameplay
frame. The ZIP includes the resolved subresource ranges for MRTs, depth and stencil
attachments, sampled and storage image bindings, texture transfers and clears, and
the presented game image. It includes driver logs, eligible GPU draw-input samples,
D24S8 and D32F transfer controls, and a packed, fence-completed tile dump with hashes.
For attachment, compute image, and texture-clear outputs, the capture samples up to
16-by-16 texels per eligible layer. The catalog identifies multisampled, discarded,
unresolved and budget-limited rows. Sampled bindings, transfer requests, and the
presentation source are also tracked by identity and subresource. The swapchain
overlay and full-resolution pixels of every surface are outside this capture.
The packed allocation is at most 16 MiB and counts toward the 128 MiB GPU budget.
The report records any limit reached. Frame generation should be off for matched
scene comparisons.

Opening flames, transient material corruption, transparency and shadow behavior
still require a scene-matched Xclipse and Adreno comparison. The transfer controls
measure transfer representation, while the pass and shader-binding catalog is needed
to locate an earlier divergence in the game. Capture a warm run and measure frame
times separately with capture off to isolate remaining pipeline compilation stalls
from replay or GPU costs.
