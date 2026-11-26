#include <assert.h>
#include "gfx_vk_resolution_extent.h"
int main(void) {
    nx_rs_raster r;
    uint32_t x=0,y=0;
    assert(nx_rs_choose_extent(&r,1728,972,50,16384,16384));
    assert(nx_rs_require_grid(&r,50,16384,16384,&x,&y,4,4)==2);
    assert(r.pw%4==0 && r.ph%4==0);
    assert(r.pw==896 && r.ph==504 && x==4 && y==4);
    assert(nx_rs_require_grid(&r,50,16384,16384,&x,&y,8,2)==1 && x==8 && y==4);
    assert(nx_rs_require_grid(&r,50,16384,16384,&x,&y,4,8)==0);
    assert(x==8 && y==4 && r.pw==896 && r.ph==504);
    assert(nx_rs_choose_extent(&r,1920,1080,75,16384,16384));
    x=y=0;
    assert(nx_rs_require_grid(&r,75,16384,16384,&x,&y,8,8)==2);
    assert(r.pw%8==0 && r.ph%8==0 && r.scale>=0.725f && r.scale<=0.775f);
    assert(nx_rs_choose_extent(&r,1920,1080,50,16384,16384));
    x=y=0;
    assert(!nx_rs_require_grid(&r,50,16384,16384,&x,&y,8,8));
    assert(x==0 && y==0 && r.pw==960 && r.ph==540);
    assert(!nx_rs_choose_extent_aligned(&r,1920,1080,75,100,100,8,8));
    assert(!nx_rs_choose_extent_aligned(&r,1920,1080,75,16384,16384,0,8));
    assert(!nx_rs_choose_extent_aligned(&r,1920,1080,100,16384,16384,8,8));
    assert(nx_rs_grid_lcm(4,6)==12);
    assert(nx_rs_grid_lcm(UINT32_MAX,2)==0);
    return 0;
}
