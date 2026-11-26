#ifndef NX_RS_WINDOW_H
#define NX_RS_WINDOW_H
#include <stdint.h>
#include <string.h>
// Two already queued submissions on the same queue, no presents or empty
// jobs. The scanner still applies its own byte, opcode and handle bounds.
enum { NX_RS_WINDOW_HANDLES=32 };
static inline unsigned nx_rs_window_handles(uint64_t *out, const uint64_t *a,int na,
                                            const uint64_t *b,int nb,int same_queue) {
    if (!out || !a || !b || !same_queue || na<=0 || nb<=0 ||
        na>NX_RS_WINDOW_HANDLES || nb>NX_RS_WINDOW_HANDLES-na) return 0;
    for (int i=0;i<na;i++) if (!a[i]) return 0;
    for (int i=0;i<nb;i++) if (!b[i]) return 0;
    memcpy(out,a,(size_t)na*sizeof *out);
    memcpy(out+na,b,(size_t)nb*sizeof *out);
    return (unsigned)(na+nb);
}
// Synchronous scopes hold the registration gate. Across async jobs, only
// a current version older than the first job can be frozen for the window.
static inline int nx_rs_window_descriptor(uint64_t job,uint64_t last,uint64_t seq,int current,int sync_guard) {
    if (!job) return sync_guard && current;
    if (seq>=job) return 0;
    return last<=job || current;
}
#endif
