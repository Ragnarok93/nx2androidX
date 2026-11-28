#pragma once
#include <stdint.h>
// Pending graph history counts accepted real-source *GPU submissions*, not
// presents, and is never seeded with a generated image. The owner invalidates
// the epoch on discontinuity and records init on the first successful submit.
typedef struct {uint64_t epoch,last_ticket,next_index;unsigned filled,initialized;} lsfg_graph_history;
enum { LSFG_GRAPH_HISTORY_COUNT=3 };
static inline void lsfg_graph_history_reset(lsfg_graph_history *h,uint64_t epoch) {
    // Layout initialization persists across a source discontinuity.
    h->epoch=epoch;h->last_ticket=0;h->next_index=0;h->filled=0;
}
static inline int lsfg_graph_history_can_seed(const lsfg_graph_history *h,uint64_t epoch,uint64_t ticket) {
    return epoch==h->epoch && ticket && ticket>h->last_ticket;
}
static inline uint64_t lsfg_graph_history_index(const lsfg_graph_history *h,uint64_t ticket) {
    // A skipped real source starts a fresh chronological window.
    return h->last_ticket && ticket-h->last_ticket==1 ? h->next_index : 0;
}
static inline int lsfg_graph_history_submit(lsfg_graph_history *h,uint64_t epoch,uint64_t ticket,int accepted) {
    if(!accepted || !lsfg_graph_history_can_seed(h,epoch,ticket))return 0;
    uint64_t index=lsfg_graph_history_index(h,ticket);
    h->initialized=1;h->last_ticket=ticket;h->next_index=index+1;
    h->filled=index<LSFG_GRAPH_HISTORY_COUNT?(unsigned)index+1:LSFG_GRAPH_HISTORY_COUNT;
    return 1;
}
static inline int lsfg_graph_history_ready(const lsfg_graph_history *h,uint64_t epoch,uint64_t next_ticket) {
    // Activate on the immediately following real source, using next_index
    // as the chain count. Copy and alpha replace the oldest entries, then
    // beta sees current and two preceding entries; generation reads the last two frames.
    return h->initialized && h->filled==LSFG_GRAPH_HISTORY_COUNT &&
        h->epoch==epoch && h->last_ticket && next_ticket>h->last_ticket &&
        next_ticket-h->last_ticket==1;
}
