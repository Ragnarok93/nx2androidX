#pragma once
#include <stdint.h>
typedef struct {
    uint64_t key,stable_since;
    int valid,blocked;
} dm_fg_coordination;
// Source-time monotonic clock. A changed workload cancels pending history
// before recording. A stressed source or competing quality probe blocks
// upward recovery immediately and for four stable seconds afterwards.
static inline int dm_fg_coordination_update(dm_fg_coordination *c,uint64_t key,uint64_t now,int blocked) {
    int changed=!c->valid || c->key!=key || now<c->stable_since;
    if(changed || blocked || c->blocked)c->stable_since=now;
    c->key=key;c->valid=1;c->blocked=blocked;
    return changed;
}
static inline int dm_fg_coordination_recover(const dm_fg_coordination *c,uint64_t now) {
    return c->valid&&!c->blocked&&now>=c->stable_since&&
        now-c->stable_since>=UINT64_C(4000000000);
}
