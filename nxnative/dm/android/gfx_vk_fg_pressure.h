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

enum { DM_FG_HEAP_OK, DM_FG_HEAP_STALE, DM_FG_HEAP_PRESSURE, DM_FG_HEAP_UNKNOWN,
       DM_FG_HEAP_RECLAIM, DM_FG_HEAP_CAP };
typedef struct { unsigned reason; uint64_t total,growth; } dm_fg_heap_admission;
// Reclaim only the active graph's proven bytes on the same heap. A snapshot
// preceding graph publication cannot prove those bytes were in heapUsage.
static inline dm_fg_heap_admission dm_fg_heap_admit(const dm_fg_heap_snapshot *s,uint64_t now,
        const uint64_t *required,const uint64_t *reclaim,uint64_t published_ns,int upward) {
    dm_fg_heap_admission p={.reason=DM_FG_HEAP_STALE};
    if(!s->sampled_ns||now<s->sampled_ns||now-s->sampled_ns>UINT64_C(5000000000))return p;
    uint64_t reserve=(upward?1024:768)*FG_MIB;
    p.reason=DM_FG_HEAP_PRESSURE;
    if(s->os_available<=reserve||s->thermal>=4||(upward&&(s->thermal<0||s->thermal>=2)))return p;
    p.reason=DM_FG_HEAP_UNKNOWN;
    if(!required||!s->count||s->count>DM_FG_MAX_HEAPS)return p;
    uint64_t cap=128*FG_MIB;
    if(cap>s->os_available-reserve)cap=s->os_available-reserve;
    for(unsigned i=0;i<DM_FG_MAX_HEAPS;i++) {
        uint64_t need=required[i],credit=reclaim?reclaim[i]:0;
        if(!need&&!credit)continue;
        p.reason=DM_FG_HEAP_UNKNOWN;
        if(i>=s->count||!(s->eligible&(1u<<i))||!s->budget[i]||s->usage[i]>s->budget[i])return p;
        p.reason=DM_FG_HEAP_RECLAIM;
        if(credit&&(!published_ns||published_ns>s->sampled_ns||credit>s->usage[i]))return p;
        p.reason=DM_FG_HEAP_CAP;
        if(need>UINT64_MAX-p.total)return p;
        p.total+=need;
        uint64_t growth=need>credit?need-credit:0;
        if(growth) {
            uint64_t local=dm_fg_heap_headroom(s,i)/10;
            if(local<cap)cap=local;
            if(growth>UINT64_MAX-p.growth)return p;
            p.growth+=growth;
        }
    }
    // Do not add independent heap headrooms on mobile GPUs: they can alias.
    p.reason=p.total&&p.growth<=cap?DM_FG_HEAP_OK:DM_FG_HEAP_CAP;
    return p;
}
static inline int dm_fg_heap_gate(const dm_fg_heap_snapshot *s,uint64_t now,int upward) {
    return s->sampled_ns&&now>=s->sampled_ns&&now-s->sampled_ns<=UINT64_C(5000000000)&&
        s->count&&s->count<=DM_FG_MAX_HEAPS&&s->eligible&&
        s->os_available>(upward?1024:768)*FG_MIB&&s->thermal<4&&
        (!upward||(s->thermal>=0&&s->thermal<2));
}
// Net growth was checked before transfer. Recheck the full reservation
// against current headroom after reclaim, rather than charging reclaim twice.
static inline int dm_fg_heap_reserved_fits(const dm_fg_heap_snapshot *s,uint64_t now,
        const uint64_t *required,int upward) {
    if(!required||!dm_fg_heap_gate(s,now,upward))return 0;
    uint64_t left=s->os_available-(upward?1024:768)*FG_MIB,total=0;
    for(unsigned i=0;i<DM_FG_MAX_HEAPS;i++) {
        uint64_t bytes=required[i];
        if(!bytes)continue;
        if(bytes>dm_fg_heap_headroom(s,i)||bytes>left)return 0;
        left-=bytes;total+=bytes;
    }
    return total!=0;
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
