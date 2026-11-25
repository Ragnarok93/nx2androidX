#ifndef NX_RS_ACTIVE_H
#define NX_RS_ACTIVE_H
#include <stdint.h>
enum { NX_RS_ACTIVE_MAX=64 };
typedef struct { uintptr_t owner[NX_RS_ACTIVE_MAX]; unsigned n; } nx_rs_active;
/* Called under the cache lock. Copy the index for transactional staging. */
static inline int nx_rs_active_update(nx_rs_active *a,uintptr_t owner,int scaled) {
    if (!a || !owner || a->n>NX_RS_ACTIVE_MAX) return 0;
    for (unsigned i=0;i<a->n;i++) if (a->owner[i]==owner) {
        if (!scaled) { a->owner[i]=a->owner[--a->n]; a->owner[a->n]=0; }
        return 1;
    }
    if (!scaled) return 1;
    if (a->n==NX_RS_ACTIVE_MAX) return 0;
    a->owner[a->n++]=owner;
    return 1;
}
#endif
