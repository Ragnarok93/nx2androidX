# Standalone spatial upscaling acceptance

## Host verification

Run `python -m unittest discover tests`. The Vulkan probe uses the same recorder
and SPIR-V as the Android runtime. With a host Vulkan 1.3 loader and a compatible
ICD, run `python tests/run_vk_spatial_upscaler.py`. The probe accepts
`--glslc`, `--spirv-val`, `--vulkan-lib` and `--vulkan-include` for
toolchains installed outside the system paths. Enable the Khronos validation layer
with `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`.

Build the Android standalone host with the repository's NDK r27c:

```sh
python nxnative/dm/android/build.py --game 0100606026F12000 --lib --play --exe --release
```

## Device matrix

| Device | Drivers | First comparison |
| --- | --- | --- |
| Galaxy S25 FE, Xclipse 940 | RADV Xclipse and Samsung system Vulkan | Final Fantasy Resonance 1.0.1 at 30 real FPS |
| Galaxy S20+, Adreno 650 | Mesa Turnip | Existing known-good port at its usual real FPS |

1. Keep the original standalone APK. In the builder's Upgrade tab, select it and
   sign the result with the original standalone certificate. Compare package name,
   version code and certificate with `apksigner verify --print-certs` and
   `aapt dump badging`. Install as an update. Confirm the save, selected graphics
   options, imported driver, and pipeline cache survive. A compatible prior
   companion-data export also needs its original data directory.
2. On the same save and display orientation, compare Native, Bilinear, FSR 1,
   GSR 1 and Auto with LSFG and Vivid disabled. Use a source resolution
   smaller than the aspect-fitted surface. Check text, fine edges, motion,
   color, orientation and the picture bounds. Try minimum and maximum
   sharpness and verify the Graphics menu shows the effective backend.
3. Repeat with 2× fixed LSFG and then adaptive LSFG using the same source rate.
   Confirm interpolation transitions, pacing, generated/display FPS and
   frame-generation status. Compare the same scene with Vivid on and off.
   Pause and redraw the menu, resume, rotate the window and suspend/resume.
4. Run the same scene with adaptive resolution at 30, 40 and 60 real FPS
   where the game exposes those targets. Capture actual internal scale,
   upscaler GPU time, total GPU time, real and displayed frame rate,
   frame-time percentiles, peak memory and sustained temperature. Compare
   fixed and automatic LSFG flow at the same output size.
5. Export one diagnostics ZIP per driver after the run. Check the
   `[upscale]` records for the actual backend, fallback reason and GPU
   timing. Report any visual regressions separately from frame-generation
   artifacts or device-specific performance differences.
