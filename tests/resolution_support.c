#include <assert.h>
#include "gfx_vk_resolution_support.h"
static float get(const uint8_t *b, unsigned slot) { float x; memcpy(&x,b+192+16*slot,4);return x; }
int main(void) {
    uint8_t b[NX_RS_SUPPORT_BYTES]; nx_rs_support_init(b);
    for(unsigned i=0;i<73;i++) assert(get(b,i)==1.0f);
    assert(nx_rs_support_set(b,0,0.75f)); assert(nx_rs_support_set(b,72,0.5f));
    assert(get(b,0)==0.75f && get(b,72)==0.5f && get(b,1)==1.0f);
    assert(!nx_rs_support_set(b,73,0.5f)); assert(!nx_rs_support_set(b,0,0.0f));
    const uint8_t p[]={'R','S','C','L',1,0,2,0,19,0,1,2,3,0,255,1};
    unsigned slot,flags; assert(nx_rs_ref_lookup(p,sizeof p,19,&slot,&flags) && slot==1 && flags==2);
    assert(nx_rs_ref_lookup(p,sizeof p,3,&slot,&flags) && slot==255 && flags==1);
    assert(!nx_rs_ref_lookup(p,sizeof p-1,19,&slot,&flags)); assert(!nx_rs_ref_lookup(p,sizeof p,7,&slot,&flags));
    const uint8_t duplicate_slot[]={'R','S','C','L',1,0,2,0,19,0,1,2,3,0,1,0};
    const uint8_t duplicate_binding[]={'R','S','C','L',1,0,2,0,19,0,1,2,19,0,2,0};
    const uint8_t malformed_peer[]={'R','S','C','L',1,0,2,0,19,0,1,2,3,0,73,0};
    const uint8_t empty[]={'R','S','C','L',1,0,0,0};
    assert(nx_rs_ref_valid(empty,sizeof empty) && nx_rs_ref_valid(p,sizeof p));
    assert(!nx_rs_ref_lookup(duplicate_slot,sizeof duplicate_slot,19,&slot,&flags));
    assert(!nx_rs_ref_lookup(duplicate_binding,sizeof duplicate_binding,19,&slot,&flags));
    assert(!nx_rs_ref_lookup(malformed_peer,sizeof malformed_peer,19,&slot,&flags));
    assert(!nx_rs_ref_lookup(p,sizeof p,19,NULL,&flags));
    return 0;
}
