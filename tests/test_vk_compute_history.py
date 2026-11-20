"""Exercise the exact on-disk compute warmup history used by the renderer."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ANDROID = Path(__file__).resolve().parents[1] / "nxnative/dm/android"


@unittest.skipUnless(shutil.which("cc"), "host C compiler required")
class ComputeHistoryTests(unittest.TestCase):
    def test_variants_round_trip_deduplicate_and_cap(self):
        source = r'''
#include <assert.h>
#include "gfx_vk_compute_history.h"

int main(int argc, char **argv) {
    assert(argc == 2);
    vk_compute_key written[VK_COMPUTE_HISTORY_MAX], loaded[VK_COMPUTE_HISTORY_MAX];
    vk_compute_key base = {{0x5854663e6002c250, 0, 0}};
    vk_compute_key variant = {{0x5854663e6002c250, 6, 8}};
    vk_compute_key invalid = {{1, 6, 2}};
    unsigned n = vk_compute_history_add(written, 0, &base);
    n = vk_compute_history_add(written, n, &variant);
    assert(n == 2);
    assert(vk_compute_history_add(written, n, &variant) == n);
    assert(vk_compute_history_add(written, n, &invalid) == n);
    for (uint64_t i = 1; i <= 200; i++) {
        vk_compute_key k = {{i, 0, 0}};
        n = vk_compute_history_add(written, n, &k);
    }
    assert(n == VK_COMPUTE_HISTORY_MAX);
    assert(vk_compute_history_save(argv[1], written, n));
    assert(!vk_compute_history_save(argv[1], written, n + 1));
    FILE *f = fopen(argv[1], "rb");
    assert(f && vk_compute_history_read(f, loaded) == n);
    assert(!memcmp(written, loaded, n * sizeof(*loaded)));
    fclose(f);
    assert(vk_compute_history_save(argv[1], written, 2));
    f = fopen(argv[1], "ab");
    assert(f && fputc(1, f) != EOF && fclose(f) == 0);
    f = fopen(argv[1], "rb");
    assert(f && vk_compute_history_read(f, loaded) == 2); // torn final key ignored
    fclose(f);
    f = fopen(argv[1], "wb");
    assert(f && fputs("invalid", f) >= 0 && fclose(f) == 0);
    f = fopen(argv[1], "rb");
    assert(f && vk_compute_history_read(f, loaded) == 0);
    fclose(f);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path / "case.c").write_text(source)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ANDROID), str(path / "case.c"), "-o", str(path / "case")],
                           check=True)
            subprocess.run([str(path / "case"), str(path / "history.bin")], check=True, timeout=10)
