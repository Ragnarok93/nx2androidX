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
