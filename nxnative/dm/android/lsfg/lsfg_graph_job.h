#pragma once
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include "lsfg.h"

// One background construction job. Owner thread exclusively starts, polls,
// cancels and drains it. Modules and external output views are borrowed:
// the owner MUST drain before releasing either, even after cancellation.
// The worker never submits Vulkan commands or modifies the live graph.
typedef lsfg_chain *(*lsfg_graph_reserved_builder)(const lsfg_modules *,VkExtent2D,float,
        const VkImageView *,const lsfg_graph_budget *,const atomic_int *);
typedef struct {
    pthread_t thread;
    int running;
    atomic_int cancelled,done;
    const lsfg_modules *mods;
    VkExtent2D extent;
    float scale;
    uint64_t byte_limit,epoch;
    lsfg_graph_reserved_builder reserved_build;
    lsfg_graph_budget reservation;
    VkImageView outputs[LSFG_MAX_GENERATIONS];
    lsfg_chain *result;
    lsfg_chain *retire; // optional fully retired graph, ownership transferred on successful start
} lsfg_graph_job;
static inline void *lsfg_graph_job_main(void *arg) {
    lsfg_graph_job *j=arg;
    // A cold replacement releases the old private allocations before any
    // allocation for the new graph. Outputs and modules remain borrowed.
    lsfg_chain_free(j->retire);j->retire=NULL;
    j->result=j->reserved_build?
        j->reserved_build(j->mods,j->extent,j->scale,j->outputs,&j->reservation,&j->cancelled):
        lsfg_chain_build_cancelable(j->mods,j->extent,j->scale,j->outputs,j->byte_limit,&j->cancelled);
    atomic_store_explicit(&j->done,1,memory_order_release);
    return NULL;
}
static inline int lsfg_graph_job_start_impl(lsfg_graph_job *j,const lsfg_modules *mods,
        VkExtent2D extent,float scale,const VkImageView *outputs,uint64_t cap,uint64_t epoch,
        lsfg_chain *retire,const lsfg_graph_budget *reservation,lsfg_graph_reserved_builder builder) {
    if(j->running || !mods || !outputs || !cap || !extent.width || !extent.height ||
        !(scale>=.25f && scale<=1.0f)) return 0;
    for(unsigned i=0;i<LSFG_MAX_GENERATIONS;i++) if(!outputs[i]) return 0;
    j->mods=mods;j->extent=extent;j->scale=scale;j->byte_limit=cap;j->epoch=epoch;
    j->reserved_build=builder;j->reservation=reservation?*reservation:(lsfg_graph_budget){0};
    for(unsigned i=0;i<LSFG_MAX_GENERATIONS;i++)j->outputs[i]=outputs[i];
    j->result=NULL;j->retire=retire;atomic_store_explicit(&j->cancelled,0,memory_order_relaxed);
    atomic_store_explicit(&j->done,0,memory_order_relaxed);
    if(pthread_create(&j->thread,NULL,lsfg_graph_job_main,j)) { j->retire=NULL;return 0; }
    j->running=1;return 1;
}
static inline int lsfg_graph_job_start_replace(lsfg_graph_job *j,const lsfg_modules *mods,
        VkExtent2D extent,float scale,const VkImageView *outputs,uint64_t cap,uint64_t epoch,lsfg_chain *retire) {
    return lsfg_graph_job_start_impl(j,mods,extent,scale,outputs,cap,epoch,retire,NULL,NULL);
}
static inline int lsfg_graph_job_start_reserved_replace(lsfg_graph_job *j,const lsfg_modules *mods,
        VkExtent2D extent,float scale,const VkImageView *outputs,uint64_t epoch,lsfg_chain *retire,
        const lsfg_graph_budget *reservation,lsfg_graph_reserved_builder builder) {
    if(!reservation||!builder||!reservation->used||reservation->used==UINT64_MAX)return 0;
    return lsfg_graph_job_start_impl(j,mods,extent,scale,outputs,reservation->used,epoch,retire,reservation,builder);
}
static inline int lsfg_graph_job_start(lsfg_graph_job *j,const lsfg_modules *mods,
        VkExtent2D extent,float scale,const VkImageView *outputs,uint64_t cap,uint64_t epoch) {
    return lsfg_graph_job_start_replace(j,mods,extent,scale,outputs,cap,epoch,NULL);
}
// Cancellation does not wait. A stale result is never returned to the owner.
static inline void lsfg_graph_job_cancel(lsfg_graph_job *j) {
    if(j->running)atomic_store_explicit(&j->cancelled,1,memory_order_release);
}
// Returns 0 while construction is in flight, 1 when a job is collected
// (NULL result means failure/cancellation). Joining occurs only after the
// worker has published completion; it has finished all Vulkan construction.
static inline int lsfg_graph_job_take(lsfg_graph_job *j,uint64_t epoch,lsfg_chain **out) {
    *out=NULL;
    if(!j->running || !atomic_load_explicit(&j->done,memory_order_acquire))return 0;
    pthread_join(j->thread,NULL);
    if(atomic_load_explicit(&j->cancelled,memory_order_acquire) || j->epoch!=epoch) lsfg_chain_free(j->result);
    else *out=j->result;
    j->result=NULL;j->running=0;return 1;
}
// Geometry change, disabling FG and device teardown may drain an outstanding
// CPU build before freeing borrowed views/modules. Never use this to switch
// adaptive scales; that path must poll and retain the active graph instead.
static inline void lsfg_graph_job_drain(lsfg_graph_job *j) {
    if(!j->running)return;
    lsfg_graph_job_cancel(j);
    pthread_join(j->thread,NULL);
    lsfg_chain_free(j->result);
    j->result=NULL;j->running=0;atomic_store_explicit(&j->cancelled,0,memory_order_relaxed);
}
