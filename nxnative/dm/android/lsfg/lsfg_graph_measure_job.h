#pragma once
#include "lsfg.h"
#include <pthread.h>
#include <stdatomic.h>

// A measurement job owns no graph or output image. Only the presentation
// thread starts/collects it; completion is published after the driver calls.
typedef int (*lsfg_graph_measure_fn)(VkExtent2D,float,lsfg_graph_budget *,const atomic_int *);
typedef struct {
    pthread_t thread;
    atomic_int cancelled,done;
    int running,ok;
    VkExtent2D extent;
    unsigned scale;
    uint64_t epoch;
    lsfg_graph_measure_fn measure;
    lsfg_graph_budget requirements;
} lsfg_graph_measure_job;
static inline void *lsfg_graph_measure_main(void *arg) {
    lsfg_graph_measure_job *j=arg;
    j->ok=j->measure(j->extent,j->scale/100.0f,&j->requirements,&j->cancelled);
    atomic_store_explicit(&j->done,1,memory_order_release);
    return NULL;
}
static inline int lsfg_graph_measure_start(lsfg_graph_measure_job *j,VkExtent2D extent,
        unsigned scale,uint64_t epoch,lsfg_graph_measure_fn measure) {
    if(j->running||!measure||!extent.width||!extent.height||scale<25||scale>100)return 0;
    j->extent=extent;j->scale=scale;j->epoch=epoch;j->measure=measure;
    j->ok=0;j->requirements=(lsfg_graph_budget){0};
    atomic_store_explicit(&j->cancelled,0,memory_order_relaxed);
    atomic_store_explicit(&j->done,0,memory_order_relaxed);
    if(pthread_create(&j->thread,NULL,lsfg_graph_measure_main,j))return 0;
    j->running=1;return 1;
}
static inline void lsfg_graph_measure_cancel(lsfg_graph_measure_job *j) {
    if(j->running)atomic_store_explicit(&j->cancelled,1,memory_order_release);
}
static inline int lsfg_graph_measure_take(lsfg_graph_measure_job *j,uint64_t epoch,lsfg_graph_budget *out) {
    *out=(lsfg_graph_budget){0};
    if(!j->running||!atomic_load_explicit(&j->done,memory_order_acquire))return 0;
    pthread_join(j->thread,NULL);
    if(j->ok&&j->epoch==epoch&&!atomic_load_explicit(&j->cancelled,memory_order_acquire))*out=j->requirements;
    j->running=0;return 1;
}
static inline void lsfg_graph_measure_drain(lsfg_graph_measure_job *j) {
    if(!j->running)return;
    lsfg_graph_measure_cancel(j);pthread_join(j->thread,NULL);j->running=0;
}
