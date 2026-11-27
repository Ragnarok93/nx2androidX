#ifndef DM_VK_RUNTIME_IDENTITY_H
#define DM_VK_RUNTIME_IDENTITY_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* build.py links every Android host with --build-id. Read the mapped ELF note
 * of this exact module (not the Android app, driver, or main executable).
 * The identifier is a binary fingerprint, not an asserted Git commit. */
enum { DM_RUNTIME_ID_CAP = 129 };
/* Android build.py defines _GNU_SOURCE. On non-ELF hosts or strict POSIX
 * host-test translation units the GNU loader enumeration API is unavailable. */
#if defined(__ELF__) && (defined(__ANDROID__) || defined(_GNU_SOURCE))
#include <elf.h>
#include <link.h>
static inline int dm_runtime_note_id(const void *notes, size_t bytes,
        char out[DM_RUNTIME_ID_CAP]) {
    const unsigned char *p = notes;
    while (bytes >= sizeof(ElfW(Nhdr))) {
        ElfW(Nhdr) h;
        memcpy(&h, p, sizeof h);
        p += sizeof h; bytes -= sizeof h;
        // Work in 64 bits before rounding so a corrupt 32-bit size cannot wrap.
        uint64_t name = ((uint64_t)h.n_namesz + 3) & ~UINT64_C(3);
        uint64_t desc = ((uint64_t)h.n_descsz + 3) & ~UINT64_C(3);
        if (name > bytes || desc > bytes - name) break;
        if (h.n_type == NT_GNU_BUILD_ID && h.n_namesz == 4 &&
            h.n_descsz && h.n_descsz <= (DM_RUNTIME_ID_CAP - 1) / 2 &&
            !memcmp(p, "GNU\0", 4)) {
            static const char hex[] = "0123456789abcdef";
            for (uint32_t i = 0; i < h.n_descsz; ++i) {
                unsigned x = p[(size_t)name + i];
                out[2*i] = hex[x >> 4]; out[2*i+1] = hex[x & 15];
            }
            out[2*h.n_descsz] = 0;
            return 1;
        }
        p += (size_t)(name + desc); bytes -= (size_t)(name + desc);
    }
    return 0;
}

typedef struct { uintptr_t anchor; char *out; } dm_runtime_identity_lookup;

/* PT_NOTE need not be mapped; only read ranges covered by a readable LOAD. */
static inline int dm_runtime_load_contains(const struct dl_phdr_info *info,
        uintptr_t address, uint64_t bytes) {
    for (ElfW(Half) i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr) *p = &info->dlpi_phdr[i];
        if (p->p_type != PT_LOAD || !(p->p_flags & PF_R)) continue;
        uintptr_t start = (uintptr_t)info->dlpi_addr + (uintptr_t)p->p_vaddr;
        if (address >= start && address - start <= p->p_memsz &&
            bytes <= p->p_memsz - (address - start)) return 1;
    }
    return 0;
}
static int dm_runtime_identity_visit(struct dl_phdr_info *info, size_t size, void *opaque) {
    (void)size;
    dm_runtime_identity_lookup *lookup = opaque;
    if (!dm_runtime_load_contains(info, lookup->anchor, 1)) return 0;
    for (ElfW(Half) i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr) *p = &info->dlpi_phdr[i];
        if (p->p_type != PT_NOTE || !p->p_filesz || p->p_filesz > SIZE_MAX) continue;
        uintptr_t address = (uintptr_t)info->dlpi_addr + (uintptr_t)p->p_vaddr;
        if (dm_runtime_load_contains(info, address, p->p_filesz) &&
            dm_runtime_note_id((const void *)address, (size_t)p->p_filesz, lookup->out)) return 1;
    }
    return 1; // Found the owning module, but no readable build-id note.
}
static inline void dm_runtime_build_id(char out[DM_RUNTIME_ID_CAP]) {
    static unsigned char module_anchor;
    memcpy(out, "unavailable", sizeof "unavailable");
    dm_runtime_identity_lookup lookup = {(uintptr_t)&module_anchor, out};
    dl_iterate_phdr(dm_runtime_identity_visit, &lookup);
}
#else
static inline void dm_runtime_build_id(char out[DM_RUNTIME_ID_CAP]) {
    memcpy(out, "unavailable", sizeof "unavailable");
}
#endif
#endif
