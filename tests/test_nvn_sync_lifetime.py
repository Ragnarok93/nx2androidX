"""NVN lifetime regression: queued fences cannot signal freed/reused guest syncs."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
HEADER = ROOT / "nxnative/dm/gfx/nvn"
if not HEADER.is_dir():  # standalone native harness
    HEADER = pathlib.Path(__file__).resolve().parent

SOURCE = r'''
#include "nvn_sync_registry.h"
#include <assert.h>

static nx_sync_registry registry = NX_SYNC_REGISTRY_INIT;
static uint64_t stale;
static void *late_replay(void *unused) {
    (void)unused;
    for (int i = 0; i < 100000; ++i)
        assert(!nx_sync_publish(&registry, stale, 1, 999));
    return NULL;
}
int main(void) {
    uint64_t fence;
    stale = nx_sync_create(&registry);
    assert(stale && !nx_sync_snapshot(&registry, stale, &fence));
    assert(nx_sync_publish(&registry, stale, 1, 42));
    assert(nx_sync_snapshot(&registry, stale, &fence) == 1 && fence == 42);
    nx_sync_destroy(&registry, stale);
    uint64_t replacement = nx_sync_create(&registry);
    assert(replacement && replacement != stale);
    assert(!nx_sync_publish(&registry, stale, 1, 99));
    assert(!nx_sync_snapshot(&registry, replacement, &fence));
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, late_replay, NULL));
    for (uint64_t i = 0; i < 100000; ++i) {
        assert(nx_sync_publish(&registry, replacement, 2, i));
        assert(nx_sync_snapshot(&registry, replacement, &fence) == 2 && fence == i);
    }
    assert(!pthread_join(thread, NULL));
    nx_sync_destroy(&registry, replacement);
    assert(!nx_sync_snapshot(&registry, replacement, &fence));
    assert(!nx_sync_publish(&registry, replacement, 1, 10));
    return 0;
}
'''

class NvnSyncLifetimeTests(unittest.TestCase):
    def test_retired_token_and_concurrent_replay(self):
        with tempfile.TemporaryDirectory() as d:
            src = pathlib.Path(d) / "test.c"
            exe = pathlib.Path(d) / "test"
            src.write_text(SOURCE)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pthread",
                            "-I", str(HEADER), str(src), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

if __name__ == "__main__":
    unittest.main()
