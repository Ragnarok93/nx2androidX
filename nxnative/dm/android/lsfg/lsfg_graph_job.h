#pragma once
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include "lsfg.h"

// One background construction job. Owner thread exclusively starts, polls,
// cancels and drains it. Modules and external output views are borrowed:
// the owner MUST drain before releasing either, even after cancellation.
// The worker never submits Vulkan commands or modifies the live graph.
typedef struct {
    pthread_t thread;
    int running,cancelled;
    atomic_int done;
    const lsfg_modules *mods;
    VkExtent2D extent;
    float scale;
    uint64_t byte_limit,epoch;
    VkImageView outputs[LSFG_MAX_GENERATIONS];
    lsfg_chain *result;
} lsfg_graph_job;
static inline void *lsfg_graph_job_main(void *arg) {
    lsfg_graph_job *j=arg;
    j->result=lsfg_chain_build(j->mods,j->extent,j->scale,j->outputs,j->byte_limit);
    atomic_store_explicit(&j->done,1,memory_order_release);
    return NULL;
}
static inline int lsfg_graph_job_start(lsfg_graph_job *j,const lsfg_modules *mods,
        VkExtent2D extent,float scale,const VkImageView *outputs,uint64_t cap,uint64_t epoch) {
    if(j->running || !mods || !outputs || !cap || !extent.width || !extent.height ||
        !(scale>=.25f && scale<=1.0f)) return 0;
    for(unsigned i=0;i<LSFG_MAX_GENERATIONS;i++) if(!outputs[i]) return 0;
    j->mods=mods;j->extent=extent;j->scale=scale;j->byte_limit=cap;j->epoch=epoch;
    for(unsigned i=0;i<LSFG_MAX_GENERATIONS;i++)j->outputs[i]=outputs[i];
    j->result=NULL;j->cancelled=0;
    atomic_store_explicit(&j->done,0,memory_order_relaxed);
    if(pthread_create(&j->thread,NULL,lsfg_graph_job_main,j)) return 0;
    j->running=1;return 1;
}
// Cancellation does not wait. A stale result is never returned to the owner.
static inline void lsfg_graph_job_cancel(lsfg_graph_job *j) {
    if(j->running)j->cancelled=1;
}
// Returns 0 while construction is in flight, 1 when a job is collected
// (NULL result means failure/cancellation). Joining occurs only after the
// worker has published completion; it has finished all Vulkan construction.
static inline int lsfg_graph_job_take(lsfg_graph_job *j,uint64_t epoch,lsfg_chain **out) {
    *out=NULL;
    if(!j->running || !atomic_load_explicit(&j->done,memory_order_acquire))return 0;
    pthread_join(j->thread,NULL);
    if(j->cancelled || j->epoch!=epoch) lsfg_chain_free(j->result);
    else *out=j->result;
    j->result=NULL;j->running=0;return 1;
}
// Geometry change, disabling FG and device teardown may drain an outstanding
// CPU build before freeing borrowed views/modules. Never use this to switch
// adaptive scales; that path must poll and retain the active graph instead.
static inline void lsfg_graph_job_drain(lsfg_graph_job *j) {
    if(!j->running)return;
    pthread_join(j->thread,NULL);
    lsfg_chain_free(j->result);
    j->result=NULL;j->running=0;j->cancelled=0;
}
