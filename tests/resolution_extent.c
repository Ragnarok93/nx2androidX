#include <assert.h>
#include "gfx_vk_resolution_extent.h"
int main(void) {
    nx_rs_raster r;
    assert(nx_rs_choose_extent(&r,1920,1080,60,16384,16384));
    assert(r.pw<1920 && r.ph<1080 && r.scale>=0.575f && r.scale<=0.625f);
    assert(nx_rs_choose_extent(&r,1920,1080,65,16384,16384));
    assert(nx_rs_choose_extent(&r,1728,972,50,16384,16384) && r.pw==864 && r.ph==486);
    assert(nx_rs_choose_extent(&r,1728,972,75,16384,16384) && r.pw==1296 && r.ph==729);
    assert(nx_rs_choose_extent(&r,1384,780,50,16384,16384) && r.pw==692 && r.ph==390);
    assert(!nx_rs_choose_extent(&r,1384,780,60,16384,16384));
    assert(!nx_rs_choose_extent(&r,1279,720,50,16384,16384));
    assert(!nx_rs_choose_extent(&r,1920,1080,100,16384,16384));
    assert(!nx_rs_choose_extent(&r,1920,1080,50,800,400));
    assert(!nx_rs_choose_extent(&r,0,1080,50,16384,16384));
    return 0;
}
