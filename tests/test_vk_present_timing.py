"""Exercise the Android presentation history without a Vulkan device."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


SOURCE = Path(__file__).resolve().parents[1] / 'nxnative/dm/android/gfx_vk_present.c'


class PresentTiming(unittest.TestCase):
    def test_surface_query_failure_retries_without_consuming_invalid_formats(self):
        cc = shutil.which('cc') or shutil.which('clang')
        if not cc:
            self.skipTest('a C compiler is needed for the surface query check')
        src = SOURCE.read_text(encoding='utf-8')
        start = src.index('    VkSurfaceCapabilitiesKHR caps;', src.index('static int swap_make(void)'))
        query = src[start:src.index('    if (g_sfmt != g_want)', start)]
        harness = r'''
#include <assert.h>
#include <vulkan/vulkan_core.h>
static struct { VkPhysicalDevice pd; } V;
static VkSurfaceKHR g_surf;
static VkFormat g_sfmt;
static const VkFormat g_want = VK_FORMAT_R8G8B8A8_SRGB;
static VkResult cap_result, format_result;
static unsigned available, queried;

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice d, VkSurfaceKHR s, VkSurfaceCapabilitiesKHR *caps) {
    (void)d; (void)s; (void)caps;
    return cap_result;
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceFormatsKHR(
    VkPhysicalDevice d, VkSurfaceKHR s, uint32_t *count, VkSurfaceFormatKHR *formats) {
    (void)d; (void)s;
    queried++;
    if (format_result < 0) return format_result;
    assert(*count >= available);
    *count = available;
    if (available) formats[0].format = g_want;
    return format_result;
}
static int read_surface(void) {
''' + query + r'''
    return 1;
surface_unavailable:
    return 0;
}
int main(void) {
    cap_result = VK_ERROR_SURFACE_LOST_KHR;
    assert(!read_surface() && !queried);
    cap_result = VK_SUCCESS;
    format_result = VK_ERROR_SURFACE_LOST_KHR;
    assert(!read_surface() && queried == 1 && g_sfmt == VK_FORMAT_UNDEFINED);
    format_result = VK_SUCCESS;
    assert(!read_surface() && queried == 2 && g_sfmt == VK_FORMAT_UNDEFINED);
    available = 1;
    assert(read_surface() && queried == 3 && g_sfmt == g_want);
}
'''
        headers = SOURCE.parents[1] / 'third_party/vulkan-headers/include'
        with tempfile.TemporaryDirectory(prefix='nx2a_surface_query_') as tmp:
            c, exe = Path(tmp) / 'check.c', Path(tmp) / 'check'
            c.write_text(harness, encoding='utf-8')
            subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(headers),
                            str(c), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_swapchain_recreated_on_window_resize(self):
        cc = shutil.which('cc') or shutil.which('clang')
        if not cc:
            self.skipTest('a C compiler is needed for the resize check')
        src = SOURCE.read_text(encoding='utf-8')
        start = src.index('static int swap_make(void)')
        prefix = src[start:src.index('    VkAndroidSurfaceCreateInfoKHR', start)]
        harness = r'''
#include <assert.h>
#include <stdint.h>
typedef struct { int width, height; } ANativeWindow;
static ANativeWindow *g_win, *current;
static int g_swap, destroys;
static struct { uint32_t width, height; } g_sext;
static ANativeWindow *dm_front_window(void) { return current; }
static int ANativeWindow_getWidth(ANativeWindow *w) { return w->width; }
static int ANativeWindow_getHeight(ANativeWindow *w) { return w->height; }
static void swap_destroy(void) { g_swap = 0; destroys++; }
''' + prefix + r'''
    return 2;
}
int main(void) {
    ANativeWindow w = {3200, 1440}; current = g_win = &w;
    g_swap = 1; g_sext.width = 3200; g_sext.height = 1440;
    assert(swap_make() == 1 && destroys == 0);
    w.width = 1440; w.height = 3200;
    assert(swap_make() == 2 && destroys == 1);
    g_swap = 1; g_sext.width = 1440; g_sext.height = 3200;
    w.width = 3200; w.height = 1440;
    assert(swap_make() == 2 && destroys == 2);
    g_swap = 1; w.width = 0;
    assert(swap_make() == 1 && destroys == 2);
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='nx2a_present_resize_') as tmp:
            c, exe = Path(tmp) / 'check.c', Path(tmp) / 'check'
            c.write_text(harness, encoding='utf-8')
            result = subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror', str(c), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_ticket_history(self):
        cc = shutil.which('cc') or shutil.which('clang')
        if not cc:
            self.skipTest('a C compiler is needed for the presentation history check')
        src = SOURCE.read_text(encoding='utf-8')
        history = src[src.index('enum { PRES_TIMES'):src.index('static VkSurfaceKHR g_surf')]
        harness = r'''
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
''' + history + r'''
int main(void) {
    assert(present_time_get(0) == 0);
    assert(present_time_get(1) == 0);
    present_time_begin(1, 0);
    assert(present_time_get(1) == 0);  // queued is not displayed
    present_time_map(42, 1);
    present_time_begin(2, 0);
    present_time_map(43, 2);
    present_time_record(43, 2000000000ull);  // results need not be in ticket order
    present_time_record(42, 1000000000ull);
    assert(present_time_get(1) == 1);
    assert(present_time_get(2) == 2);
    present_time_record(43, 0);
    assert(present_time_get(2) == 2);
    present_time_map(44, 0);  // a menu redraw has a Vulkan ID but no game ticket
    present_time_record(44, 3000000000ull);
    assert(present_time_get(2) == 2);
    assert(present_time_get(0) == 0);

    present_time_begin(3, 0);
    present_time_map(42 + PRES_TIMES, 3);
    present_time_record(42, 9000000000ull);  // overwritten Vulkan ID mapping
    assert(present_time_get(3) == 0);
    present_time_record(42 + PRES_TIMES, 3000000000ull);
    assert(present_time_get(3) == 3);
    present_time_begin(1 + PRES_TIMES, 0);
    present_time_map(200, 1);  // a delayed result must not overwrite a newer ticket
    present_time_record(200, 9000000000ull);
    assert(present_time_get(1) == 0);
    assert(present_time_get(1 + PRES_TIMES) == 0);

    present_time_begin(5, 7000000000ull);  // no display times: the hand-over time stands in at once
    assert(present_time_get(5) == 7);
    present_time_reset();
    assert(present_time_get(2) == 0);
    present_time_begin(3, 0);
    present_time_record(42 + PRES_TIMES, 9000000000ull);  // old swapchain feedback
    assert(present_time_get(3) == 0);
    present_time_map(UINT32_MAX, 3);
    present_time_record(UINT32_MAX, 3000000000ull);
    assert(present_time_get(3) == 3);
    present_time_begin(4, 0);
    present_time_map(0, 4);  // Vulkan's 32-bit present ID may wrap
    present_time_record(0, 4000000000ull);
    assert(present_time_get(4) == 4);
    present_time_begin(5, 0);  // failed, headless or unsupported presents get no result
    assert(present_time_get(5) == 0);
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='nx2a_present_timing_') as tmp:
            c = Path(tmp) / 'check.c'
            exe = Path(tmp) / 'check'
            c.write_text(harness, encoding='utf-8')
            result = subprocess.run([cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-pthread', str(c), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
