#pragma once
#include <pthread.h>
#include <stdint.h>

// One source ticket per actual replay present boundary, independent of the
// guest's asynchronously advanced diagnostics frame counter.
enum { VK_REPLAY_WORK_SLOTS=64 };
typedef struct {uint64_t ticket,ns;unsigned pending,submits;int closed,invalid;} vk_replay_work_slot;
typedef struct {
    pthread_mutex_t mu;
    uint64_t current;
    vk_replay_work_slot slots[VK_REPLAY_WORK_SLOTS];
} vk_replay_work;
#define VK_REPLAY_WORK_INIT {.mu=PTHREAD_MUTEX_INITIALIZER,.current=1}
static inline vk_replay_work_slot *vk_replay_work_open(vk_replay_work *w,uint64_t t) {
    vk_replay_work_slot *s=&w->slots[t%VK_REPLAY_WORK_SLOTS];
    if(s->ticket!=t)*s=(vk_replay_work_slot){.ticket=t};
    return s;
}
static inline uint64_t vk_replay_work_submit(vk_replay_work *w) {
    pthread_mutex_lock(&w->mu);
    uint64_t t=w->current;
    vk_replay_work_slot *s=vk_replay_work_open(w,t);
    if(s->pending==~0u||s->submits==~0u||s->closed)s->invalid=1;
    else {++s->pending;++s->submits;}
    pthread_mutex_unlock(&w->mu);
    return t;
}
static inline void vk_replay_work_complete(vk_replay_work *w,uint64_t t,uint64_t ns,int valid) {
    if(!t)return;
    pthread_mutex_lock(&w->mu);
    vk_replay_work_slot *s=&w->slots[t%VK_REPLAY_WORK_SLOTS];
    if(s->ticket==t&&s->pending) {
        --s->pending;
        if(!valid||ns>UINT64_MAX-s->ns)s->invalid=1;
        else s->ns+=ns;
    }
    pthread_mutex_unlock(&w->mu);
}
static inline void vk_replay_work_close(vk_replay_work *w,uint64_t t,int replay) {
    pthread_mutex_lock(&w->mu);
    if(w->current!=t) {
        vk_replay_work_open(w,w->current)->invalid=1;
        // Unexpected ticket ordering must never publish a guessed mapping.
        replay=0;
    }
    vk_replay_work_slot *s=vk_replay_work_open(w,t);
    s->closed=1;
    if(!replay)s->invalid=1;
    w->current=t+1;
    pthread_mutex_unlock(&w->mu);
}
static inline double vk_replay_work_ms(vk_replay_work *w,uint64_t t) {
    if(!t)return -1;
    pthread_mutex_lock(&w->mu);
    vk_replay_work_slot *s=&w->slots[t%VK_REPLAY_WORK_SLOTS];
    double ms=s->ticket==t&&s->closed&&!s->invalid&&!s->pending&&s->submits?s->ns/1e6:-1;
    pthread_mutex_unlock(&w->mu);
    return ms;
}
