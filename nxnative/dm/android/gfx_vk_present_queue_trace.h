#pragma once
#include <stdint.h>
#include <stdio.h>

// CPU-side observations only: retirement here means release of a software
// queue entry, NOT GPU completion or actual display. Zero means unavailable.
// Occupancy includes the in-service entry until the consumer retires it.
typedef struct {
    uint64_t source_ns, entry_ns, dequeue_ns, work_begin_ns, work_end_ns, retire_ns;
    uint64_t oldest_entry_age_ns, oldest_dequeue_age_ns;
    unsigned entry_occupied, dequeue_occupied, synchronous, has_image;
} dm_present_queue_trace;

static inline uint64_t dm_present_queue_age(uint64_t now, uint64_t oldest, unsigned occupied) {
    return occupied && oldest && now >= oldest ? now - oldest : 0;
}

// Kept as a single writer so column names and order have one owner.
#define DM_PRESENT_QUEUE_COLUMNS "\tqueue_source_ns\tqueue_entry_ns\tqueue_dequeue_ns\tqueue_work_begin_ns\tqueue_work_end_ns\tqueue_cpu_retire_ns\tqueue_oldest_entry_age_ns\tqueue_oldest_dequeue_age_ns\tqueue_entry_occupied\tqueue_dequeue_occupied\tqueue_sync\tqueue_has_image"
static inline void dm_present_queue_write(FILE *out, const dm_present_queue_trace *s) {
    fprintf(out,"\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%u\t%u\t%u\t%u",
            (unsigned long long)s->source_ns, (unsigned long long)s->entry_ns,
            (unsigned long long)s->dequeue_ns, (unsigned long long)s->work_begin_ns,
            (unsigned long long)s->work_end_ns, (unsigned long long)s->retire_ns,
            (unsigned long long)s->oldest_entry_age_ns, (unsigned long long)s->oldest_dequeue_age_ns,
            s->entry_occupied, s->dequeue_occupied, s->synchronous, s->has_image);
}
