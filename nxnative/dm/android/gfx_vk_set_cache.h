#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
// Keys are immutable for one submission; sets remain owned by its pool.
typedef struct { uint64_t h, epoch; VkDescriptorSet set; size_t off, n; } vk_set_cache_entry;
static inline vk_set_cache_entry *vk_set_cache_find(vk_set_cache_entry *entries, size_t count,
        const uint64_t *arena, uint64_t epoch, uint64_t hash, const uint64_t *key, size_t n,
        VkDescriptorSet *hit) {
    enum { WAYS = 4 };
    *hit = (VkDescriptorSet)0;
    if (!n || count < WAYS || count % WAYS) return NULL;
    vk_set_cache_entry *bucket = entries + (hash % (count / WAYS)) * WAYS;
    vk_set_cache_entry *empty = NULL;
    for (unsigned i=0; i<WAYS; ++i) {
        vk_set_cache_entry *e = &bucket[i];
        if (e->epoch != epoch || !e->set) { if (!empty) empty=e; continue; }
        if (e->h == hash && e->n == n && !memcmp(arena + e->off,key,n*sizeof *key)) {
            *hit=e->set; return e;
        }
    }
    // Keep admitted keys alive rather than evicting them and consuming more
    // arena space on the next encounter. Full buckets safely bypass reuse.
    return empty;
}
