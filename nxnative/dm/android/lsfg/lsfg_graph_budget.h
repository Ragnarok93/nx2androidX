#pragma once
#include <stdint.h>
// Count the actual Vulkan memory requirement, not nominal pixel area.
// Driver-internal pipeline and descriptor memory is not represented here.
#define LSFG_GRAPH_MAX_HEAPS 16u
typedef struct {uint64_t limit,used,heap_used[LSFG_GRAPH_MAX_HEAPS],heap_limit[LSFG_GRAPH_MAX_HEAPS]; int heap_limited;} lsfg_graph_budget;
static inline int lsfg_graph_fits(const lsfg_graph_budget *b,uint64_t bytes) {
    return b->used<=b->limit && bytes<=b->limit-b->used;
}
static inline int lsfg_graph_charge(lsfg_graph_budget *b,uint64_t bytes) {
    if (!lsfg_graph_fits(b,bytes)) return 0;
    b->used+=bytes;
    return 1;
}
// Each allocation belongs to the selected memory type's heap. No credit is
// transferable across heaps, including heaps sharing physical RAM.
static inline int lsfg_graph_fits_heap(const lsfg_graph_budget *b,unsigned heap,uint64_t bytes) {
    return heap<LSFG_GRAPH_MAX_HEAPS && lsfg_graph_fits(b,bytes) &&
        bytes<=UINT64_MAX-b->heap_used[heap] &&
        (!b->heap_limited || (b->heap_used[heap]<=b->heap_limit[heap] &&
         bytes<=b->heap_limit[heap]-b->heap_used[heap]));
}
static inline int lsfg_graph_charge_heap(lsfg_graph_budget *b,unsigned heap,uint64_t bytes) {
    if (!lsfg_graph_fits_heap(b,heap,bytes)) return 0;
    b->heap_used[heap]+=bytes;
    b->used+=bytes;
    return 1;
}
