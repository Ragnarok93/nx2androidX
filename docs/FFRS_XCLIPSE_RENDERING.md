# Final Fantasy Resonance — Xclipse 940 rendering investigation (2026-11-19)

Device: Galaxy S25 FE (SM-S731*), Exynos 2400 / Xclipse 940, Android 17, imported JimVulkan RADV. Repository: `Ragnarok93/nx2androidX`, `android` branch.

## Symptom and confirmed evidence

In the first Steel Bot battle, scenery, dust, HUD, targeting labels, UI sprites and health bars are present, but the player characters and enemies are absent. Frame generation OFF did not resolve it; forcing subgroup size 32 also did not resolve it and performed worse.

Source: device export `ffr-render-passes.tar.gz` shared on 2026-11-19, containing `log.txt`, `env.txt`, `frame_001500.ppm`, `pass_00.ppm`, `pass_01.ppm`.

- `frame_001500.ppm` is the first-battle frame with UI, 1920×1080.
- `pass_00.ppm` is the final postprocessed scenery without HUD. Geometry is already absent.
- `pass_01.ppm` is the final scenery and HUD; geometry still absent.
- These **do not capture the internal UE scene/G-buffer pass**. Do not conclude that vertex shaders failed from these alone.
- Vulkan device in log: `AMD Radeon Graphics (RADV MAGELLAN CARL)`, Vulkan 1.4, driver identifier `0x6863063`, custom RADV active (no system fallback).
- At present 1800: 1,772,747 draws, zero skipped for missing program/target/pipeline. At 3000: only 3 skipped pipelines.
- 13 `[vk] [alias] stale read` diagnostics where a sampled old image overlaps memory subsequently rendered by a different image. Potential source of wrong sampled content, but not yet proven to explain absent geometry.
- At frame 1500: p50 frame time 24.86 ms (~40 FPS), GPU p50 14.43 ms; target 60 FPS was explicitly overridden in `env.txt`. CPU/replay and submission pacing deserve later work.
- The capture explicitly enabled experimental `DM_VK_MT_REPLAY=1`, ordinarily off by default in `gfx_vk_dq.c`.
- The capture also showed `[RED] [vk] the surface offers no format 0 for the window texture`, a confirmed independent color/presentation bug. The async present thread called `present_one` without first setting `im_meta_srgb`.

## Work applied on `android`

1. `0411bf61` fixes async present to choose sRGB / UNORM based on `im->fmt` inside `present_one`, on both sync and async pathways. `f3590a54` adds regression tests.
2. `7e393dd2` prefers Vulkan dynamic rendering (`DM_VK_RP=0`) for S25 FE + imported RADV only, bypassing imageless framebuffers. Override `DM_VK_RP=1` restores previous behavior. `f90edde0` adds test.
3. `6730e41f` enables `RADV_XCLIPSE_DLC=1` on that device/game/profile as an experimental correctness workaround for cross-shader GPU memory coherency. Override `RADV_XCLIPSE_DLC=0` to disable; `08b6a61f` adds test.

These changes have **not** been visually verified on the phone. No claim of repaired missing characters is justified yet. Adreno and non-FFRS profiles must retain original behavior.

## Driver selection matters

The previously used `radv-xclipse-27.0.0-devel-fe9b894.zip` is v1.9.0-RC1. JimVulkan's later v1.9.0 `cc87fdf` (2026-11-11) added the `debug.radv_xclipse_dlc=1` / `RADV_XCLIPSE_DLC=1` cache-coherency option for Xclipse 940 shader reads after other shader writes. It is **not** present in RC1. Prefer `radv-xclipse-27.0.0-devel-cc87fdf.zip` in the new standalone export. Do not misrepresent DLC as a proven FFRS fix; driver author suggests testing for similar missing/corrupted geometry.

Source: https://github.com/JimVulkan/radv-xclipse/releases/tag/v1.9.0 (check release URL if tags differ), or https://github.com/JimVulkan/radv-xclipse/releases.

## Device-side verification required

1. Build the current native/Android app, export one self-contained FFRS APK selecting stable RADV `cc87fdf`. Confirm actual identity via startup log, not just UI selection.
2. Revisit the **same first battle** and compare visually with FG off. Check if the `format 0` warning is gone and `passes: dynamic rendering` appears. Record average/p50 GPU and frame times. Distinguish which rendering errors remain.
3. If characters still absent, change **one** startup environment option per comparison:
   - `RADV_XCLIPSE_DLC=0` to A/B Xclipse cache coherency.
   - `DM_VK_RP=1` to A/B dynamic vs imageless render passes.
   - `DM_VK_MT_REPLAY=0` to A/B threaded Vulkan command recording; it was enabled in the original trace.
   - Only if needed, `RADV_XCLIPSE_DCC=0` to test compression, rather than assuming small-target DCC caused it.
4. Capture **early geometry and depth**, not just final postprocessing:
   - Arm `DM_VK_PASSDUMP=1` near the problematic frame through `DM_CTL env`, or `DM_VK_PASSDUMP=1500` at process start.
   - `DM_VK_PASSDUMP_SIZE=1280x720` (likely handheld scene target; actual resolution must be checked), plus `DM_VK_PASSDUMP_DEPTH=1`; adjust size to match actual scene pass.
   - `DM_VK_TRACE_PASS=-2` with the desired `DM_VK_LIST_AT` present to enumerate per-pass draw shaders and image reads.
   - Inspect depth `.f32` exports. Determine whether meshes exist in the geometry/G-buffer before UE's postprocessing.
5. If geometry absent from early passes, focus on skinned vertex inputs, GPU culling / indirect buffers, and compute→graphics synchronization. If present in early passes and lost downstream, focus on depth/stencil, postprocessing, aliasing, and pass boundaries. Do not make broad Vulkan memory workarounds without decisive evidence.

**Status:** rendering correctness outstanding until device screenshot shows visible characters and enemies. GitHub Issues is disabled for this repo, so this document is the handoff tracker.
