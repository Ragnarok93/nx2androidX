#pragma once
#include <stdint.h>
#define FG_MIB UINT64_C(1048576)
typedef struct {
    uint64_t sampled_ns,os_available,heap_available;
    int thermal; // Android thermal status, -1 unknown
} dm_fg_pressure;
// Conservative cap on *additional* explicit VkDeviceMemory. Unknown/stale
// memory vetoes all allocations; unknown thermal vetoes quality recovery.
// Keep 768 MiB OS reserve even for reductions; upward probes require >1 GiB.
static inline uint64_t dm_fg_pressure_cap(const dm_fg_pressure *s,uint64_t now,int upward) {
    if(!s->sampled_ns || now<s->sampled_ns || now-s->sampled_ns>UINT64_C(5000000000) ||
        !s->heap_available || s->os_available<=768*FG_MIB || s->thermal>=4 ||
        (upward && (s->os_available<=1024*FG_MIB || s->thermal<0 || s->thermal>=2)))return 0;
    uint64_t cap=s->heap_available/10;
    uint64_t reserve=s->os_available-768*FG_MIB;
    if(cap>128*FG_MIB)cap=128*FG_MIB;
    if(cap>reserve)cap=reserve;
    return cap;
}
