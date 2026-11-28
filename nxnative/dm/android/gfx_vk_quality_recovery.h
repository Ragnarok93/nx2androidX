#pragma once
#include <stdatomic.h>
#include <stdint.h>
// One cross-thread recovery window. This is an admission lease, not a GPU
// fence or a resource owner. Only quality increases consume it; emergency
// fallbacks and explicit user changes never wait for it.
typedef struct { _Atomic uint64_t until_ns; } dm_quality_recovery;
static inline int dm_quality_recovery_claim(dm_quality_recovery *p,uint64_t now,unsigned hold_ms) {
    if(!hold_ms)return 0;
    uint64_t duration=(uint64_t)hold_ms*UINT64_C(1000000);
    uint64_t end=now>UINT64_MAX-duration?UINT64_MAX:now+duration;
    uint64_t prev=atomic_load_explicit(&p->until_ns,memory_order_relaxed);
    if(prev>now || end<=now)return 0;
    return atomic_compare_exchange_strong_explicit(&p->until_ns,&prev,end,
        memory_order_acq_rel,memory_order_relaxed);
}
// Downward LSFG transitions do not need admission, but prevent another
// quality recovery from starting while their pending graph is prepared.
static inline void dm_quality_recovery_hold(dm_quality_recovery *p,uint64_t now,unsigned hold_ms) {
    uint64_t duration=(uint64_t)hold_ms*UINT64_C(1000000);
    uint64_t end=now>UINT64_MAX-duration?UINT64_MAX:now+duration;
    uint64_t prev=atomic_load_explicit(&p->until_ns,memory_order_relaxed);
    while(prev<end&&!atomic_compare_exchange_weak_explicit(&p->until_ns,&prev,end,
        memory_order_acq_rel,memory_order_relaxed)){}
}
