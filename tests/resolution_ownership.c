#include <assert.h>
#include "gfx_vk_resolution_ownership.h"
static nx_ro_graph g;
static nx_ro_graph admission;
static void test_admission(void) {
    nx_ro_graph *g=&admission;
    uintptr_t keys[3]={16,32,0};
    assert(!nx_ro_group_admit(g,keys,3));
    uint32_t a=nx_ro_note(g,16,NX_RO_ATTACH), b=nx_ro_note(g,32,NX_RO_ATTACH);
    assert(!nx_ro_group_admit(g,keys,3));
    nx_ro_merge(g,a,b);
    assert(nx_ro_group_admit(g,keys,3));
    assert(!nx_ro_group_admit(g,keys,1));
    keys[2]=16; assert(!nx_ro_group_admit(g,keys,3)); keys[2]=0;
    uint32_t c=nx_ro_note(g,48,NX_RO_ATTACH); nx_ro_merge(g,a,c);
    assert(!nx_ro_group_admit(g,keys,3));
    nx_ro_drop(g,48); assert(nx_ro_group_admit(g,keys,3));
    nx_ro_note(g,32,NX_RO_HOST); assert(!nx_ro_group_admit(g,keys,3));
    nx_ro_drop(g,32); nx_ro_note(g,32,NX_RO_ATTACH);
    assert(!nx_ro_group_admit(g,keys,3));
    g->exhausted=1; assert(!nx_ro_group_admit(g,keys,3));
}
int main(void) {
    test_admission();
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
