# Snapdragon GSR 2 import

Upstream: https://github.com/SnapdragonGameStudios/snapdragon-gsr/tree/d926f074bcb9d714e179f1ce0fcb9ee2eeb5074e/sgsr/v2

The eight GLSL shader files under `include/` are byte-identical to the pinned
revision. The BSD-3-Clause notice includes the shader files' 2024 copyright.
Standalone APKs include the notice as `assets/licenses/sgsr2.txt`.

The Android build generates Vulkan copies with unique descriptor bindings for
the two-pass compute variant, positive-up NDC Y, and dispatch bounds checks.
The two-pass fragment variant uses the runtime's fullscreen triangle. Use
`python tests/run_vk_spatial_upscaler.py --gsr2` to validate SPIR-V and run
all supported variants on a host Vulkan ICD. The probe uses odd image sizes,
moving geometry, jitter, camera cuts and recorded but rejected submissions.

The runtime implements all three variants for validated inputs. Gameplay
integration requires a replay adapter that supplies those inputs, so the
spatial controls are the only modes exposed to players at this checkpoint.

## Runtime integration requirements

- Capture at the verified pre-HUD temporal evaluation point in NVN replay,
  before frame generation and final presentation, not at swapchain present.
- Validate same-frame resolved color, depth convention, encoded velocity,
  current-to-previous clip transform, jitter, exposure, near plane and FOV.
  Three-pass also needs opaque-only color. No synthetic zero-motion substitute.
- Normalize reverse-Z, coordinate conventions and resource semantics in the
  adapter before enabling a title. The current shaders consume standard-Z and
  positive-up clip Y. The dispatched variants use separate reflected fragment
  and compute UBO layouts.
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
