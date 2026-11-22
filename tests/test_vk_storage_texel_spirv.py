"""Validate the runtime rewrite of formatted R32 storage texel buffers."""
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

ANDROID = Path(__file__).resolve().parents[1] / "nxnative/dm/android"


def module(component, image_format):
    def op(code, *args):
        return [(len(args) + 1) << 16 | code, *args]

    scalar = op(22, 3, 32) if component == "float" else op(21, 3, 32, component == "int")
    return [
        0x07230203, 0x00010000, 0, 17, 0,
        *op(17, 1), *op(17, 47),                  # Shader, ImageBuffer
        *op(14, 0, 1),                            # Logical, GLSL450
        *op(15, 5, 13, 0x6e69616d, 0),           # GLCompute main
        *op(16, 13, 17, 1, 1, 1),
        *op(71, 8, 34, 0), *op(71, 8, 33, 0),    # DescriptorSet, Binding
        *op(19, 1), *op(33, 2, 1), *scalar,
        *op(23, 4, 3, 4),
        *op(25, 5, 3, 5, 0, 0, 0, 2, image_format),
        *op(32, 6, 0, 5),
        *op(21, 9, 32, component != "int"),
        *op(43, 9, 10, 0),                        # coordinates
        *op(43, 3, 11, 0x3f800000),              # raw 32-bit payload
        *op(44, 4, 12, 11, 11, 11, 11),
        *op(59, 6, 8, 0),
        *op(54, 1, 13, 0, 2), *op(248, 14),
        *op(61, 5, 15, 8), *op(99, 15, 10, 12),
        *op(253), *op(56),
    ]


def instructions(data):
    words = struct.unpack(f"<{len(data) // 4}I", data)
    i = 5
    while i < len(words):
        size, code = words[i] >> 16, words[i] & 0xffff
        yield code, words[i + 1:i + size]
        i += size


@unittest.skipUnless(shutil.which("cc"), "host C compiler required")
class StorageTexelSpirv(unittest.TestCase):
    def test_runtime_declares_matching_format_and_bitcasts_constant_stores(self):
        source = (ANDROID / "gfx_vk_shader.c").read_text()
        start = source.index("typedef struct { const uint32_t *w;")
        end = source.index("// A variant of a fragment shader", start)
        source = "#include <stdint.h>\n#include <stdlib.h>\n#include <string.h>\n#include <stdio.h>\n" + source[start:end] + r"""
int main(int argc, char **argv) {
    if (argc != 3) return 1;
    uint32_t in[256], nout = 0;
    size_t n = fread(in, sizeof *in, 256, stdin);
    uint16_t binding = 0;
    uint8_t kind = (uint8_t)atoi(argv[1]);
    uint32_t *out = spv_int_images(in, n, &binding, &kind, 1, &nout);
    FILE *f = out ? fopen(argv[2], "wb") : NULL;
    int ok = f && fwrite(out, sizeof *out, nout, f) == nout;
    if (f && fclose(f)) ok = 0;
    free(out);
    return !ok;
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            c = root / "rewrite.c"
            exe = root / "rewrite"
            spv = root / "variant.spv"
            c.write_text(source)
            subprocess.run(["cc", "-std=gnu11", str(c), "-o", str(exe)], check=True)
            validator = shutil.which("spirv-val")
            for component, original, kind, expected, scalar in (
                ("float", 4, 5, 33, (21, (32, 0))),  # Rgba8 -> R32ui
                ("float", 4, 6, 24, (21, (32, 1))),  # Rgba8 -> R32i
                ("uint", 33, 6, 24, (21, (32, 1))),  # R32ui -> R32i
                ("float", 4, 4, 3, (22, (32,))),     # Rgba8 -> R32f
            ):
                with self.subTest(component=component, kind=kind):
                    words = module(component, original)
                    data = struct.pack(f"<{len(words)}I", *words)
                    subprocess.run([str(exe), str(kind), str(spv)], input=data, check=True)
                    ops = list(instructions(spv.read_bytes()))
                    image = next(operands for code, operands in ops if code == 61)[0]
                    decl = next(operands for code, operands in ops if code == 25 and operands[0] == image)
                    self.assertEqual(decl[7], expected)
                    self.assertIn((scalar[0], (decl[1], *scalar[1])), ops)
                    value = next(args for code, args in ops if code == 99)[2]
                    if kind == 4:
                        self.assertEqual(value, 12)
                    else:
                        self.assertTrue(any(code == 124 and args[1:] == (value, 12) for code, args in ops))
                    if validator:
                        subprocess.run([validator, "--target-env", "vulkan1.3", str(spv)], check=True)
