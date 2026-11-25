#ifndef NX_RS_RESTORE_H
#define NX_RS_RESTORE_H
#include <stdint.h>
/* Native restoration consumes at most the original dedicated allocation.
 * Scaled and retired generations remain charged until actual destruction. */
static inline int nx_rs_restore_admit(uint64_t native_bytes, uint64_t requested) {
    return requested && native_bytes && requested <= native_bytes;
}
#endif
