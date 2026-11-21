#ifndef GFX_VK_COMPUTE_HISTORY_H
#define GFX_VK_COMPUTE_HISTORY_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

enum { VK_COMPUTE_HISTORY_MAX = 96, VK_COMPUTE_HISTORY_STARTUP = 16 };
typedef struct { uint64_t word[3]; } vk_compute_key;
static const char vk_compute_history_magic[] = "DMVKCH1";

static inline unsigned vk_compute_history_add(vk_compute_key *keys, unsigned count,
                                               const vk_compute_key *key) {
    if (count >= VK_COMPUTE_HISTORY_MAX || !key->word[0] || (key->word[1] & key->word[2])) return count;
    for (unsigned i = 0; i < count; i++)
        if (!memcmp(keys[i].word, key->word, sizeof(key->word))) return count;
    keys[count] = *key;
    return count + 1;
}

// Leave the earliest startup variants in place. A slow miss can displace
// the oldest other record after the bounded history reaches capacity.
static inline int vk_compute_history_observe(vk_compute_key *keys, unsigned *count,
                                             const vk_compute_key *key, int slow) {
    if (!key->word[0] || (key->word[1] & key->word[2])) return 0;
    if (*count < VK_COMPUTE_HISTORY_MAX) {
        unsigned old = *count;
        *count = vk_compute_history_add(keys, old, key);
        return *count != old;
    }
    if (!slow) return 0;
    for (unsigned i = 0; i < *count; i++)
        if (!memcmp(keys[i].word, key->word, sizeof(key->word))) return 0;
    memmove(keys + VK_COMPUTE_HISTORY_STARTUP, keys + VK_COMPUTE_HISTORY_STARTUP + 1,
            (*count - VK_COMPUTE_HISTORY_STARTUP - 1) * sizeof(*keys));
    keys[*count - 1] = *key;
    return 1;
}

// Built-in profile observations are separate from the player's saved cache.
static inline unsigned vk_compute_history_seeds(const char *list, vk_compute_key *keys) {
    unsigned count = 0;
    while (list && *list && count < VK_COMPUTE_HISTORY_STARTUP) {
        unsigned long long shader, um, im;
        int used = 0;
        if (sscanf(list, "%16llx:%16llx:%16llx%n", &shader, &um, &im, &used) == 3 &&
            used > 0 && (!list[used] || list[used] == ',')) {
            vk_compute_key key = {{shader, um, im}};
            count = vk_compute_history_add(keys, count, &key);
        }
        list = strchr(list, ',');
        if (list) list++;
    }
    return count;
}

static inline unsigned vk_compute_history_read(FILE *f, vk_compute_key *keys) {
    char magic[sizeof vk_compute_history_magic];
    if (fread(magic, sizeof magic, 1, f) != 1 ||
        memcmp(magic, vk_compute_history_magic, sizeof magic)) return 0;
    unsigned count = 0;
    for (unsigned i = 0; i < VK_COMPUTE_HISTORY_MAX; i++) {
        vk_compute_key key;
        if (fread(&key, sizeof key, 1, f) != 1) break;
        count = vk_compute_history_add(keys, count, &key);
    }
    return count;
}

static inline int vk_compute_history_save(const char *path, const vk_compute_key *keys, unsigned count) {
    char temporary[1050];
    int len = snprintf(temporary, sizeof temporary, "%s.tmp", path);
    if (count > VK_COMPUTE_HISTORY_MAX || len < 0 || (size_t)len >= sizeof temporary) return 0;
    FILE *f = fopen(temporary, "wb");
    if (!f) return 0;
    int ok = fwrite(vk_compute_history_magic, sizeof vk_compute_history_magic, 1, f) == 1 &&
             fwrite(keys, sizeof *keys, count, f) == count;
    if (fclose(f) != 0) ok = 0;
    if (ok && rename(temporary, path) == 0) return 1;
    unlink(temporary);
    return 0;
}
#endif
