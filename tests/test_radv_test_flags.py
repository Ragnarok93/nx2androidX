"""Exercise the restart-only flag storage and startup application."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

import host_cc


ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(host_cc.posix(), "POSIX C compiler required")
class RadvTestFlags(unittest.TestCase):
    def test_round_trip_and_driver_gate(self):
        source = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include "nxnative/dm/android/radv_test_flags.h"

int main(int argc, char **argv) {
    assert(argc == 2);
    const char *root = argv[1];
    unsigned mask = (1u << RADV_TEST_DLC) | (1u << RADV_TEST_BC);
    assert(!radv_test_load(root) && radv_test_save(root, mask));
    assert(radv_test_load(root) == mask && !radv_test_save(root, 999));
    setenv("DM_VK_DRIVER", "libvulkan.freedreno.so", 1);
    assert(!radv_test_apply(root) && !getenv("RADV_XCLIPSE_DLC"));
    setenv("RADV_XCLIPSE_DCC", "0", 1);
    setenv("DM_VK_DRIVER", "libvulkan.radeon.so", 1);
    assert(radv_test_apply(root) == mask);
    assert(!strcmp(getenv("RADV_XCLIPSE_DLC"), "1"));
    assert(!strcmp(getenv("RADV_XCLIPSE_NO_BC_EMU"), "1"));
    assert(!strcmp(getenv("RADV_XCLIPSE_DCC"), "0"));
    assert(!getenv("RADV_DEBUG"));
    mask |= 1u << RADV_TEST_HIZ;
    assert(radv_test_save(root, mask));
    assert(radv_test_apply(root) == mask && !strcmp(getenv("RADV_DEBUG"), "nohiz"));
    assert(!setenv("RADV_DEBUG", "nodcc,~nohiz", 1));
    assert(radv_test_apply(root) == mask && radv_test_apply(root) == mask);
    assert(!strcmp(getenv("RADV_DEBUG"), "nodcc,~nohiz,nohiz"));
    assert(radv_test_save(root, 0) && !radv_test_load(root));
    char path[620];
    assert(radv_test_path(path, sizeof path, root));
    FILE *corrupt = fopen(path, "w");
    assert(corrupt);
    fputs("1 extra data", corrupt);
    fclose(corrupt);
    assert(!radv_test_load(root));
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path / "flags.c").write_text(source, encoding="utf-8")
            exe = path / "flags"
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT), str(path / "flags.c"), "-o", str(exe)], check=True)
            env = {key: value for key, value in os.environ.items() if not key.startswith("RADV_XCLIPSE_") and key != "RADV_DEBUG"}
            subprocess.run([str(exe), tmp], check=True, env=env)
