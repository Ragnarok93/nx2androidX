// Host-owned sync state. Recorded GPU work carries a lifetime token, never a
// guest pointer: a finalized sync may be freed or initialized at the same address.
#pragma once
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

#define NX_SYNC_BUCKETS 256u

typedef struct nx_sync_entry {
    struct nx_sync_entry *next;
    uint64_t token, fence;
    unsigned kind;
} nx_sync_entry;

typedef struct {
    pthread_mutex_t mu;
    uint64_t next_token;
    nx_sync_entry *bucket[NX_SYNC_BUCKETS];
} nx_sync_registry;

#define NX_SYNC_REGISTRY_INIT { .mu = PTHREAD_MUTEX_INITIALIZER }

// Caller holds mu. Token 0 never names a live sync.
static inline nx_sync_entry **nx_sync_slot(nx_sync_registry *r, uint64_t token) {
    nx_sync_entry **p = &r->bucket[token % NX_SYNC_BUCKETS];
    while (*p && (*p)->token != token) p = &(*p)->next;
    return p;
}

static inline uint64_t nx_sync_create(nx_sync_registry *r) {
    nx_sync_entry *e = calloc(1, sizeof *e);
    if (!e) return 0;
    pthread_mutex_lock(&r->mu);
    if (r->next_token == UINT64_MAX) {
        pthread_mutex_unlock(&r->mu); free(e); return 0;
    }
    uint64_t token = ++r->next_token;
    e->token = token;
    unsigned b = (unsigned)(token % NX_SYNC_BUCKETS);
    e->next = r->bucket[b]; r->bucket[b] = e;
    pthread_mutex_unlock(&r->mu);
    return token;
}

static inline void nx_sync_destroy(nx_sync_registry *r, uint64_t token) {
    if (!token) return;
    pthread_mutex_lock(&r->mu);
    nx_sync_entry **p = nx_sync_slot(r, token), *e = *p;
    if (e) *p = e->next;
    pthread_mutex_unlock(&r->mu);
    free(e);
}

static inline int nx_sync_publish(nx_sync_registry *r, uint64_t token, unsigned kind, uint64_t fence) {
    if (!token || !kind) return 0;
    pthread_mutex_lock(&r->mu);
    nx_sync_entry *e = *nx_sync_slot(r, token);
    int live = e != NULL;
    if (e) { e->fence = fence; e->kind = kind; }
    pthread_mutex_unlock(&r->mu);
    return live;
}

// Return a coherent snapshot; fence 0 is valid for an already completed acquire.
static inline unsigned nx_sync_snapshot(nx_sync_registry *r, uint64_t token, uint64_t *fence) {
    *fence = 0;
    if (!token) return 0;
    pthread_mutex_lock(&r->mu);
    nx_sync_entry *e = *nx_sync_slot(r, token);
    unsigned kind = e ? e->kind : 0;
    if (e) *fence = e->fence;
    pthread_mutex_unlock(&r->mu);
    return kind;
}
