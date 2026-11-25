#include <assert.h>
#include "gfx_vk_resolution_plan.h"
int main(void) {
    nx_rs_group g = {0};
    nx_rs_add(&g, 1280, 720, 0);
    nx_rs_add(&g, 1280, 720, 0);
    nx_rs_extent e;
    assert(nx_rs_plan(&g, 75, 4096, 4096, &e));
    assert(e.w == 960 && e.h == 540);
    assert(nx_rs_plan(&g, 50, 4096, 4096, &e) && e.w == 640 && e.h == 360);
    assert(nx_rs_plan(&g, 100, 4096, 4096, &e) && e.w == 1280 && e.h == 720);
    assert(nx_rs_plan(&g, 200, 4096, 4096, &e) && e.w == 2560 && e.h == 1440);
    assert(!nx_rs_plan(&g, 200, 2048, 4096, &e));
    assert(!nx_rs_plan(&g, 49, 4096, 4096, &e));
    nx_rs_rect r;
    assert(nx_rs_map_rect(1, 1, 3, 3, 1280, 720, 960, 540, &r));
    assert(r.x == 0 && r.y == 0 && r.w == 3 && r.h == 3);
    assert(nx_rs_map_rect(1279, 719, 1, 1, 1280, 720, 960, 540, &r));
    assert(r.x == 959 && r.y == 539 && r.w == 1 && r.h == 1);
    assert(!nx_rs_map_rect(1279, 719, 2, 1, 1280, 720, 960, 540, &r));
    nx_rs_add(&g, 640, 360, 0);
    assert(g.blockers & NX_RS_MIXED_EXTENT);
    assert(!nx_rs_plan(&g, 75, 4096, 4096, &e));
    g = (nx_rs_group){0};
    nx_rs_add(&g, 1280, 720, NX_RS_ALIAS | NX_RS_STORAGE);
    assert(!nx_rs_plan(&g, 75, 4096, 4096, &e));
    g = (nx_rs_group){0};
    assert(!nx_rs_plan(&g, 75, 4096, 4096, &e));
    nx_rs_timing t = {0};
    assert(nx_rs_new_sample(&t, 0));
    assert(!nx_rs_new_sample(&t, 0));
    assert(nx_rs_new_sample(&t, 3));
    assert(!nx_rs_new_sample(&t, 2));
    assert(nx_rs_new_sample(&t, 4));
    return 0;
}
