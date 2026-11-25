# Snapdragon GSR 2 import

Upstream: https://github.com/SnapdragonGameStudios/snapdragon-gsr/tree/d926f074bcb9d714e179f1ce0fcb9ee2eeb5074e/sgsr/v2

The eight shader files under include/ and the repository LICENSE are imported
from that revision (BSD-3-Clause). Keep the binary redistribution notice when
the shaders begin shipping in APKs. The original integration guide is linked above.

Status: shader source and temporal eligibility/history policy only. No runtime
dispatch or user-facing GSR 2 setting is enabled by this commit. The current
FSR 1 / Vivid / frame generation present path is intentionally unchanged.

## Runtime integration requirements

- Capture at the verified pre-HUD temporal evaluation point in NVN replay,
  before frame generation and final presentation, not at swapchain present.
- Validate same-frame resolved color, depth convention, encoded velocity,
  current-to-previous clip transform, jitter, exposure, near plane and FOV.
  Three-pass also needs opaque-only color. No synthetic zero-motion substitute.
- Compile GLSL with explicit shader stage and verify descriptor formats and
  UBO offsets against the pinned source. Validate reverse-Z and NDC-Y variants
  before enabling on a title. Keep per-driver pipeline selection capability-gated.
- Allocate history and intermediates from a bounded GPU budget, account for
  allocations and retirement, and use explicit barriers and fence retirement.
  Commit the policy only on successful real-frame submission with ordered
  dependencies. Never advance it for LSFG-generated frames or overlay redraws.
- Prefer verified native temporal hooks first. Then engine adapters, validated
  camera/object motion reconstruction and optional optical flow. Unknown inputs
  keep the existing FSR 1 / linear path. Do not advertise GSR 2 until it dispatches.
- Test native depth/velocity, camera cuts, orientation, dynamic resolution,
  memory pressure, lifecycle, frame pacing and UI composition on Adreno 650,
  Xclipse 940 system/RADV and recent Adreno; measure variants on device.
- Runtime-only APK changes must preserve existing game data, saves, package and
  signing identity. Guest shader instrumentation may need targeted regeneration.
