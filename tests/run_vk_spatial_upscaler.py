"""Compile and run the spatial Vulkan probe with the host loader and an ICD."""
import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ANDROID = ROOT / "nxnative/dm/android"
SHADERS = ("fsr.vert", "fsr_easu.frag", "fsr_rcas.frag", "gsr1.frag", "vivid.frag")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--glslc", default="glslc")
    parser.add_argument("--spirv-val", default="spirv-val")
    parser.add_argument("--vulkan-lib", default="-lvulkan")
    parser.add_argument("--vulkan-include", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="nx2-spatial-") as tmp:
        build = Path(tmp)
        for name in SHADERS:
            shader = ANDROID / "shaders" / name
            spv = build / (name + ".spv")
            subprocess.run([args.glslc, "--target-env=vulkan1.1", "-O",
                            f"-I{shader.parent / 'include'}", shader, "-o", spv], check=True)
            subprocess.run([args.spirv_val, "--target-env", "vulkan1.1", spv], check=True)
            code = spv.read_bytes()
            symbol = name.replace(".", "_") + "_spv"
            words = ", ".join(hex(word[0]) for word in struct.iter_unpack("<I", code))
            (build / f"{symbol.replace('_spv', '.spv')}.h").write_text(
                f"static const uint32_t {symbol}[] = {{{words}}};\n")
        probe = build / "spatial-probe"
        headers = ["-idirafter", args.vulkan_include] if args.vulkan_include else []
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-O2", "-pthread",
                        f"-I{ANDROID}", f"-I{build}", *headers,
                        ROOT / "tests/vk_spatial_upscaler.c", args.vulkan_lib, "-lm", "-o", probe], check=True)
        result = subprocess.run([probe], text=True, capture_output=True)
        print(result.stdout, end="")
        print(result.stderr, end="")
        if "Validation Error" in result.stdout + result.stderr:
            return 1
        return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
