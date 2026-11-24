#ifndef DM_DRIVER_SELECTION_H
#define DM_DRIVER_SELECTION_H
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
enum { DM_DRIVER_LEGACY, DM_DRIVER_SYSTEM, DM_DRIVER_CUSTOM, DM_DRIVER_INVALID };
// Marker contents are either "system" or drivers/<32 lowercase hex>/<safe .so basename>.
// Absence preserves the old APK's embedded default. Invalid selections fail to system.
static inline int dm_driver_selection(const char *internal, char *out, size_t cap) {
    char marker[1024], value[384];
    if (!internal || !*internal || snprintf(marker, sizeof marker, "%s/driver-selection", internal) >= (int)sizeof marker)
        return DM_DRIVER_INVALID;
    FILE *f = fopen(marker, "rb");
    if (!f) return errno == ENOENT ? DM_DRIVER_LEGACY : DM_DRIVER_INVALID;
    size_t n = fread(value, 1, sizeof value - 1, f);
    int bad = ferror(f) || !feof(f);
    fclose(f);
    if (bad || !n || memchr(value, 0, n)) return DM_DRIVER_INVALID;
    while (n && (value[n-1] == '\n' || value[n-1] == '\r')) n--;
    value[n] = 0;
    if (!strcmp(value, "system")) return DM_DRIVER_SYSTEM;
    if (n < 45 || strncmp(value, "drivers/", 8) || value[40] != '/') return DM_DRIVER_INVALID;
    for (int i = 8; i < 40; i++) if (!((value[i] >= '0' && value[i] <= '9') ||
                                         (value[i] >= 'a' && value[i] <= 'f'))) return DM_DRIVER_INVALID;
    const char *name = value + 41;
    if (*name == '.' || strcmp(value + n - 3, ".so")) return DM_DRIVER_INVALID;
    for (const char *p = name; *p; p++)
        if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
              (*p >= '0' && *p <= '9') || *p == '.' || *p == '_' || *p == '-')) return DM_DRIVER_INVALID;
    if (snprintf(out, cap, "%s/%s", internal, value) >= (int)cap || access(out, R_OK))
        return DM_DRIVER_INVALID;
    return DM_DRIVER_CUSTOM;
}
#endif
