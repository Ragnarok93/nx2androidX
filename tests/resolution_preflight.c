#include <assert.h>
#include "gfx_vk_resolution_preflight.h"
int main(void) {
    assert(!nx_rs_range_overlap(0,64,64,32));
    assert(nx_rs_range_overlap(0,65,64,32));
    assert(nx_rs_range_overlap(64,32,0,65));
    assert(!nx_rs_range_overlap(4,0,4,1));
    assert(nx_rs_range_overlap(UINT64_MAX-3,4,UINT64_MAX,1));
    assert(!nx_rs_range_overlap(UINT64_MAX-3,3,UINT64_MAX,1));
    assert(nx_rs_preflight_overlap(0,64,0,64,1,1)==1);
    assert(nx_rs_preflight_overlap(0,64,0,64,1,0)==2);
    assert(nx_rs_preflight_overlap(0,64,32,32,0,1)==2);
    assert(nx_rs_preflight_overlap(0,64,64,64,0,0)==0);
    return 0;
}
