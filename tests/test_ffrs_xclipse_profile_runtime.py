"""Execute the actual production device-specific profile function with stubbed Android properties."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
PROFILE = ROOT / "nxnative/dm/android/front_profile.h"

@unittest.skipUnless(shutil.which("cc"), "C compiler required")
class FFRSXclipseProfileRuntime(unittest.TestCase):
    def test_no_speculative_defaults_and_preserves_explicit_overrides(self):
        src = PROFILE.read_text(encoding="utf-8")
        fn = src[src.index("static inline void tune_ffrs_s25fe_radv(void) {"):
                 src.index("#endif")]
        code = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PROP_VALUE_MAX 128
#define LOG(...) ((void)0)
static char phone[PROP_VALUE_MAX] = "SM-S731U";
int __system_property_get(const char *name, char *out) {
    assert(strcmp(name, "ro.product.model") == 0);
    strcpy(out, phone); return (int)strlen(phone);
}
''' + fn + r'''
static void clean(void) {
    const char *keys[] = {"DM_FFRS_S25FE_RADV_TUNE","DM_VK_DRIVER","DM_FPS_TARGET",
                          "DM_OPMODE","RADV_XCLIPSE_DCC_SMALL","DM_VK_RP",
                          "RADV_XCLIPSE_DLC","DM_VK_MT_REPLAY","DM_VK_ASYNC_PIPE",
                          "DM_VK_LRZ_TOUCH","DM_VK_D24_FLOAT_COPY","DM_VK_UNCACHED_MIP_INIT"};
    for (unsigned i=0; i<sizeof keys/sizeof keys[0]; ++i) unsetenv(keys[i]);
}
int main(void) {
    clean();
    setenv("DM_VK_DRIVER","libvulkan_radeon.so",1);
    tune_ffrs_s25fe_radv();
    assert(getenv("DM_FPS_TARGET")==NULL); // opt-in needed
    assert(getenv("DM_VK_ASYNC_PIPE")==NULL);
    assert(getenv("DM_VK_UNCACHED_MIP_INIT")==NULL);
    setenv("DM_FFRS_S25FE_RADV_TUNE","1",1);
    strcpy(phone,"SM-S938B");
    tune_ffrs_s25fe_radv();
    assert(getenv("DM_FPS_TARGET")==NULL); // other Samsung models unaffected
    assert(getenv("DM_VK_ASYNC_PIPE")==NULL);
    assert(getenv("DM_VK_UNCACHED_MIP_INIT")==NULL);
    strcpy(phone,"SM-S731U");
    setenv("DM_VK_DRIVER","libvulkan_adreno.so",1);
    tune_ffrs_s25fe_radv();
    assert(getenv("DM_FPS_TARGET")==NULL); // Adreno path unaffected
    assert(getenv("DM_VK_ASYNC_PIPE")==NULL);
    assert(getenv("DM_VK_UNCACHED_MIP_INIT")==NULL);
    setenv("DM_VK_DRIVER","libvulkan_radeon.so",1);
    assert(getenv("DM_VK_LRZ_TOUCH")==NULL);
    assert(getenv("DM_VK_D24_FLOAT_COPY")==NULL);
    tune_ffrs_s25fe_radv();
    assert(!strcmp(getenv("DM_FPS_TARGET"),"30"));
    assert(!strcmp(getenv("DM_VK_ASYNC_PIPE"),"0"));
    assert(!strcmp(getenv("DM_VK_LRZ_TOUCH"),"0"));
    assert(!strcmp(getenv("DM_VK_UNCACHED_MIP_INIT"),"1"));
    assert(getenv("DM_VK_D24_FLOAT_COPY")==NULL);
    assert(!getenv("DM_OPMODE") && !getenv("RADV_XCLIPSE_DCC_SMALL"));
    assert(!getenv("DM_VK_RP") && !getenv("RADV_XCLIPSE_DLC"));
    assert(!getenv("DM_VK_MT_REPLAY"));
    setenv("DM_FPS_TARGET","60",1);
    setenv("DM_VK_ASYNC_PIPE","1",1);
    setenv("DM_VK_LRZ_TOUCH","14",1);
    setenv("DM_VK_UNCACHED_MIP_INIT","0",1);
    setenv("DM_VK_D24_FLOAT_COPY","0",1);
    setenv("DM_OPMODE","1",1);
    setenv("RADV_XCLIPSE_DCC_SMALL","1",1);
    setenv("DM_VK_RP","1",1);
    setenv("RADV_XCLIPSE_DLC","0",1);
    setenv("DM_VK_MT_REPLAY","1",1);
    tune_ffrs_s25fe_radv();
    assert(!strcmp(getenv("DM_FPS_TARGET"),"60"));
    assert(!strcmp(getenv("DM_VK_ASYNC_PIPE"),"1"));
    assert(!strcmp(getenv("DM_VK_LRZ_TOUCH"),"14"));
    assert(!strcmp(getenv("DM_VK_UNCACHED_MIP_INIT"),"0"));
    assert(!strcmp(getenv("DM_VK_D24_FLOAT_COPY"),"0"));
    assert(!strcmp(getenv("DM_OPMODE"),"1"));
    assert(!strcmp(getenv("RADV_XCLIPSE_DCC_SMALL"),"1"));
    assert(!strcmp(getenv("DM_VK_RP"),"1"));
    assert(!strcmp(getenv("RADV_XCLIPSE_DLC"),"0"));
    assert(!strcmp(getenv("DM_VK_MT_REPLAY"),"1"));
    clean(); return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="ffrs-profile-") as d:
            p = Path(d)
            (p / "test.c").write_text(code)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            str(p / "test.c"), "-o", str(p / "test")], check=True)
            subprocess.run([str(p / "test")], check=True, timeout=10)

if __name__ == "__main__":
    unittest.main()
