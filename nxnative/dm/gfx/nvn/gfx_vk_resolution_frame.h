#ifndef NX_RS_FRAME_H
#define NX_RS_FRAME_H
#include <stdint.h>
/* Replay-owned state: target is permission to PLAN, not proof of a scaled
 * backing. A changed target cannot commit until old owners are restored. */
typedef struct { uint64_t epoch; unsigned target; } nx_rs_frame_state;
enum { NX_RS_FRAME_IGNORE=0,NX_RS_FRAME_ACCEPT=1,NX_RS_FRAME_RESTORE=2 };
static inline int nx_rs_frame_latch(nx_rs_frame_state *s,uint64_t epoch,unsigned percent,int active,int restored) {
    if (!s || epoch<=s->epoch || percent<50 || percent>100) return NX_RS_FRAME_IGNORE;
    if (active && (percent!=s->target || percent==100) && !restored) return NX_RS_FRAME_RESTORE;
    s->epoch=epoch; s->target=percent;
    return NX_RS_FRAME_ACCEPT;
}
#endif
