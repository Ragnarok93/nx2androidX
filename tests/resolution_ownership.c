#include <assert.h>
#include "gfx_vk_resolution_ownership.h"
static nx_ro_graph g;
int main(void) {
    uint32_t a=nx_ro_note(&g,16,NX_RO_ATTACH), b=nx_ro_note(&g,32,NX_RO_ATTACH);
    nx_ro_merge(&g,a,b);
    nx_ro_note(&g,32,NX_RO_STORAGE);
    assert(nx_ro_flags(&g,a)&NX_RO_STORAGE);
    uint32_t c=nx_ro_note(&g,48,NX_RO_ATTACH);
    nx_ro_merge(&g,b,c);
    assert(nx_ro_flags(&g,c)&NX_RO_STORAGE);
    nx_ro_drop(&g,32);
    uint32_t d=nx_ro_note(&g,32,NX_RO_SAMPLE);
    assert(d!=b && !(nx_ro_flags(&g,d)&NX_RO_BLOCKERS));
    assert(nx_ro_flags(&g,a)&NX_RO_STORAGE);
    for (unsigned i=0;i<NX_RO_MAX-4;i++) assert(nx_ro_note(&g,64+(uintptr_t)i*16,NX_RO_ATTACH));
    assert(!nx_ro_note(&g,UINTPTR_MAX,NX_RO_ATTACH));
    assert(nx_ro_flags(&g,d)&NX_RO_UNKNOWN);
    return 0;
}
