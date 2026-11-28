#pragma once
#include <pthread.h>
#include <stdint.h>
#include "nvn_hmap.h"
// The caller holds the same mutex for all map accesses. A reserved key
// has exactly one compiler; a failed speculative compile releases it.
#define VK_PIPE_FAILED ((void *)(uintptr_t)1)
#define VK_PIPE_PENDING ((void *)(uintptr_t)2)
#define VK_PIPE_BUILDING ((void *)(uintptr_t)3)
static inline int vk_pipe_reserve_locked(nvn_hmap *map,const void *key,void *claim) {
    if(nvn_hm_get(map,key)) return 0;
    nvn_hm_put(map,key,claim); return 1;
}
static inline void *vk_pipe_wait_locked(nvn_hmap *map,const void *key,
        pthread_mutex_t *mu,pthread_cond_t *cv,int allow_pending) {
    void *p;
    while((p=nvn_hm_get(map,key))==VK_PIPE_BUILDING ||
          (p==VK_PIPE_PENDING&&!allow_pending))
        pthread_cond_wait(cv,mu);
    return p;
}
static inline int vk_pipe_finish_locked(nvn_hmap *map,const void *key,
        pthread_cond_t *cv,void *claim,void *result,int speculative) {
    if(nvn_hm_get(map,key)!=claim)return 0;
    if(speculative&&(!result||result==VK_PIPE_FAILED))nvn_hm_del(map,key);
    else nvn_hm_put(map,key,result?result:VK_PIPE_FAILED);
    pthread_cond_broadcast(cv);return 1;
}
