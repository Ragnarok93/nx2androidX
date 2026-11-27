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
