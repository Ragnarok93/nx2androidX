# The Elder Scrolls V: Skyrim — Android bring-up

Status: **experimental; no installable APK verified**. Title ID `01000A10041EA000`.

## Evidence (2026-11-21)

The Galaxy S25 FE alpha31 log for game version 1.0.12.517175 shows successful ExeFS/RomFS extraction (14.6 GB) and rewrites of `main` and `sdk`. Shader splitting stopped with zero shader records, before translation or APK packaging. Main contains NVN control sections that the existing raw-code heuristic cannot pair with shader code.

This profile registers the observed base-game build IDs and survey-derived memory configuration, with no unsupported frame-rate or rendering claims. The BSA scanner now examines shader-named entries inside Bethesda archives without reading the entire multi-GB archive. `Skyrim - Shaders.bsa` is a *candidate* source; its presence in the user's dump and the shader encoding **have not been verified** from the attached diagnostics.

## Next validation

1. Rebuild from the original game dump with this profile; inspect the BSA member count and names, and record which members contain recognizable control+code pairs.
2. If shader count is still zero, inspect the actual asset paths and payload headers and implement their specific decoder. Do not accept zero-shader output.
3. Complete shader translation and create the self-contained APK.
4. On Galaxy S25 FE/Xclipse, verify driver selection, title, 3D scene rendering, controls, audio, save/load and FPS before changing status to `boots` or `playable`.

No game assets or encryption keys belong in this repository.
