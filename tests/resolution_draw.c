#include <assert.h>
#include "gfx_vk_resolution_draw.h"
int main(void) {
    nx_rs_draw_extent a[3]={{1280,720,960,540,1},{1280,720,960,540,1},{0}};
    nx_rs_draw_extent r;
    assert(nx_rs_draw_group(a,3,&r)==1 && r.pw==960 && r.ph==540);
    a[1].pw=1280; a[1].ph=720;
    assert(!nx_rs_draw_group(a,3,&r));
    a[0].pw=1280; a[0].ph=720;
    assert(nx_rs_draw_group(a,3,&r)==1 && r.pw==1280);
    a[0].pw=959; a[0].ph=540; a[1]=a[0];
    assert(!nx_rs_draw_group(a,3,&r));
    nx_rs_draw_samples s={0};
    assert(nx_rs_draw_sample(&s,15,0.75f,1));
    assert(nx_rs_draw_sample(&s,15,0.75f,1) && s.count==1);
    assert(!nx_rs_draw_sample(&s,15,0.5f,1));
    assert(!nx_rs_draw_sample(&s,19,0.75f,0));
    assert(nx_rs_draw_sample(&s,19,1.0f,0) && s.count==1);
    return 0;
}
