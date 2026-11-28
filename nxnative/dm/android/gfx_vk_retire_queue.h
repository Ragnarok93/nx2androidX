#ifndef NX2A_VK_RETIRE_QUEUE_H
#define NX2A_VK_RETIRE_QUEUE_H
#include <stdint.h>
#include <stddef.h>

// Single ordered queue protected by the caller's mutex. An open submission
// reserves its fence before recording; idle frees inherit that same fence.
// No dependency on reusable command-buffer slots or on the next submit.
typedef struct vk_retire_entry {
    struct vk_retire_entry *next;
    uint64_t fence;
    int kind;
    void *a, *b;
} vk_retire_entry;
typedef struct {
    vk_retire_entry *head, *tail;
    uint64_t fence;
} vk_retire_queue;
static inline void vk_retire_begin(vk_retire_queue *q, uint64_t fence) {
    q->fence = fence;
}
static inline void vk_retire_push(vk_retire_queue *q, vk_retire_entry *e) {
    e->fence = q->fence;
    e->next = NULL;
    if (q->tail) q->tail->next = e; else q->head = e;
    q->tail = e;
}
static inline vk_retire_entry *vk_retire_pop(vk_retire_queue *q, uint64_t done) {
    vk_retire_entry *e = q->head;
    if (!e || e->fence > done) return NULL;
    q->head = e->next;
    if (!q->head) q->tail = NULL;
    e->next = NULL;
    return e;
}
#endif
