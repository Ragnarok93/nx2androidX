// Tracks allocated bytes until actual destruction, not logical retirement.
#ifndef NX_RESOLUTION_BUDGET_H
#define NX_RESOLUTION_BUDGET_H
#include <stdint.h>
#include <stdatomic.h>
typedef struct { _Atomic uint64_t bytes; } nx_rs_budget;
static inline int nx_rs_reserve(nx_rs_budget *b, uint64_t bytes, uint64_t limit) {
    uint64_t used = atomic_load(&b->bytes);
    if (!bytes) return 0;
    do {
        if (used > limit || bytes > limit - used) return 0;
    } while (!atomic_compare_exchange_weak(&b->bytes, &used, used + bytes));
    return 1;
}
static inline void nx_rs_release(nx_rs_budget *b, uint64_t bytes) {
    atomic_fetch_sub(&b->bytes, bytes);
}
#endif
