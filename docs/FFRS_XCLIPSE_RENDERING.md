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

## Follow-up: persistent missing geometry in 86-second gameplay recording

The 2026-11-19 follow-up MP4 `1000025325.mp4` still shows no visible player or enemy models in the first battle despite a working FPS overlay (roughly 38–60 FPS), visible terrain, battle bars, targeting marks and effects. The initial DCC/720p profile and later dynamic-rendering/DLC changes **did not establish a visual repair**. Do not call these settings a fix.

Rechecked the user's earlier `ffr-source-frames.tar.gz` and `ffr-render-passes.tar.gz` and the log: the scene is already missing figures in `pass_00.ppm` before final HUD (`pass_01.ppm`), with 1,772,747 draws and 0 missing programs/targets/pipelines by present 1800. Both dumps were postprocessed 1920×1080 targets; neither identifies whether early scene geometry was rasterized. The active driver was RADV MAGELLAN CARL. `DM_VK_MT_REPLAY=1` was present in the export's env.txt.

### Next isolated change

- `fd0b7a64`: On SM-S731* with the FFRS RADV profile, override experimental `DM_VK_MT_REPLAY=1` to `0` **before** `vk_dq_init`, so Vulkan commands are recorded on their replay thread. An explicit `DM_FFRS_S25FE_RADV_ALLOW_MT_REPLAY=1` restores the previous mode for an A/B run. This is a scoped hypothesis test, not a proven repair, and does not affect any other device/game.
- `c7679fd0`: Added `DM_VK_PASSDUMP_MIN_COLORS=N`, selecting only passes that bind at least N color attachments, so final compositing/HUD doesn't consume the capture budget before early UE G-buffer geometry; and `DM_VK_PASSDUMP_LIMIT=N` (1–96) to cap GPU readback allocations on 8 GB phones. Works with existing `DM_VK_PASSDUMP_SIZE` and `DM_VK_PASSDUMP_DEPTH=1`.

**If models remain absent after disabling experimental two-stage replay**, collect early scene passes in one test with these values in the game port's env.txt (or the existing DM_CTL environment mechanism):

```
DM_VK_MT_REPLAY=0
DM_VK_PASSDUMP=1500
DM_VK_PASSDUMP_SIZE=1280x720,1920x1080,960x540
DM_VK_PASSDUMP_MIN_COLORS=2
DM_VK_PASSDUMP_DEPTH=1
DM_VK_PASSDUMP_LIMIT=12
DM_VK_LIST_AT=1500
DM_VK_INDIRECT_LOG_AT=1500
```

Use the same first-battle save. Check log.txt for `pass dump` and the `--- pass ...` lines to identify the first multi-target geometry pass. If the scene outputs and depth show actors but the final composition does not, debug downstream depth, blending and aliasing. If the actors never reach scene/depth, debug vertex data, GPU culling, skinned mesh shaders and buffer visibility. Never equate a successful `vkCmdDraw` call with the mesh actually rasterizing.

- `1f2cdce3`: Added opt-in `DM_VK_INDIRECT_LOG_AT=N`: logs at most 48 indexed indirect draws on each of two frames, including vertex-shader key, draw-argument guest bytes, and cached pool-shadow bytes. These are **not synchronized GPU readbacks**. Zero primitive counts in both suggest a GPU-culling/argument-buffer path to investigate, but nonzero shadow values do not prove a successful draw. This is scoped by opt-in, with negligible cost when disabled.


## Dedicated diagnostic integration branch (2026-11-19)

Baseline \`android\` HEAD: \`e4d1a6176ec380cac5134bea89c5a8df986b628e\`.
Branch: \`fix/ffrs-xclipse-render-stage-capture-20261119\`.

This branch changes **opt-in capture diagnostics only**, not default NVN/Vulkan
rendering. The first color target of a postprocessed scene is not sufficient
evidence to attribute missing actor geometry to vertex shading or the driver.

For one first-battle capture, with frame generation **disabled** and the
verified \`cc87fdf\` RADV driver selected:

\`\`\`text
DM_VK_PASSDUMP=1500
DM_VK_PASSDUMP_MIN_COLORS=2
DM_VK_PASSDUMP_ALL_COLORS=1
DM_VK_PASSDUMP_DEPTH=1
DM_VK_PASSDUMP_STENCIL=1
DM_VK_PASSDUMP_LIMIT=16
DM_VK_PASSDUMP_MAX_MB=128
DM_VK_PASSDUMP_RAW=1
DM_VK_LIST_AT=1500
DM_VK_INDIRECT_LOG_AT=1500
\`\`\`

The new \`pass_dump_manifest.tsv\` records pass/present, color slot
(\`-1\` depth, \`-3\` stencil, \`-2\` texture dump), mip/layer, dimensions,
storage/view formats, pool offset, byte count, and FNV-1a hash over exact
GPU transfer bytes. The optional \`.raw\` files preserve HDR/bitwise values
that are clipped in preview PPMs. Explicit host-cache invalidation occurs
**after** the submission fence completes. The 128 MB readback cap avoids
unbounded allocation growth; skipped captures must not be interpreted as
empty attachments.

These are **diagnostic enhancements, not a rendering fix**. A correct G-buffer
capture from the known-good Adreno reference is still needed to identify
the first divergent pass; without it, do not infer that a shader, indirect
argument, depth test, alias, or RADV driver is the cause. Do not enable
\`RADV_XCLIPSE_DLC\` or other driver toggles as a "fix" without comparative
captures. There is no evidence yet of zero graphical defects.


## Profile cleanup: eliminate unproven rendering defaults

The Xclipse-specific profile now retains only the game's Switch-reference
30 FPS pacing default (explicit \`DM_FPS_TARGET\` wins). It no longer sets
\`DM_OPMODE=0\`, \`RADV_XCLIPSE_DCC_SMALL=0\`,
\`DM_VK_RP=0\`, \`RADV_XCLIPSE_DLC=1\`, or forces
\`DM_VK_MT_REPLAY=0\` when the user explicitly enabled it.
The port still honors those flags if supplied directly in \`env.txt\`.

Reason: none of the graphics-mode or driver changes resolved the missing
character/enemy models in the previous phone tests. Automatically changing
those Vulkan paths without a proven failure mechanism can mask the first
incorrect stage and cause fidelity/performance regressions. This cleanup
is covered by a compiled production-function fixture that verifies the
device and driver guard and preservation of explicit overrides.
