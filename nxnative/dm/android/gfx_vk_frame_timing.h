#pragma once
#include <pthread.h>
#include <stdint.h>
// Completion writes while replay, diagnostics and presentation read. A
// frame tag and its accumulated GPU duration must be published together.
enum { VK_FRAME_TIMING_SLOTS=64 };
typedef struct {
    pthread_mutex_t mu;
    struct { uint64_t tag, ns; } slots[VK_FRAME_TIMING_SLOTS];
} vk_frame_timing;
#define VK_FRAME_TIMING_INIT { .mu=PTHREAD_MUTEX_INITIALIZER }
static inline void vk_frame_timing_add(vk_frame_timing *t,uint64_t frame,uint64_t ns) {
    unsigned i=(unsigned)(frame%VK_FRAME_TIMING_SLOTS);
    pthread_mutex_lock(&t->mu);
    if(t->slots[i].tag!=frame+1) {
        t->slots[i].tag=frame+1;
        t->slots[i].ns=0;
    }
    t->slots[i].ns+=ns;
    pthread_mutex_unlock(&t->mu);
}
static inline double vk_frame_timing_ms(vk_frame_timing *t,uint64_t frame) {
    unsigned i=(unsigned)(frame%VK_FRAME_TIMING_SLOTS);
    pthread_mutex_lock(&t->mu);
    double ms=t->slots[i].tag==frame+1?t->slots[i].ns/1e6:-1;
    pthread_mutex_unlock(&t->mu);
    return ms;
}
