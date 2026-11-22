"""Build the Vulkan depth probe for Linux or Android without a Vulkan SDK."""
import argparse
from pathlib import Path
import struct
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--cc", default="cc", help="C compiler, or NDK aarch64-linux-android31-clang")
    parser.add_argument("--glslang", default="glslangValidator")
    parser.add_argument("--spirv-val", default="spirv-val")
    args = parser.parse_args()
    source = Path(__file__).resolve().parent
    output = args.output.resolve()
    outdir = output.parent
    outdir.mkdir(parents=True, exist_ok=True)
    for name in ("triangle.vert", "early.frag", "late.frag", "depth_sample.comp"):
        key = name.replace(".", "_")
        spv = outdir / (key + ".spv")
        subprocess.run([args.glslang, "-V", "--target-env", "vulkan1.2",
                        "-o", str(spv), str(source / name)], check=True)
        subprocess.run([args.spirv_val, "--target-env", "vulkan1.2", str(spv)], check=True)
        words = [f"0x{w:08x}" for (w,) in struct.iter_unpack("<I", spv.read_bytes())]
        rows = [", ".join(words[i:i + 8]) for i in range(0, len(words), 8)]
        (outdir / (key + ".h")).write_text(
            f"static const uint32_t {key}[] = {{\n    " + ",\n    ".join(rows) + "\n};\n")
    includes = source.parents[1] / "nxnative/dm/third_party/vulkan-headers/include"
    subprocess.run([args.cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-Wno-missing-field-initializers", "-I", str(includes), "-I", str(outdir),
                    str(source / "probe.c"), "-o", str(output), "-ldl"], check=True)


if __name__ == "__main__":
    main()
