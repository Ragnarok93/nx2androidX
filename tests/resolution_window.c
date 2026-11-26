#include <assert.h>
#include "gfx_vk_resolution_window.h"
int main(void) {
    uint64_t a[]={11,12},b[]={21,22},out[NX_RS_WINDOW_HANDLES]={0};
    assert(nx_rs_window_handles(out,a,2,b,2,1)==4);
    assert(out[0]==11 && out[1]==12 && out[2]==21 && out[3]==22);
    assert(!nx_rs_window_handles(out,a,2,b,2,0));
    assert(!nx_rs_window_handles(out,a,0,b,2,1));
    assert(!nx_rs_window_handles(out,a,32,b,1,1));
    b[0]=0;assert(!nx_rs_window_handles(out,a,2,b,2,1));
    assert(nx_rs_window_descriptor(0,0,0,1,1));
    assert(!nx_rs_window_descriptor(0,0,0,1,0));
    assert(!nx_rs_window_descriptor(0,0,0,0,1));
    assert(nx_rs_window_descriptor(10,11,9,1,0));
    assert(!nx_rs_window_descriptor(10,11,9,0,0));
    assert(!nx_rs_window_descriptor(10,11,10,1,0));
    assert(nx_rs_window_descriptor(10,10,9,0,0));
    assert(!nx_rs_window_descriptor(10,10,10,1,0));
    return 0;
}
