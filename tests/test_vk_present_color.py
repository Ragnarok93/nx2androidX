"""Regression checks for async Vulkan presentation color-space selection."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[1] / "nxnative/dm/android/gfx_vk_present.c"


class PresentFormatTests(unittest.TestCase):
    def test_async_present_uses_image_format_not_uninitialized_metadata(self):
        src = SOURCE.read_text(encoding="utf-8")
        present = src[src.index("static void present_one("):src.index("static void present_window(")]
        thread = src[src.index("static void *present_thread("):src.index("static void present_thread_start(")]
        self.assertIn("present_window_format(im->fmt)", present)
        self.assertIn("if (with_meta && im)", present)
        self.assertIn("present_one(e.im, e.im->img", thread)
        self.assertNotIn("im_meta_srgb", src)
        self.assertIn("if (want != g_want)", present)

    def test_present_format_mapping(self):
        cc = shutil.which("cc") or shutil.which("clang")
        if not cc:
            self.skipTest("C compiler unavailable")
        src = SOURCE.read_text(encoding="utf-8")
        helper = src[src.index("static VkFormat present_window_format("):src.index("static void present_one(")]
        code = r"""
#include <assert.h>
typedef int VkFormat;
enum {
    VK_FORMAT_UNDEFINED = 0,
    VK_FORMAT_R8G8B8A8_UNORM = 37,
    VK_FORMAT_R8G8B8A8_SRGB = 43,
    VK_FORMAT_B8G8R8A8_UNORM = 44,
    VK_FORMAT_B8G8R8A8_SRGB = 50,
    VK_FORMAT_A8B8G8R8_SRGB_PACK32 = 57
};
""" + helper + r"""
int main(void) {
    assert(present_window_format(VK_FORMAT_R8G8B8A8_SRGB) == VK_FORMAT_R8G8B8A8_SRGB);
    assert(present_window_format(VK_FORMAT_B8G8R8A8_SRGB) == VK_FORMAT_R8G8B8A8_SRGB);
    assert(present_window_format(VK_FORMAT_A8B8G8R8_SRGB_PACK32) == VK_FORMAT_R8G8B8A8_SRGB);
    assert(present_window_format(VK_FORMAT_R8G8B8A8_UNORM) == VK_FORMAT_R8G8B8A8_UNORM);
    assert(present_window_format(VK_FORMAT_B8G8R8A8_UNORM) == VK_FORMAT_R8G8B8A8_UNORM);
    assert(present_window_format(VK_FORMAT_UNDEFINED) != VK_FORMAT_UNDEFINED);
    return 0;
}
"""
        with tempfile.TemporaryDirectory(prefix="nx2a_present_format_") as d:
            path = Path(d) / "test.c"
            exe = Path(d) / "test"
            path.write_text(code, encoding="utf-8")
            result = subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", str(path), "-o", str(exe)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
