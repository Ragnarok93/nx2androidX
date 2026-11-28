#pragma once
#include "lsfg_graph_job.h"
#include "lsfg_graph_history.h"

// One extra graph: building, warming or retiring. The present thread owns
// active and extra references, and tracks GPU users by retired command slot.
// Output images are external and shared; their lifetime is NOT owned here.
// They must remain alive until the ordinary presentation consumer retires.
enum { LSFG_GRAPH_EMPTY, LSFG_GRAPH_BUILDING, LSFG_GRAPH_WARMING, LSFG_GRAPH_RETIRING };
typedef struct {
    lsfg_graph_job job;
    lsfg_chain *extra;
    lsfg_graph_history history;
    unsigned phase,scale,active_slots,extra_slots;
} lsfg_graph_owner;
static inline void lsfg_graph_owner_collect(lsfg_graph_owner *o) {
    if(o->phase==LSFG_GRAPH_RETIRING && !o->extra_slots) {
        lsfg_chain_free(o->extra);o->extra=NULL;o->phase=LSFG_GRAPH_EMPTY;o->scale=0;
    }
}
static inline int lsfg_graph_owner_start(lsfg_graph_owner *o,const lsfg_modules *mods,
        VkExtent2D extent,unsigned scale,const VkImageView *outputs,uint64_t limit,uint64_t epoch) {
    if(o->phase!=LSFG_GRAPH_EMPTY || o->job.running || o->extra || scale<25 || scale>100) return 0;
    if(!lsfg_graph_job_start(&o->job,mods,extent,scale/100.0f,outputs,limit,epoch))return 0;
    o->scale=scale;o->phase=LSFG_GRAPH_BUILDING;
    return 1;
}
// 0 pending/no job, 1 warmup ready to begin, -1 failed/cancelled.
static inline int lsfg_graph_owner_poll(lsfg_graph_owner *o,uint64_t epoch) {
    if(o->phase!=LSFG_GRAPH_BUILDING)return 0;
    lsfg_chain *p=NULL;
    if(!lsfg_graph_job_take(&o->job,epoch,&p))return 0;
    if(!p){o->phase=LSFG_GRAPH_EMPTY;o->scale=0;return -1;}
    o->extra=p;o->history=(lsfg_graph_history){.epoch=epoch};
    o->phase=LSFG_GRAPH_WARMING;return 1;
}
static inline void lsfg_graph_owner_cancel(lsfg_graph_owner *o) {
    if(o->phase==LSFG_GRAPH_BUILDING)lsfg_graph_job_cancel(&o->job);
    else if(o->phase==LSFG_GRAPH_WARMING) {
        o->phase=LSFG_GRAPH_RETIRING;lsfg_graph_owner_collect(o);
    }
}
// Call only after the existing slot fence has retired, before its reset.
static inline void lsfg_graph_owner_retire_slot(lsfg_graph_owner *o,unsigned slot) {
    if(slot>=sizeof(unsigned)*8)return;
    o->active_slots&=~(1u<<slot);o->extra_slots&=~(1u<<slot);
    lsfg_graph_owner_collect(o);
}
// No mutation on failed submit. The caller must retire the slot before
// reuse. A seed may run in the same submission as active generation.
static inline void lsfg_graph_owner_submit(lsfg_graph_owner *o,unsigned slot,int accepted,
        int active_used,int seeded,uint64_t epoch,uint64_t ticket) {
    if(!accepted || slot>=sizeof(unsigned)*8)return;
    if(active_used)o->active_slots|=1u<<slot;
    if(seeded && o->phase==LSFG_GRAPH_WARMING) {
        o->extra_slots|=1u<<slot;
        lsfg_graph_history_submit(&o->history,epoch,ticket,1);
    }
}
static inline int lsfg_graph_owner_ready(const lsfg_graph_owner *o,uint64_t epoch,uint64_t ticket) {
    return o->phase==LSFG_GRAPH_WARMING && o->extra &&
        lsfg_graph_history_ready(&o->history,epoch,ticket);
}
// Commit only AFTER a successful submission recorded using the warmed graph.
// The caller must not record old active work in that same submission.
static inline int lsfg_graph_owner_publish(lsfg_graph_owner *o,lsfg_chain **active,
        unsigned slot,uint64_t epoch,uint64_t ticket,int accepted) {
    if(!accepted || !*active || slot>=sizeof(unsigned)*8 ||
        !lsfg_graph_owner_ready(o,epoch,ticket))return 0;
    lsfg_chain *old=*active;*active=o->extra;o->extra=old;
    unsigned old_slots=o->active_slots;
    o->active_slots=o->extra_slots|(1u<<slot);o->extra_slots=old_slots;
    o->phase=LSFG_GRAPH_RETIRING;
    lsfg_graph_owner_collect(o);
    return 1;
}
// Teardown only: first drain CPU construction; then the caller waits for
// GPU/output retirement using its existing teardown contract, then clear.
static inline void lsfg_graph_owner_clear_idle(lsfg_graph_owner *o) {
    lsfg_graph_job_drain(&o->job);
    lsfg_chain_free(o->extra);o->extra=NULL;
    o->phase=LSFG_GRAPH_EMPTY;o->scale=0;o->active_slots=o->extra_slots=0;
    o->history=(lsfg_graph_history){0};
}
