#ifndef DM_VK_SOURCE_CONTINUITY_H
#define DM_VK_SOURCE_CONTINUITY_H
#include <stdint.h>

/* Present-thread-owned source contract. Guest window images rotate normally,
 * so a VkImage handle is not stream identity. Mode is the operation-mode
 * snapshot at enqueue, not proof the guest has processed the notification. */
enum {
    DM_RECON_SOURCE_INITIAL = 1u, DM_RECON_SOURCE_GAP = 2u,
    DM_RECON_SOURCE_PAUSE = 4u, DM_RECON_SOURCE_MODE = 8u,
    DM_RECON_SOURCE_GEOMETRY = 16u, DM_RECON_SOURCE_ENCODING = 32u,
    DM_RECON_SOURCE_SURFACE = 64u, DM_RECON_SOURCE_STALL = 128u
};
typedef struct {
    uint64_t ticket, source_ns, lifecycle_epoch;
    uint32_t width, height, output_w, output_h, format;
    int crop[4], flip_y, docked;
} dm_recon_source;
typedef struct {
    dm_recon_source previous;
    unsigned pending;
    int valid;
} dm_recon_continuity;

// Zero is the legacy frontend; nonzero odd epochs are held, even are running.
static inline uint64_t dm_lifecycle_next_epoch(uint64_t epoch, int held) {
    return (epoch & 1u) != (unsigned)!!held ? epoch + 1 : epoch;
}
static inline int dm_lifecycle_source_current(uint64_t source, uint64_t current) {
    return source == current && !(current & 1u);
}

static inline void dm_recon_source_invalidate(dm_recon_continuity *s) {
    s->pending |= DM_RECON_SOURCE_SURFACE;
    s->valid = 0;
}
/* Returns false for redraw, duplicate or stale tickets; no state is advanced. */
static inline int dm_recon_source_accept(dm_recon_continuity *s,
        const dm_recon_source *f, unsigned *flags) {
    *flags = 0;
    if (!f->ticket || f->ticket <= s->previous.ticket || (f->lifecycle_epoch & 1u) ||
        (f->lifecycle_epoch && f->lifecycle_epoch < s->previous.lifecycle_epoch)) return 0;
    unsigned changed = s->pending;
    if (!s->valid) changed |= DM_RECON_SOURCE_INITIAL;
    else {
        const dm_recon_source *p = &s->previous;
        if (f->ticket - p->ticket != 1) changed |= DM_RECON_SOURCE_GAP;
        if (f->lifecycle_epoch != p->lifecycle_epoch) changed |= DM_RECON_SOURCE_PAUSE;
        else if (!f->source_ns || !p->source_ns || f->source_ns <= p->source_ns ||
            f->source_ns - p->source_ns > UINT64_C(250000000))
            changed |= f->lifecycle_epoch ? DM_RECON_SOURCE_STALL : DM_RECON_SOURCE_PAUSE;
        if (f->docked != p->docked) changed |= DM_RECON_SOURCE_MODE;
        if (f->width != p->width || f->height != p->height ||
            f->output_w != p->output_w || f->output_h != p->output_h ||
            f->crop[0] != p->crop[0] || f->crop[1] != p->crop[1] ||
            f->crop[2] != p->crop[2] || f->crop[3] != p->crop[3]) changed |= DM_RECON_SOURCE_GEOMETRY;
        if (f->format != p->format || f->flip_y != p->flip_y) changed |= DM_RECON_SOURCE_ENCODING;
    }
    s->previous = *f;
    s->valid = 1;
    s->pending = 0;
    *flags = changed;
    return 1;
}
#endif
