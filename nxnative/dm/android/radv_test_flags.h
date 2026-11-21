#ifndef DM_RADV_TEST_FLAGS_H
#define DM_RADV_TEST_FLAGS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

enum { RADV_TEST_DLC, RADV_TEST_DCC, RADV_TEST_BC, RADV_TEST_HIZ, RADV_TEST_COUNT };
static const struct {
    const char *env, *property, *test_value;
} radv_test_flags[RADV_TEST_COUNT] = {
    {"RADV_XCLIPSE_DLC", "debug.radv_xclipse_dlc", "1"},
    {"RADV_XCLIPSE_DCC", "debug.radv_xclipse_dcc", "0"},
    {"RADV_XCLIPSE_NO_BC_EMU", "debug.radv_xclipse_no_bc_emu", "1"},
    {"RADV_DEBUG", "debug.radv_debug", "nohiz"},
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

// Append after any negated token and preserve the other flags. The captured
// RADV checks the Android property only when its environment variable is empty.
static inline int radv_test_disable_hiz(void) {
    const char *env = radv_test_flags[RADV_TEST_HIZ].env;
    const char *value = getenv(env);
#ifdef __ANDROID__
    char property[PROP_VALUE_MAX] = {0};
    if ((!value || !*value) &&
        __system_property_get(radv_test_flags[RADV_TEST_HIZ].property, property) > 0)
        value = property;
#endif
    const char *flag = radv_test_flags[RADV_TEST_HIZ].test_value;
    if (!value || !*value) return setenv(env, flag, 1);
    size_t n = strlen(value), f = strlen(flag);
    if (n >= f && !strcmp(value + n - f, flag) &&
        (n == f || strchr(", \n", value[n - f - 1]))) return 0;
    char *combined = malloc(n + f + 2);
    if (!combined) return -1;
    snprintf(combined, n + f + 2, "%s,%s", value, flag);
    int result = setenv(env, combined, 1);
    free(combined);
    return result;
}

// Apply before opening the driver: RADV reads these at instance/device
// creation or caches them at shader compilation. Live changes would mix modes.
static inline unsigned radv_test_apply(const char *root) {
    if (!radv_test_driver(getenv("DM_VK_DRIVER"))) return 0;
    unsigned mask = radv_test_load(root);
    for (unsigned i = 0; i < RADV_TEST_COUNT; i++) {
        if (!(mask & (1u << i))) continue;
        int failed = i == RADV_TEST_HIZ ? radv_test_disable_hiz() :
            setenv(radv_test_flags[i].env, radv_test_flags[i].test_value, 1);
        if (failed) mask &= ~(1u << i);
    }
    return mask;
}

#endif
