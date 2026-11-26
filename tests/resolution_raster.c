#include <assert.h>
#include "gfx_vk_resolution_raster.h"
int main(void) {
    nx_rs_raster r;
    assert(nx_rs_raster_init(&r,1280,720,960,540));
    assert(r.scale==0.75f);
    nx_rs_viewport v={0,720,1280,-720};
    nx_rs_scale_viewport(&r,&v);
    assert(v.x==0 && v.y==540 && v.w==960 && v.h==-540);
    nx_rs_raster_rect out;
    assert(nx_rs_clip_scissor(&r,-4,4,12,8,0,&out));
    assert(out.x==0 && out.y==3 && out.w==6 && out.h==6);
    assert(nx_rs_clip_scissor(&r,0,0,1280,720,1,&out));
    assert(out.x==0 && out.y==0 && out.w==960 && out.h==540);
    assert(nx_rs_clip_scissor(&r,0,0,8,8,1,&out));
    assert(out.y==534 && out.h==6);
    assert(!nx_rs_clip_scissor(&r,1300,0,1,1,0,&out));
    assert(!nx_rs_raster_init(&r,1279,720,960,540));
    // The SupportBuffer scalar cannot round-trip these exact aspect ratios.
    assert(!nx_rs_raster_init(&r,1920,1080,1152,648));
    assert(!nx_rs_raster_init(&r,1920,1080,1248,702));
    assert(nx_rs_raster_init(&r,1384,780,692,390));
    assert(nx_rs_raster_init(&r,1384,780,1038,585));
    assert(nx_rs_raster_init(&r,1280,720,1280,720));
    v=(nx_rs_viewport){1,2,3,-4}; nx_rs_scale_viewport(&r,&v);
    assert(v.x==1 && v.y==2 && v.w==3 && v.h==-4);
    return 0;
}
