# Reconstruction engineering progress

## 2026-11-27 — Milestone 0, intermediate diagnostics checkpoint

Milestone 0 is **In progress**. Milestones 1–9 are **Pending**.

- Starting commit is `37525418c81ec37590df8a099b3f117f66dad923`. The remote
  development branch matched it before edits. The latest checkpoint is the
  commit introducing this entry, resolved with
  `git log --diff-filter=A -1 --format=%H -- docs/progress/reconstruction-completion.md`.
- Android Actions run [45952434846](https://github.com/Ragnarok93/nx2androidX/actions/runs/45952434846)
  succeeded at the starting SHA. GitHub lists artifact `15177288036`,
  `nx2android-android-apk`, unexpired at inspection. The game runtime is built
  separately by the builder, so this metadata does not identify the installed
  game runtime.
- Ran `python3 devtools/trace_reconstruction.py <capture.zip> -o <report.json>`
  against the 01:47:20 capture. It contains 574 docked Off-mode real records,
  including 122 FSR 1 requests, 114 GSR 1 requests and 96 GSR 2 requests. All
  332 reported None. The capture contains ELF build ID `b0bd3d90e1d50083`
  and the arm-time selection label is `System`. Neither establishes the
  exact runtime commit or resolved per-frame Vulkan driver.
- The prior capture writer placed the guest crop into the reconstruction
  extent fields on bypass. The present path now reports these fields only
  for a successfully submitted reconstruction selected as the downstream
  source. The capture separately records guest extent and crop, downstream
  input extent, drawable and swapchain extent. Submission status does not
  imply GPU completion or compositor display. The trace analyzer flags
  inconsistent submissions and counts bypass reasons while preserving legacy
  archive support.
- Affected code is `gfx_vk_present.c`, `gfx_vk_capture_scale.h`,
  `gfx_vk_capture_session.inc` and `devtools/trace_reconstruction.py`.
  No rendering or fallback policy changed.
- Device validation is outstanding on every GPU. The environment does not
  expose adb or a connected device. The trace evidence predates this change.
- Next action is to stamp the shipped build-tool source SHA into the Android
  payload, carry it through the on-device native build, and export it alongside
  the mapped ELF build ID and resolved Vulkan driver. Then instrument recorded,
  retired/completed and accepted presentation states by real-frame ticket
  without waiting on GPU fences solely for diagnostics.
- Validation for this checkpoint used the reconstruction host runner with
  `PYTHONPATH=.` and the project Python dependencies. All 51 tests passed.
  The production TSV writer compiled with `-Wall -Wextra -Werror`, and the
  presentation fixture checked zero reconstruction extents on forced fallback.
  The legacy archive analyzer reproduced all 332 native 1:1 bypasses.
  `git diff --check` passed. Full discovery was started separately. APK and
  SPIR-V validation remain outstanding. This checkpoint changes no shaders.

## 2026-11-27 — Milestone 0, runtime provenance checkpoint

Milestone 0 is **In progress**. The previous remote checkpoint was
`515a96306c9da1a36e33a0a12d906669dbb03e77`. The latest checkpoint is the commit
adding this section, resolved with
`git log -1 --format=%H -- docs/progress/reconstruction-completion.md`.

- The Android payload now stages a source commit stamp when its source trees
  match the checkout. Direct Android builds resolve the commit in Git, and
  on-device builds consume the staged stamp. An unavailable or modified source
  identity is reported explicitly. The stamp participates in the native
  object cache key for the capture writer.
- Capture metadata now carries the Git identity, release/development setting,
  LSFG build flag, mapped ELF build ID and actual physical-device Vulkan
  driver ID and version. The trace analyzer exports these fields and remains
  compatible with legacy archives.
- Changed `packaging/android-apk/build_assets.py`, the Android native builder
  and Vulkan capture/device initialization, and the trace analyzer. Added
  clean/dirty stamp and capture-header checks to existing fixtures.
- Ran `PYTHONPATH=. python tests/run_reconstruction_host.py` (51 passed),
  `python -m unittest discover -s tests -p test_payload_stamp.py` (2 passed),
  Python bytecode compilation of the changed scripts and `git diff --check`.
  No shaders changed. The local environment lacks the Android SDK/NDK,
  glslc and spirv-val. The first checkpoint triggered Android Actions run
  [45964955086](https://github.com/Ragnarok93/nx2androidX/actions/runs/45964955086).
- No hardware validation was performed. Exact runtime identity in an exported
  game requires a new game APK or runtime upgrade built using the stamped
  builder, followed by a capture from the installed runtime.
- Next action is to record command completion from existing reconstruction
  slot retirement, VK present acceptance, and accepted generated-frame count
  by source ticket. Record source image/crop and allocated output separately
  from fallback consumption. Re-run host and Android build checks before
  starting observation-only source discovery.

## 2026-11-27 — Milestone 0, presentation outcome checkpoint

Milestone 0 is **In progress**. The previous checkpoint was
`cbd0bb0bb6aae377ca80787c8c04629653123f5b`. Resolve the commit adding this
entry with `git log -1 --format=%H -- docs/progress/reconstruction-completion.md`.

- The present path records whether Vulkan accepted the real frame and how
  many generated frames it accepted. The trace analyzer reports these results
  separately from backend residency and the number of generated images
  offered for presentation. Vulkan acceptance confirms queueing, while
  compositor display timing still needs separate evidence.
- Updated the capture writer, presenter, analyzer and relevant host fixtures.
  The host reconstruction runner passed 51 checks. The standalone graphics
  and FFR compatibility fixtures passed all 10 checks. The legacy archive
  parsed successfully and `git diff --check` passed.
- Full discovery at the runtime provenance checkpoint ran 835 tests, with
  four missing-`javac` errors, three LeakSanitizer failures under ptrace and
  20 skips. The reconstruction fixtures passed. Android provenance checkpoint
  build [45965309706](https://github.com/Ragnarok93/nx2androidX/actions/runs/45965309706)
  was in progress. Device validation remains outstanding.
- Next action is to account for GPU completion even when timestamps are
  unavailable, using the existing reconstruction slot fence retirement.
  Record source image and valid crop, output allocation and recording state
  independently. Then review runtime validation before starting Milestone 1.

## 2026-11-27 — Milestone 0, reconstruction lifecycle checkpoint

Milestone 0 is **Awaiting device validation**. Milestone 1 is **Pending**.
The previous checkpoint was `167fe3bfca4b423d54b44029bfe26a3e31bf0946`.
Resolve this checkpoint with `git log -1 --format=%H -- docs/progress/reconstruction-completion.md`.

- The runtime now records the selected source handle and crop, guest image,
  available output allocation, command-buffer recording and successful
  submission. Existing fence waits at reconstruction slot reuse supply
  completion evidence even without timestamp support. The trace joins that
  evidence to the original source ticket. Pending tail slots may leave some
  submissions without observed completion in a stopped capture.
- Source selection still uses the guest presentation image or the existing
  physical-mode offer. The handle does not certify earlier scene color.
  The downstream input extent represents the image supplied to the
  presenter and offered to LSFG. Generated output currently uses that extent.
  Actual compositor display and separate future composition/LSFG stages
  require independent evidence when those paths are introduced.
- Ran the reconstruction host suite (51 passed), the legacy archive trace
  (332 native 1:1 bypasses), and `git diff --check`. No shader code changed.
  The previous checkpoint triggered Android Actions run
  [45965529404](https://github.com/Ragnarok93/nx2androidX/actions/runs/45965529404).
- Device procedure after the signed builder artifact is available. Upgrade the
  FFR 1.0.1 runtime in place with the existing signer. On Xclipse 940, use
  internal scaling Off and select an explicit Vulkan driver. Arm full
  graphics diagnostics. In one scene with 1080p target, capture at least
  120 real frames each for handheld FSR 1, docked FSR 1, and docked GSR 2.
  Repeat with fixed 2× LSFG and export the ZIP. Verify the runtime Git SHA,
  ELF ID, driver ID/version, the submitted and completed 720p-to-1080p
  handheld reconstruction, and the docked bypass with zero reconstruction
  output. Verify a nonzero count of accepted generated frames when LSFG
  successfully warms up. Repeat with Samsung Vulkan and RADV Xclipse.
- Next implementation step for Milestone 1 is to index the existing replay
  surface-usage writer by resource identity and write sequence inside a
  bounded per-frame observation buffer. Begin with the exact
  presentation-source marker and backward traversal of color writes and
  sampled reads. Export the candidate path without substituting resources.

## 2026-11-27 — Milestone 1, offline observation checkpoint

Milestone 0 remains **Awaiting device validation**. Milestone 1 is
**In progress**. Milestones 2–9 remain **Pending**. The previous checkpoint
was `0fc006f3cbcd2b9756d0017d737941ca0a95854b`. Resolve the commit adding
this entry with `git log -1 --format=%H -- docs/progress/reconstruction-completion.md`.

- Extended the existing capture analyzer to shortlist plausible enlargement
  boundaries using an exact presentation dependency, stored color producer,
  matching view formats, near-matching aspect and single-draw raster evidence.
  It reports each candidate's rejection reasons and exports the producer,
  sampled binding and path to the final presentation marker.
- At present 16745 the shortlist includes 1384×780 → 1728×972 and
  1728×972 → 1920×1080. The capture shows single full-size raster draws
  without blending at these two boundaries. It does not establish the
  fragment shader's semantics, source valid region or replacement eligibility.
  The subsequent compositing write remains part of the dependency path.
- The production renderer is unchanged by this checkpoint. The shortlist
  uses the existing bounded capture data. Runtime observation without GPU
  readbacks, replay CPU/memory overhead and device behavior remain outstanding.
- Ran `python3 devtools/trace_reconstruction.py <capture.zip> -o <report.json>`,
  the 52-test reconstruction host suite and `git diff --check`. No shaders or
  Android runtime code changed in this checkpoint.
- Next action is to separate metadata-only dependency acquisition from
  the diagnostic readback session. Reuse the existing surface descriptors,
  exact present markers, and subresource and write-sequence identity.
  Correlate exact transfer/resolve and compute producer events before
  enabling any candidate substitution. Preserve ambiguous or overflowed
  observations without authorizing replacement.

## 2026-11-27 — Milestone 1, metadata-only replay capture checkpoint

Milestone 1 is **In progress**. Milestone 0 remains **Awaiting device
validation**. The previous checkpoint was
`581c632732f2a45488330c7138e320c7ea87c73b`. Resolve this checkpoint with
`git log -1 --format=%H -- docs/progress/reconstruction-completion.md`.

- Set `DM_VK_SURFACE_METADATA_ONLY=1` in the runtime environment before
  arming full graphics capture to retain the three bounded surface catalogs
  and exact present marker without GPU image, uniform or draw-input
  readbacks. Existing full capture remains available with the variable
  unset. The surface TSV footer records the mode and dropped-row count.
- Reuses the existing attachment, sampled/storage, transfer and alias
  records, including formats and subresources. The replay present boundary
  still publishes the complete marker. This is diagnostic observation and
  does not substitute resources in the rendering path.
- The reconstruction host suite passed all 52 tests. Its surface fixture
  verified the metadata-only path emits no image or uniform copy and
  allocates no readback buffer. `git diff --check` passed. No shader source
  changed. Device CPU and memory overhead remain unmeasured.
- To validate on device, repeat the 1080p docked FFR capture with the flag
  enabled. Export the ZIP and inspect the completed surface TSVs for
  `metadata_only=1`, zero completed samples and the two expected upstream
  enlargement observations. Compare replay metrics with capture disabled
  and confirm identical rendering.
- Next action is to give transfer, resolve and compute observations exact
  operation-level producer/consumer identity, then bound the runtime graph
  and assess safe eligibility at the first observed enlargement boundary.

## 2026-11-27 — Milestone 1, recorded transfer evidence checkpoint

Milestone 1 is **In progress**. Milestone 0 remains **Awaiting device validation**.
The previous remote checkpoint was `d1178deabb1d29ba0c02da0287e05feb0fa9cad1`.

- Reproduced the Android APK CI failure from run 45966346348 in the indirect
  compute harness. Updated its stubs for the metadata-only diagnostic branch.
- Moved texture-to-texture transfer observations after successful command
  recording and published the destination after its write sequence advances.
  The analyzer follows consecutive full single-sample color transfer pairs
  with exact subresources and rejects requested or overlapping pairs.
- Indirect compute (7), trace analyzer (19) and surface capture (1) tests
  passed. `git diff --check` passed. No shaders changed. Device tests remain open.
- Next action is to add resolved compute reads and writes and resolve
  provenance without attributing a partial write to a whole attachment.
  Then validate the discovery graph and eligibility against FFR on device.

## 2026-11-27 — Milestone 1, compute and resolve observation checkpoint

Milestone 1 remains **In progress**. Milestone 0 remains **Awaiting device
validation**. The preceding remote checkpoint was
`dce0a937c4b6f798e9c1ce568613f2145056ef3f` and triggered Android run 45969237774.

- Compute observation retains the dispatch ordinal shared with its descriptor
  inputs and reports the resource version after replay's potential write.
  Metadata-only observations preserve the bound subresource range. Empty
  direct dispatches no longer emit output rows. A recorded resolve is marked
  separately from a copy and remains excluded from full-color traversal
  until its source and destination sample coverage can be proved.
- The surface capture and indirect compute host tests passed (8 total).
  `git diff --check` passed. Device validation remains outstanding.
- Next action is to implement bounded selection on live replay events and
  verify that viewport/scissor coverage and shader semantics establish an
  eligible scene input across docked and handheld paths.

## 2026-11-27 — Harness-based acceptance checkpoint

Milestone 0 is **Host validated** with device-specific checks outstanding.
Milestone 1 remains **In progress**. Milestones 2–9 remain **Pending**.

- Presentation harness checks the exact image routed to frame generation,
  including docked 2160p output and native 1080p fallback for Bilinear, FSR 1,
  GSR 1 and GSR 2. The trace harness verifies that delayed fence retirement
  is assigned to its originating ticket, and catches completion attributed
  to a fallback frame. Ran all 54 required host checks successfully.
- Reconstruction CI now runs automatically for relevant changes on this
  development branch. Its host runner installs its required dependency and
  gets the repository import path. Software Vulkan checks shader execution
  and readbacks up to 2160p, while the matched NDK job compiles and links the
  FFR runtime without a device.
- Use host and software Vulkan evidence for selection, geometry, handoff,
  failure and pixel correctness wherever possible. Physical-device follow-up
  is limited to title data and workloads that the fixtures cannot reproduce,
  proprietary/custom driver behavior, presentation pacing and peak memory.
  A fixture selecting native fallback at 1080p does not close the docked
  scene-source selection regression.
- Next action remains the live replay selection path. Use synthetic draw and
  presentation sequences to verify scene-source eligibility, alias rejection,
  lifetime and unchanged fallback before relying on FFR captures.

## 2026-11-27 — Milestone 1, live replay discovery checkpoint

Milestone 1 is **Host validated** for observation-only discovery. Driver and
title validation remain outstanding. Milestone 2 still requires shader
and source-coverage evidence for safe replacement.

- The metadata-only replay path now searches bounded live resource events
  from the exact present image. It follows preserved attachment contents
  and full-color transfers, and looks for a single sampled color input to
  a full-size enlargement. It reports a ticket-bound observation in the
  surface catalog. The existing offline report retains the fuller path
  and raster screening evidence.
- Discovery retains no Vulkan resources and is disabled during normal
  presentation. The observer stores at most 8,192 replay events and follows
  at most 16 writer hops. It rejects incomplete/overflowed history, a cropped
  presentation root, unresolved subresources, and relevant cross-image pool
  aliases. Replay CPU cost on an actual game workload remains unmeasured.
- All 54 required host checks passed. The metadata harness exercised a
  synthetic 1384×780 → 1920×1080 enlargement followed by a loaded composition
  pass, with cropped, aliased, cross-frame and overflow rejection paths.
  The predecessor's Vulkan and Android native link CI jobs passed
  (run 45969835111). This checkpoint changes no shaders.
- Next action is to admit only proven equivalences at the replay insertion
  point. Record sampler and producer coverage and establish the guest
  shader's behavior independently. A plausible observation cannot authorize
  a substitution, and the docked reconstruction regression remains open.

## 2026-11-27 — Milestone 1 replay and CI validation

- Ran the production C observer over the archived FFR 1.0.1 docked
  present 16745 surface rows. It selected the 1728×972 image feeding the
  final enlargement to 1920×1080. The existing offline analyzer also
  observed its 1384×780 upstream input. These are provenance observations.
- Reconstruction validation run 45970378481 passed its software Vulkan
  and FFR Android NDK link jobs at `5704ea06db33e364d8c18fd48eaf58e641eaf976`.
- Milestone 2 FFR eligibility remains unverified. Neither fragment shader
  key (16405ad0ea722934 or 7d05eef41136554f) appears in the capture's shader
  index. The archive contains no SPIR-V modules. Use the matching build's
  DMSV bundle in a host harness to inspect the two fragment modules and
  their shared vertex module (6e9683d5d012ebac), then check coverage and
  color equivalence while preserving the subsequent composition.

## 2026-11-27 — Milestone 2 evidence intake and Android CI repair

Milestone 2 is **In progress**. Milestones 0 and 1 retain their host results
and device acceptance remains open. Milestones 3–9 are **Pending**.

- Verified the starting branch at `579a4dc79b2e82a411f1c728c9b8c5a320598dd6`.
  The signed Android build [45970378346](https://github.com/Ragnarok93/nx2androidX/actions/runs/45970378346)
  at `5704ea06` failed in the full test gate. Repository signing preparation
  passed. Packaging was skipped and GitHub listed zero artifacts. The two
  reconstruction validation jobs succeeded in run
  [45970378481](https://github.com/Ragnarok93/nx2androidX/actions/runs/45970378481).
- The transfer harness stub still used the old three-argument capture
  signature. Its fourth argument now matches production. All 10 transfer
  tests passed locally. This changes only the test double.
- Extracted the three supplied modules and passed independent
  `spirv-val --target-env vulkan1.3` on each. Both fragment and the
  shared vertex entry points match the archived present 16745 draw state.
  Fragment `16405ad0ea722934` uses an RGB edge-directed six-sample filter
  with negative-lobe taps and zero output alpha. Fragment `7d05eef41136554f`
  remaps texel fractions with cubic smoothstep, clamps the coordinates
  using a uniform buffer, samples RGB once at explicit LOD zero, and also
  forces output alpha to zero. The vertex shader uses guest attributes
  and uniform transforms for position and normalized sample coordinates.
  Neither fragment shader reads history or applies exposure or tone mapping.
- The capture establishes full-size viewport/scissor, one color attachment,
  no blend/depth/stencil on passes 75 and 76, and mip 0/layer 0 RGBA8 UNORM
  inputs. Pass 75 clears, while pass 76 loads. The sampled binding rows do
  not resolve sampler state, and the draw input dump does not include
  these draws. Guest vertices, indices and graphics uniforms are needed
  to establish exact coverage and coordinate semantics. Both filters
  change RGB relative to straight bilinear sampling, and an unmodified
  alpha-preserving recorder cannot replace either draw as written.
- Next action is a patterned software Vulkan fixture with explicit
  synthetic uniforms and coverage. Compare RGB and alpha contracts
  before admitting any production insertion.

## 2026-11-27 — Milestone 2 software Vulkan checkpoint

Milestone 2 is **In progress**. The synthetic image and composition checks
passed. FFR docked insertion is **Blocked** on exact draw and source
coverage evidence. Milestones 3–9 are **Pending** under the plan's
dependency order. Milestones 0 and 1 still await device validation.

- Android run [45972298484](https://github.com/Ragnarok93/nx2androidX/actions/runs/45972298484)
  passed at `c435967a2a3f63cc7369d4bb12d4012ca96fcf49`, including the
  repository signing verification and native smoke link. GitHub listed
  [artifact 15185518305](https://github.com/Ragnarok93/nx2androidX/actions/runs/45972298484/artifacts/15185518305)
  unexpired. This is the builder APK for the CI-repair checkpoint.
- Lavapipe ran both supplied fragment modules with the supplied vertex
  module, synthetic vertices and uniforms, linear clamp sampling, and
  patterned color and alpha. Both original and fixture-adapted modules
  passed SPIR-V validation. The edge-directed and smoothstep filters
  differed from bilinear RGB by over two code values at 258 and 208 of
  1,024 pixels, respectively. Both returned zero alpha everywhere under
  full raster coverage.
- The synthetic insertion prepares a private output and commits it to the
  guest-sized attachment. Preserving the draw's zero-alpha write preserved
  the later alpha-sensitive blend. The alpha-preserving blit alone changed
  that blend. Partial coverage preserved the loaded target outside the
  scissor. Aliased input, partial coverage and injected allocation failure
  refused insertion and the subsequent guest draw reproduced its reference
  bytes. The probe exercises synthetic insertion, not NVN replay insertion.
  Run it with `python3 tests/run_vk_spatial_upscaler.py --guest-shaders <local-extracted-directory>`.
- Each archived center tile had alpha 255 at FFR pass 74 and alpha zero
  at passes 75 and 76. Pass 74's viewport was 1383×778 for a 1384×780
  image, with LOAD. Pass 75's clear establishes a candidate with a defined
  target background. Pass 76 is the narrower insertion candidate because
  it retains the first enlargement and leaves later guest composition in
  order. Its LOAD requires exact written pixels and preservation of the
  prior attachment outside the draw. The archive lacks its index/vertex
  data, effective sampler and graphics uniform values, and cannot
  establish full coverage or reconstruct the live draw's UV transform.
- Added opt-in direct indexed draw sampling to the existing Vulkan
  diagnostic capture. The existing `DM_VK_PASSDUMP_INPUT_VS` setting now
  survives arming from the menu. A matching direct draw reports its
  complete small index range, base vertex and instance counts, and
  bounded vertex prefixes. The input capture test verifies alignment,
  range limits, deferred reads and report fields. Scope excludes
  converted quads and ranges outside the bounded readback.
- Local verification passed the 54-check reconstruction host runner, the
  draw input capture test, the archived trace rerun, the guest Vulkan
  probe with synchronization validation, and the GSR 2/large-output
  Vulkan probe. The latter exercised every backend at 1440p and 2160p and
  resolved 2160p to 1080p. These runs do not exercise FFR docked
  presentation or on-device LSFG. Local full discovery lacked some
  Python 3.14 and Android CI dependencies, so the signed workflow is the
  full-suite gate.
- The remaining FFR evidence is a capture of the actual draw inputs and
  effective sampler/uniform state for pass 76. A targeted full GPU
  capture can use vertex key `6e9683d5d012ebac`. The existing targeted
  Vulkan sampler and uniform logs can corroborate the capture. Prove
  the producer crop, sample coordinates and destination coverage before
  applying reconstruction inside the guest pass. Preserve the currently
  composed image for presentation until that boundary is proven.

## 2026-11-27 — Milestone 2 archived-pixel correlation

- Compared the actual RGBA8 center-tile readbacks at presents 16745,
  16775 and 16805. For pass 75, the four half-texel neighborhood taps
  and two edge-directed taps on a top-left 1383×778 crop reproduced the
  archived RGB with mean absolute error 0.249–0.264 code values per
  channel, over 240 pixels per capture whose taps fit inside the tile.
  Assuming the full 1384×780 allocation produced mean absolute error
  18.195–22.120. The observed 1383×778 predecessor viewport is the
  supported crop hypothesis for these center pixels.
- For pass 76, the 1728×972 pixel-center mapping, cubic smoothstep
  of each texel fraction and linear sampling reproduced all 256 RGB
  pixels per capture with mean absolute error 0.240–0.255 and maximum
  absolute error 0.62 code values. Plain bilinear on the same mapping
  produced mean absolute error 1.307–1.362. Both filter comparisons
  used the supplied shader instructions and archived source pixels.
- The final 1920×1080 presentation readback matched all 256 pass-76
  center pixels in each capture. It contained 197,855 pixels with
  nonzero alpha in each capture, whereas the pass-76 shader writes
  zero alpha where it draws. Keep the later composition on the
  original attachment. These center-tile fits establish the filter
  behavior locally and leave full-image coverage, edge clamping and
  preservation of the LOAD contents open.
- Verified implementation checkpoint `566b31b287cd14bf27b046fe4677d7328300ef29`
  on the remote development branch. At this SHA, reconstruction validation
  [45973787312](https://github.com/Ragnarok93/nx2androidX/actions/runs/45973787312)
  passed both software Vulkan and the FFR Android NDK link. Signed Android
  run [45973787386](https://github.com/Ragnarok93/nx2androidX/actions/runs/45973787386)
  passed its 851-test discovery with nine skips, native host smoke, APK
  assembly and repository signing verification. Its
  [builder artifact 15187430279](https://github.com/Ragnarok93/nx2androidX/actions/runs/45973787386/artifacts/15187430279)
  was available and unexpired at inspection. This verifies the build of
  the capture and fixture checkpoint. FFR replay substitution, device
  acceptance and the dependent production milestones remain open.

## 2026-11-27 — Targeted graphics descriptor capture

Milestone 2 implementation and FFR device acceptance remain open. This
checkpoint fills the missing draw inputs in one scoped GPU capture.

- Targeted indexed draw diagnostics now include bounded GPU copies of the
  effective graphics constant buffer and embedded C1 descriptor prefixes.
  Copies use the resolved buffer and offset before dynamic descriptor
  offsets are applied. The report includes stage, binding and array element
  alongside the exact draw ordinal. Passes with graphics memory writes or
  transform feedback still invalidate these deferred samples.
- Combined-sampler rows record the parameters actually selected by the
  sampler lookup and identify dummy fallback. The diagnostic resolves
  supported graphics sampler bindings during full captures only.
- A fragment filter now narrows the existing vertex filter. For the FFR
  final enlargement, set the two entries below in the game's data
  `env.txt`, relaunch the upgraded standalone runtime, then export a full
  GPU snapshot from the in-game menu in docked gameplay.

  ```ini
  DM_VK_PASSDUMP_INPUT_VS=6e9683d5d012ebac
  DM_VK_PASSDUMP_INPUT_FS=7d05eef41136554f
  ```

- The capture harness checks both stage filters, the binding-specific
  deferred UBO bytes and zero-copy sampler metadata. The reconstruction host
  runner passed 54 checks and `git diff --check` passed. The report still
  requires actual draw geometry and producer coverage before any production
  replacement can be admitted.
- Reconstruction validation [45975755961](https://github.com/Ragnarok93/nx2androidX/actions/runs/45975755961)
  passed software Vulkan and the Android NDK link. Android app run
  [45975755979](https://github.com/Ragnarok93/nx2androidX/actions/runs/45975755979)
  reached the full suite and exposed two outdated descriptor stubs in the
  cube-view and indirect-compute harnesses. Both harnesses now match the
  production interfaces and pass locally. The runtime code is identical
  to the NDK-validated checkpoint.
