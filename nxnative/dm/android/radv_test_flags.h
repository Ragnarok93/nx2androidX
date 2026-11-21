#ifndef DM_RADV_TEST_FLAGS_H
#define DM_RADV_TEST_FLAGS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum { RADV_TEST_DLC, RADV_TEST_DCC, RADV_TEST_BC, RADV_TEST_COUNT };
static const struct {
    const char *env, *property, *test_value;
} radv_test_flags[RADV_TEST_COUNT] = {
    {"RADV_XCLIPSE_DLC", "debug.radv_xclipse_dlc", "1"},
    {"RADV_XCLIPSE_DCC", "debug.radv_xclipse_dcc", "0"},
    {"RADV_XCLIPSE_NO_BC_EMU", "debug.radv_xclipse_no_bc_emu", "1"},
};

static inline int radv_test_driver(const char *driver) {
    return driver && (strstr(driver, "radeon") || strstr(driver, "radv"));
}

static inline int radv_test_path(char *path, size_t cap, const char *root) {
    int n = snprintf(path, cap, "%s/radv-test-flags.txt", root);
    return n > 0 && (size_t)n < cap;
}

static inline unsigned radv_test_load(const char *root) {
    char path[620];
    FILE *file = radv_test_path(path, sizeof path, root) ? fopen(path, "r") : NULL;
    if (!file) return 0;
    unsigned mask = 0;
    char extra;
    if (fscanf(file, "%u %c", &mask, &extra) != 1 || mask >= (1u << RADV_TEST_COUNT)) mask = 0;
    fclose(file);
    return mask;
}

static inline int radv_test_save(const char *root, unsigned mask) {
    char path[620], temp[640];
    if (!radv_test_path(path, sizeof path, root) || mask >= (1u << RADV_TEST_COUNT)) return 0;
    snprintf(temp, sizeof temp, "%s.tmp.XXXXXX", path);
    int fd = mkstemp(temp);
    if (fd < 0) return 0;
    FILE *file = fdopen(fd, "w");
    int ok = file && fprintf(file, "%u\n", mask) > 0 && fflush(file) == 0 && fsync(fd) == 0;
    if (file ? fclose(file) != 0 : close(fd) != 0) ok = 0;
    if (ok && rename(temp, path) == 0) return 1;
    unlink(temp);
    return 0;
}

// Apply before opening the driver: RADV reads these at instance/device
// creation or caches them at shader compilation. Live changes would mix modes.
static inline unsigned radv_test_apply(const char *root) {
    if (!radv_test_driver(getenv("DM_VK_DRIVER"))) return 0;
    unsigned mask = radv_test_load(root);
    for (unsigned i = 0; i < RADV_TEST_COUNT; i++) {
        if ((mask & (1u << i)) && setenv(radv_test_flags[i].env, radv_test_flags[i].test_value, 1))
            mask &= ~(1u << i);
    }
    return mask;
}

#endif
