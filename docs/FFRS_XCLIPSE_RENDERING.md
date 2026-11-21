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


## Standalone APK: exportable rendering diagnostics (opt-in)

The game's **native pause menu** has a fourth **Diagnostics** tab (also
reachable with a controller). Its rows are:

- **Capture next frame — Capture**: one-shot capture on resumed gameplay.
  Arm automatically sets \`DM_VK_PASSDUMP_ALL_COLORS=1\`,
  \`DM_VK_PASSDUMP_MIN_COLORS=2\`, \`DM_VK_PASSDUMP_DEPTH=1\`,
  \`DM_VK_PASSDUMP_STENCIL=1\`, \`DM_VK_PASSDUMP_RAW=1\`,
  \`DM_VK_PASSDUMP_LIMIT=16\`, \`DM_VK_PASSDUMP_MAX_MB=128\`,
  and \`DM_VK_PASSDUMP=2\` (avoid capturing a paused-menu redraw).
- **Trace details — OFF by default**: when enabled, the next manually
  requested capture also logs pass/dispatch/resource relationships and bounded
  indirect argument records for two presents (\`DM_VK_LIST_AT\` and
  \`DM_VK_INDIRECT_LOG_AT\`). These **CPU/cached-shadow values are not
  authoritative GPU-consumed indirect readbacks**.
- **Export diagnostics — Save ZIP**: Android's standard
  \`ACTION_CREATE_DOCUMENT\` picker. The report is streamed asynchronously,
  including current and up to three previous native-host logs, an opt-in
  capture's \`pass_dump_manifest.tsv\`, the source-byte FNV hashes and
  raw/preview/float attachments where available, the one-shot settings,
  and a report README with actual device/Android fields. It can be exported
  during a run or after restarting the game following a crash.
- **Capture status** and **Capture limit** explain whether a request is
  armed/available and the 16-target/128-MB GPU readback budget.

Normal startup does not enable costly traces or per-pass readbacks.
Opening the menu alone does not allocate diagnostic GPU resources. Trace mode
is a per-process choice, **not** automatically persisted in \`settings.txt\`.
The capture is a bounded one-shot request, not continuous video recording.
No APK/debug signing or Android all-files permission is needed to save the ZIP.

The Java archive writer only includes direct diagnostic files. It excludes
RomFS, game images/libraries, shaders, imported DLLs, saves and signing
material; streaming uses a 64-KiB buffer, a 128-MiB per-file cap, a
384-MiB source-data budget and 310 candidate-file limit. Large native logs
export their last 8 MiB *and* their first 64 KiB (preserving initial Vulkan
driver identity). Only pass outputs indexed in the latest valid manifest
belong to the exported capture: stale previous-capture files are omitted.
The APK shows a completion/failure notification. A system document-provider
failure may leave a partial destination, so a failed report must be retried.

**Limitations:** The menu cannot be opened while the app is terminated,
but relaunching preserves prior logs and capture files. Device graphics
correctness is still unresolved; an exported report is not proof of fixed
geometry. If a scene has no pass with at least two color attachments after
arming, no MRT preview will be produced. No full-frame capture runs in the
background without the explicit Capture action.


## User capture: 2026-11-19, SM-S731U, imported RADV

The attached standalone ZIP was taken in the field at present 2675, not during
a verified battle scene. It captures 1384x780 scene targets. Pass 1 had
**one** prepared draw and uniform clear values in its four MRTs and
zero depth/stencil. Pass 2 had **zero** prepared draws and six black MRTs.
Those clears are not evidence of missing geometry.

The initial 16-entry budget captured all 12 outputs from those two passes,
then only four of pass 3's six MRTs (107 draws). Passes 4 and 5 each have
around 100 draws but none were read back. The fourth pass-3 output contains
both level geometry and a small player model, so *some* actor triangles do
reach the G-buffer. No conclusion about battle actors or composition follows
from this field frame alone.

The log records **384 skipped draws: no pipeline** by present 4200, with
zero missing shader programs or render targets and no reported Vulkan
pipeline-creation errors. The native source's asynchronous shader pipeline
queue intentionally omits draws until first-use compilation completes.
For this one phone/profile, `DM_VK_ASYNC_PIPE=0` is now the default:
first-use graphics pipelines must exist for the draw to execute. This removes
one **measured** class of skipped draws, but can cause first-use compilation
hitches, and is **not proven to fix persistent geometry**. An explicit
`DM_VK_ASYNC_PIPE=1` in env.txt wins. Snapdragon is not changed.

Two presents logged 96 indirect draws with nonzero **guest** index/instance
arguments, but the Xclipse buffers have no accessible copied-pool shadow.
These are not authoritative GPU-consumed arguments. The log has a single
stale texture-alias warning; its relationship to the missing meshes is unknown.

**Capture improvement**: the menu's one-shot capture now ignores passes
with fewer than **10 successfully prepared draws** and captures up to
**24 attachments**, still capped at **128 MiB GPU readback**. This should
include scene MRTs and depth/stencil after geometry rather than clearing
passes. No readbacks run when capture is not explicitly armed.

Next acceptance test: capture the *battle frame where combatants are absent*,
then compare pass/depth/stencil and GPU-produced vertex/indirect buffer bytes
against a known-good Adreno frame at the equivalent scene. Do not ship a
claim of zero graphical glitches without this confirmation.


## 2026-11-19 19:42 player report: every 3D gameplay element affected

Examined exact uploaded `nx2android-render-diagnostics-20261119-194209.zip`.
The user reports that **only UI is rendered correctly; 3D is broadly corrupt**.
The active driver is custom RADV MAGELLAN CARL, Vulkan 1.4 on SM-S731U
Android 17. The game runs 1384×780 internally. The new synchronous-pipeline
policy is visibly active at startup. At presents 3600, 4800 and 5400,
replay logs report **zero no-program, no-target and no-pipeline skips**.
It is now demonstrated that compilation omissions do NOT explain the remaining
3D failures; do not keep proposing them as a full fix.

The one-shot GPU transfer at present **3665** produced 24 entries from scene
passes 3, 4, 5: each has six MRTs + D24S8 depth + S8 stencil. Color MRTs
clearly contain 3D world imagery and material/normal data (the depth buffer
is not the *only* source of scene data). However, **every one of the three
D24S8 raw depth planes (4,318,080 bytes each) and every S8 stencil plane
(1,079,520 bytes each) is entirely zero**, as are their visual previews.
The D24 depth is indeed a reversed-Z buffer, initially cleared to 0.
The two traced submissions just before MRT rendering show **three early
91-draw depth-only passes**, all uncaptured by the earlier multi-color-only
selection. Whether those passes write depth, and whether the RADV D24S8
transfer correctly exposes those writes, must be determined, NOT inferred.

Surgical diagnostic change: the Capture action now also copies the **first
eligible depth-only prepass** (at least 10 draws, no color targets) with its
own pass number, before it is reused by any G-buffer pass. The optional
Trace Details mode logs the *actual* Vulkan depth test/write/compare,
stencil, culling, winding, rasterizer discard, depth range and viewport of
up to the first three pipelines of each depth-using pass, capped to 72
entries per submission. These are diagnostics only and are off except for
explicit player-armed captures; there is **no depth mode or shader override**.
On 8-GB phones, the existing 24-target/128-MB total GPU readback cap remains.

This is a device-specific lead, not a proven device-driver bug. A readback
adds ordering and cache operations that may affect a broken driver. If the
first depth-only readback is zero despite a valid write-enabled pipeline,
then investigate clip-space position output, culling, guest/GPU vertex
inputs, and RADV depth implementation. If the early depth contains values
but later snapshots are zero, investigate depth load/store transitions and
D24S8 readback. Compare matching scenes with Snapdragon before choosing a
renderer or driver patch. The user's already-tested subgroup workarounds,
DCC, DLC, render pass modes and threaded replay overrides were not
successful; do not silently re-enable them.


**Raw-depth readback control (independent of any scene draw):** on manual
capture only, `DM_VK_PASSDUMP_DEPTH_CONTROL=1` makes a separate 8×8 D24S8
image, clears it to depth **0.625** and stencil **0x5a**, and transfers the
depth aspect to a readback buffer through the exact Vulkan copy command
used for the scene. Manifest pass **-200**, target **-1**, has an ordinary
`pass_NN.raw` record, and the log prints `[depthcontrol]`. The recorded
D24 low 24 bits should approximate `round(0.625*16777215)`, allowing
normal UNORM quantization. Its **constant-depth preview looks black** due
to per-image min/max normalization: inspect the `raw` bytes or the logged
min/max, *not* its preview. This control does not modify the game depth
image, its pipelines or draw commands; it only adds opt-in transfer work.
An all-zero control means the main scene depth capture cannot be relied
upon until the depth-copy failure is independently investigated. A valid
control and all-zero depth after a write-enabled depth prepass implicates
the actual rendering or depth-load/store path. Either way the next step is
evidence, not a guessed rendering override.

## 2026-11-19, 21:42 CST: isolated RADV D24S8 format violation

User supplied `nx2android-render-diagnostics-20261119-214247.zip` and
`1000025928.mp4`. The movie shows UI and dialogue sprites rendered correctly
but combatant geometry missing or corrupted in the battle. The 1384x780
capture at present 2502 contains: an early **92-draw** depth-only pass
(pass 1), six color MRTs in scene passes 3-5, and entirely zero raw
depth readbacks for the scene passes. The traced prepass has depth test
and write **enabled**, compare **ALWAYS** (VkCompareOp=7), and no
rasterizer discard. No pipeline/program/target skips were recorded.

The 8x8 isolated depth control, `pass_00.raw`, is definitive about
one **driver behavior**: all 64 32-bit words equal `0x3f200000` (IEEE-754
float32 depth **0.625**). Vulkan's `vkCmdCopyImageToBuffer` with
`VK_FORMAT_D24_UNORM_S8_UINT` requires a 32-bit word with **packed
UNORM D24 in the lowest 24 bits** (approximately `0x009fffff`).
Therefore the driver returned the right numeric clear as the **wrong
transfer format**. The previous 0/64 FAIL was correct about
conformance but not explicit about the float format.
See Vulkan specification, Copy Commands, image-to-buffer depth
aspect representation.

Two *scoped* mitigations for **FFRS on SM-S731* with selected RADV**:
1. `DM_VK_LRZ_TOUCH=0` removes the **Adreno-specific** low-resolution
   depth workaround, which defaults to enabled globally and repeatedly
   copies and writes depth texels back. On this RADV build, the D24
   transfer representation is nonconforming, so copying it back into
   real scene depth images is unsafe. This removes a concrete
   incompatibility mechanism from Xclipse gameplay, while retaining
   the proven Adreno behavior.
2. `DM_VK_D24_FLOAT_COPY=1`: where the guest calls CPU ReadTexels on a
   D24S8 image, normalize the observed float32 depth into the guest's
   required packed UNORM D24. No change to shaders, GPU texture reads,
   draw depth state or vertex generation. Explicit env.txt overrides
   take priority. The conversion is covered by a host-compiled
   executable test, including 0.625, 0, 1, NaN, and out-of-range.

The **opt-in** depthcontrol now distinguishes packed D24 from
float32 and logs the exact raw first word and per-format match
counts instead of a misleading generic FAIL. On this one driver, PPM
and .f32 preview data decode float32 too; raw copies always stay
unmodified for evidence and later comparisons.

**Open:** This transfer conformance defect is confirmed, but may
not be the sole cause of missing battle geometry. In particular,
zero-valued scene depth can represent valid reverse-Z clear values
when no primitives rasterize, or could be related to driver depth
metadata/cache incoherency. The next hardware test must verify the
same battle with the LRZ copies disabled and the driver-specific
CPU conversion enabled. If still wrong, reduce D24 rendering itself
to a small Vulkan raster/depth test with matching Adreno
reference, and inspect GPU-consumed indirect and vertex data.
Do not claim zero-defect visuals before that comparison.

## GPU buffer samples and depth probe

For the next matched battle capture, enable **Trace details** and press
**Capture next frame** on both phones. Confirm frame generation is off,
the scene settings and camera match, and the startup `[driver] selected
Vulkan device` line names the expected driver. Attach the exact imported
driver binary's SHA-256 to the test record. Select a targeted vertex shader
with `DM_VK_PASSDUMP_INPUT_VS=<hex key>` when a general capture fills its
sample budget before reaching the intended draw.

The completed ZIP adds `metadata/gpu_draw_inputs.tsv` when eligible
indexed indirect draws were sampled. Compare matching shader, pass and
draw context first. Pool IDs and addresses can differ across runs. Decode
little-endian command records as `indexCount, instanceCount, firstIndex,
vertexOffset (signed), firstInstance`, using the recorded stride. A
`draw-count` row provides the GPU count before clamping to max draw count.
Vertex `attributes` are `location:VkFormat:byteOffset`. Index and vertex
prefix rows need to be checked against the indirect draw's actual range
before drawing conclusions. See the [audit](NVN_VULKAN_XCLIPSE_AUDIT_20261119.md)
for ordering and coverage limits.

For the same drivers run the [independent Vulkan depth probe](../devtools/vk_depth_probe/README.md).
Use its raster color and sampled depth to evaluate the attachment separately
from the D24 transfer representation. A post-change game capture on Xclipse
is still needed even if the independent probe passes.

### D24 capability check

The in-game depth transfer control now logs the selected driver's D24S8
format features and the result of an image-format query for the exact
attachment and transfer usage. A SKIP means the request is unavailable;
the public candidate RADV source explicitly rejects D24S8 attachments.
If supported, compare the 64 words with the independent probe. See
[D24 capability and driver provenance](NVN_VULKAN_XCLIPSE_AUDIT_20261119.md#d24-capability-and-driver-provenance)
for the code path and the limits of the older capture.

### Intro capture and RADV test controls

The 21:04 export captured present 1219 on the intro road with frame generation
off. The prepass wrote depth and the G-buffer and exposure inputs contained
scene data. Both 87×49 RGBA16F images called out by the overlap checker
contained nonzero, finite values. Their older 128×128 neighbors have different
formats and extents. The overlap report alone does not establish that those
neighbors produced the expected texels. Compute storage writes now advance
the image writer sequence after replay records the dispatch.

The captured RADV build is
[`cc87fdf712`](https://github.com/JimVulkan/radv-xclipse/tree/cc87fdf71267dd0acc3b470920a6566a1a46f0be).
Its application shader DLC option affects device-scope loads, DCC controls
color-target compression, and the BC emulation option controls GPU decoding.
The standalone pause menu exposes them under **Diagnostics → RADV test flags**.
Each selection takes effect after fully restarting the game. The diagnostic ZIP
records the startup values. Test one flag at a time on the same scene.
**Delayed capture** waits 120 presents before looking for scene geometry.

The CPU BC comparison requires the Vulkan fallback to recognize guest block
compression even when it creates an uncompressed backing image. It now does,
including when streamed data replaces the texture. The CPU buffer-copy path
also waits for tracked GPU producers and reads the ordered update bytes before
decoding. These are renderer fixes with host regression coverage for texture
initialization and refresh. The 21:04 export predates Limit Break and does not
identify the cause of its black textures or the missing intro flames.
