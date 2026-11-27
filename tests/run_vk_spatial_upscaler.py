"""Compile and run the spatial Vulkan probe with the host loader and an ICD."""
import argparse
import os
from pathlib import Path
import struct
import sys
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ANDROID = ROOT / "nxnative/dm/android"
SHADERS = ("fsr.vert", "fsr_easu.frag", "fsr_rcas.frag", "gsr1.frag", "vivid.frag", "guest_spatial.frag")
GUEST_MODULES = {
    "6e9683d5d012ebac": ("guest_vert", 0),
    "16405ad0ea722934": ("guest_edge_frag", 1),
    "7d05eef41136554f": ("guest_smooth_frag", 1),
}


def write_shader_header(build, symbol, code):
    words = ", ".join(hex(word[0]) for word in struct.iter_unpack("<I", code))
    (build / f"{symbol}.spv.h").write_text(
        f"static const uint32_t {symbol}_spv[] = {{{words}}};\n")


def guest_modules(build, directory, validator, optimizer):
    for key, (symbol, stage_set) in GUEST_MODULES.items():
        module = directory / f"{key}.spv"
        subprocess.run([validator, "--target-env", "vulkan1.3", module], check=True)
        words = [word[0] for word in struct.iter_unpack("<I", module.read_bytes())]
        # Replay gives each stage its own descriptor set. Leave bindings and
        # shader arithmetic intact. OpDecorate = 71, DescriptorSet = 34.
        index = 5
        while index < len(words):
            count, opcode = divmod(words[index], 1 << 16)
            if opcode == 71 and count == 4 and words[index + 2] == 34:
                assert words[index + 3] == 0
                words[index + 3] = stage_set
            index += count
        code = struct.pack(f"<{len(words)}I", *words)
        rebound = build / f"{symbol}.spv"
        rebound.write_bytes(code)
        # The translator emits device capabilities even when unused. Trim
        # declarations before creating shader modules on the software driver.
        subprocess.run([optimizer, "--trim-capabilities", rebound, "-o", rebound], check=True)
        subprocess.run([validator, "--target-env", "vulkan1.3", rebound], check=True)
        write_shader_header(build, symbol, rebound.read_bytes())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--glslc", default="glslc")
    parser.add_argument("--spirv-val", default="spirv-val")
    parser.add_argument("--spirv-opt", default="spirv-opt")
    parser.add_argument("--vulkan-lib", default="-lvulkan")
    parser.add_argument("--vulkan-include", type=Path)
    parser.add_argument("--gsr2", action="store_true", help="run the temporal reconstruction probe")
    parser.add_argument("--guest-shaders", type=Path,
                        help="also probe the three locally supplied FFR SPIR-V modules")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="nx2-spatial-") as tmp:
        build = Path(tmp)
        shaders = [ANDROID / "shaders" / name for name in SHADERS]
        if args.gsr2:
            sys.path.insert(0, str(ANDROID / "tools"))
            from sgsr2_shaders import shader_sources
            shaders += shader_sources(ANDROID.parent / "third_party/sgsr2/include", build / "sgsr2")
            shaders.append(ANDROID / "shaders/sgsr2_fullscreen.vert")
            shaders.append(ANDROID / "shaders/sgsr2_flow_convert.frag")
        for shader in shaders:
            name = shader.name
            spv = build / (name + ".spv")
            subprocess.run([args.glslc, "--target-env=vulkan1.1", "-O",
                            f"-I{shader.parent / 'include'}", shader, "-o", spv], check=True)
            subprocess.run([args.spirv_val, "--target-env", "vulkan1.1", spv], check=True)
            write_shader_header(build, name.replace(".", "_"), spv.read_bytes())
        probe = build / "spatial-probe"
        headers = ["-idirafter", args.vulkan_include] if args.vulkan_include else []
        defines = ["-DSGSR2_HOST_PROBE"] if args.gsr2 else []
        if args.guest_shaders:
            guest_modules(build, args.guest_shaders, args.spirv_val, args.spirv_opt)
            defines.append("-DGUEST_ENLARGEMENT_PROBE")
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-O2", "-pthread",
                        f"-I{ANDROID}", f"-I{build}", *headers, *defines,
                        ROOT / "tests/vk_spatial_upscaler.c", args.vulkan_lib, "-lm", "-o", probe], check=True)
        result = subprocess.run([probe], text=True, capture_output=True)
        print(result.stdout, end="")
        print(result.stderr, end="")
        if "Validation Error" in result.stdout + result.stderr:
            return 1
        return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
