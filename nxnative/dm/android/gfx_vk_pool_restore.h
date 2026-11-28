#ifndef NX2A_VK_POOL_RESTORE_H
#define NX2A_VK_POOL_RESTORE_H
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>

// The caller owns the guest range (no concurrent guest writes). Fill the
// replacement pages before moving them into place, as in the lazy swap path.
// Copy once rather than through a whole-pool malloc and a second memcpy.
// The temporary mapping is bounded to 16 MiB; completed chunks are anonymous.
// An error must not be treated as successful restoration by the caller.
static inline int vk_pool_restore_anon(uint8_t *mem, uint64_t size) {
    const size_t chunk = 16u << 20;
    while (size) {
        size_t n = size > chunk ? chunk : (size_t)size;
        void *tmp = mmap(NULL,n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if (tmp == MAP_FAILED) return -1;
        memcpy(tmp,mem,n);
        if (mremap(tmp,n,n,MREMAP_MAYMOVE|MREMAP_FIXED,mem) != mem) {
            int error = errno;
            munmap(tmp,n);
            errno = error;
            return -1;
        }
        mem += n;
        size -= n;
    }
    return 0;
}
#endif
