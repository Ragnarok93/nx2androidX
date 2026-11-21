# Vulkan depth and stencil probe

The probe uses an offscreen 8×8 image. Each case checks the results of a
known 0.625 transfer clear, two reversed-Z render passes with indexed draws,
depth and stencil copies, and a depth-only sampled view. The color check
uses a separate RGBA8 image. Yellow left and green right are expected,
independently of the depth transfer representation.

## Build and run

From the repository root, with `glslangValidator` and `spirv-val` on PATH:

```bash
python3 devtools/vk_depth_probe/build.py /tmp/vk-depth-probe
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation \
VK_LAYER_ENABLES=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT \
  /tmp/vk-depth-probe
```

For Android, pass the path to the NDK's `aarch64-linux-android31-clang`
using `--cc`. Push the resulting executable to a device staging directory
such as `/data/local/tmp` and run it under ADB to test the system loader.
To test an imported driver, use the staged ARM64 libadrenotools libraries
matching the app and the exact imported `.so`:

```text
LD_LIBRARY_PATH=<hooks_dir> ./vk-depth-probe --custom <hooks_dir> <driver_dir/> <driver_so>
```

The hooks and driver directory must be readable by the process running the
probe and usable in the device linker namespace. The driver probe rejects
a Samsung fallback for a RADV-named library. Keep the selected driver
identity, `sha256sum` of the input driver binary, and both device logs.
If shell execution cannot load the same driver, no custom-driver result
can be attributed to that APK's driver.

D24 clear should report low 24 bits approximately `009fffff`. Float word
`3f200000` is correct for the D32F depth copy and nonconforming for D24.
Raster colors, sampled depth, clear copy and drawn copy are separate checks.
A format the device cannot create is reported as skipped. Depth sampling
is also feature-tested and can be unavailable independently. Exit status
is 0 for all exercised checks passing, 1 for a mismatch, 2 for setup/Vulkan
failure, or 77 if neither depth/stencil format can be tested.
