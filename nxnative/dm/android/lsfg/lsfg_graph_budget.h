#pragma once
#include <stdint.h>
// Count the actual Vulkan memory requirement, not nominal pixel area.
// Driver-internal pipeline and descriptor memory is not represented here.
typedef struct {uint64_t limit,used;} lsfg_graph_budget;
static inline int lsfg_graph_fits(const lsfg_graph_budget *b,uint64_t bytes) {
    return b->used<=b->limit && bytes<=b->limit-b->used;
}
static inline int lsfg_graph_charge(lsfg_graph_budget *b,uint64_t bytes) {
    if (!lsfg_graph_fits(b,bytes)) return 0;
    b->used+=bytes;
    return 1;
}
