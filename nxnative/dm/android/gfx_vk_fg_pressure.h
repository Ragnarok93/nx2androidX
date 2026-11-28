#pragma once
#include <stdint.h>
#define FG_MIB UINT64_C(1048576)
#define DM_FG_MAX_HEAPS 16u
// Raw per-heap budget/usage from one physical-device query. Keep this separate
// from the compact per-frame report; the capture ring does not copy 16 heaps.
typedef struct {
    uint64_t sampled_ns,os_available;
    int thermal;
    unsigned count,eligible;
    uint64_t budget[DM_FG_MAX_HEAPS],usage[DM_FG_MAX_HEAPS];
} dm_fg_heap_snapshot;
static inline uint64_t dm_fg_heap_headroom(const dm_fg_heap_snapshot *s,unsigned heap) {
    return heap<s->count && heap<DM_FG_MAX_HEAPS && (s->eligible&(1u<<heap)) &&
        s->budget[heap]>s->usage[heap] ? s->budget[heap]-s->usage[heap] : 0;
}
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

// Raw background observation and its derived limits, not an allocation
// reservation. Keep stale observations visible while vetoing their use.
typedef struct {
    uint64_t sampled_ns,age_ns,os_available,heap_available,down_cap,up_cap;
    int fresh,thermal;
} dm_fg_pressure_report;
// Only a fresh, affirmative critical signal suppresses reconstruction.
// Missing telemetry is not evidence of memory or thermal exhaustion.
static inline int dm_recon_pressure_critical(const dm_fg_pressure_report *r) {
    return r->fresh && (r->thermal>=4 ||
        (r->os_available && r->os_available<=768*FG_MIB));
}
static inline dm_fg_pressure_report dm_fg_pressure_describe(const dm_fg_pressure *s,uint64_t now) {
    dm_fg_pressure_report r={.sampled_ns=s->sampled_ns,.os_available=s->os_available,
        .heap_available=s->heap_available,.thermal=s->sampled_ns?s->thermal:-1,
        .age_ns=s->sampled_ns&&now>=s->sampled_ns?now-s->sampled_ns:UINT64_MAX};
    r.fresh=r.age_ns<=UINT64_C(5000000000);
    r.down_cap=dm_fg_pressure_cap(s,now,0);r.up_cap=dm_fg_pressure_cap(s,now,1);
    return r;
}
