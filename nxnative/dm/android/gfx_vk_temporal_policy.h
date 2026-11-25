#ifndef DM_VK_TEMPORAL_POLICY_H
#define DM_VK_TEMPORAL_POLICY_H
#include <stdint.h>

/* Pure policy: input bits certify normalized, same-frame, pre-HUD resources.
 * Variant bits certify pipeline/format support. This does not discover or retain
 * images. One owner per temporal stream; commit only after successful submission
 * with ordered history dependencies. Epoch changes on lifecycle/driver changes. */

enum {
    DM_TEMPORAL_COLOR=1, DM_TEMPORAL_DEPTH=2, DM_TEMPORAL_VELOCITY=4,
    DM_TEMPORAL_CAMERA=8, DM_TEMPORAL_JITTER=16, DM_TEMPORAL_EXPOSURE=32,
    DM_TEMPORAL_OPAQUE=64, DM_TEMPORAL_REQUIRED=63
};
enum { DM_TEMPORAL_2FS=1, DM_TEMPORAL_2CS=2, DM_TEMPORAL_3CS=4 };
typedef struct {
    uint64_t ticket, stream, epoch;
    uint32_t iw, ih, ow, oh, inputs, variants;
    int real, memory_ok, cut;
} dm_temporal_frame;
typedef struct {
    uint64_t ticket, stream, epoch;
    uint32_t iw, ih, ow, oh, variant;
    int valid;
} dm_temporal_history;
typedef struct { uint32_t variant; int reset; } dm_temporal_plan;
static inline dm_temporal_plan dm_temporal_choose(const dm_temporal_history *h,
        const dm_temporal_frame *f, uint32_t preferred) {
    dm_temporal_plan p = {0,1};
    if (!f->real || !f->memory_ok || !f->ticket || !f->stream ||
        !f->iw || !f->ih || !f->ow || !f->oh ||
        f->iw > f->ow || f->ih > f->oh ||
        (f->inputs & DM_TEMPORAL_REQUIRED) != DM_TEMPORAL_REQUIRED)
        return p;
    int same = h->valid && h->stream == f->stream && h->epoch == f->epoch;
    if (same && f->ticket <= h->ticket) return p;
    uint32_t available = f->variants & (DM_TEMPORAL_2FS | DM_TEMPORAL_2CS | DM_TEMPORAL_3CS);
    if (!(f->inputs & DM_TEMPORAL_OPAQUE)) available &= ~DM_TEMPORAL_3CS;
    if ((preferred == DM_TEMPORAL_2FS || preferred == DM_TEMPORAL_2CS ||
         preferred == DM_TEMPORAL_3CS) && (available & preferred)) p.variant = preferred;
    else if (available & DM_TEMPORAL_2FS) p.variant = DM_TEMPORAL_2FS;
    else if (available & DM_TEMPORAL_2CS) p.variant = DM_TEMPORAL_2CS;
    else if (available & DM_TEMPORAL_3CS) p.variant = DM_TEMPORAL_3CS;
    p.reset = !p.variant || !same || f->cut || f->ticket - h->ticket != 1 ||
        h->variant != p.variant || h->iw != f->iw || h->ih != f->ih ||
        h->ow != f->ow || h->oh != f->oh;
    return p;
}
static inline void dm_temporal_commit(dm_temporal_history *h,
        const dm_temporal_frame *f, dm_temporal_plan p, int success) {
    if (!f->real || (h->valid && h->stream == f->stream &&
        h->epoch == f->epoch && f->ticket <= h->ticket)) return;
    if (!success || !p.variant) { h->valid = 0; return; }
    *h = (dm_temporal_history){.ticket=f->ticket, .stream=f->stream,
        .epoch=f->epoch, .iw=f->iw, .ih=f->ih, .ow=f->ow, .oh=f->oh,
        .variant=p.variant, .valid=1};
}
#endif
