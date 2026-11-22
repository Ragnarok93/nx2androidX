# Standalone game: in-game Pacing controls

The game menu now includes a **Pacing** tab in addition to Gamepad,
Game and Graphics. These settings are stored in the standalone port's
`settings.txt`, independently of game save data.

## Fixed source-frame targets

**Frame rate** cycles through 30, 40 and 60 FPS. Only games with a
validated Unreal `t.MaxFPS` / `rhi.SyncInterval` build-ID pair in
`host/hle_fpslock.c` support this live mode. Changing the value updates
the game's CVar copies on subsequent presents and sets the matching
NVN/Android pacing deadline; it does not create missing game frames.

* 30: 33.333 ms / source frame
* 40: 25.000 ms / source frame; a 120 Hz panel can show each for 3 refreshes
* 60: 16.667 ms / source frame

The selected mode bypasses the host's automatic 60→40→30 rate
downshift. It **does not** guarantee that a CPU- or GPU-bound game can
reach the chosen rate. LSFG generated frames are displayed and counted
separately; they do not alter the real source-frame budget.

## Adaptive **internal** resolution

`hle_nvn.c` samples actual GPU work, not generated frame count. Every
30 usable source frames it measures the 90th-percentile GPU duration,
using 84% of the selected source-frame budget. Two sustained over-budget
windows lower 3D render scale by 5 percentage points; four comfortably
under-budget windows raise it by 5. The scale remains between the
in-game **Minimum scale** (50–95%, default 70%) and 100%. Pacing itself
is never lowered by this controller.

This option is available **only when the runtime identifies a real,
game-engine-internal scaling hook**:

* A rewritten NSO with both `FPS_STATE_OFF` and
  `# RENDER_SCALE 1` in its fixups. Its compiled 3D render-scale
  stub consumes the state page's float at offset `+0x10`.
* Optionally, a validated Unreal float `r.ScreenPercentage`
  `TAutoConsoleVariable` for the two recognized FINAL FANTASY RESONANCE
  main build IDs. The environment value `DM_UE_SCREENPCT_OFF` must be
  the *verified main-relative offset of that CVar object*, **not**
  a guessed address or the pointer to its float data. The runtime
  validates that the CVar pointer and both float copies are readable
  and have plausible percentage values before enabling the control.

**FINAL FANTASY RESONANCE presently has no verified
`r.ScreenPercentage` object offset in this repository.** The Pacing
menu therefore reports dynamic internal scaling as **Unavailable** for
this game unless that hook is verified and configured. This deliberate
safety boundary protects depth images, postprocessing, UI, render-target
aliasing and NVN-to-Vulkan rendering accuracy. A presentation-only
FSR 1 pass is not a substitute for internal 3D resolution control.

When available, the UI shows the actual selected internal percentage.
The existing manual `DM_RENDER_SCALE` is preserved when adaptive mode
is turned off; an explicit developer `DM_DYN_SCALE` environment option
continues to work for games with render-scale stubs.

## Verification

The regression suite includes `test_live_graphics_policy.py`,
with a compiled native C test for hysteresis and floor/ceiling
behavior. GitHub Actions also compiles/links the Android host and builds
the installer. Physical-device gameplay still needs to verify pacing,
GPU timing, pause/resume, scene transitions and memory pressure.

Existing standalone game APKs must be **re-exported** from the updated
builder to receive the new native menu/runtime.
