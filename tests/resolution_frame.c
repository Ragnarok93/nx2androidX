#include <assert.h>
#include "gfx_vk_resolution_frame.h"
int main(void) {
    nx_rs_frame_state s={0,100};
    assert(nx_rs_frame_latch(&s,1,75,0,0)==NX_RS_FRAME_ACCEPT);
    assert(s.epoch==1 && s.target==75);
    assert(nx_rs_frame_latch(&s,1,50,0,0)==NX_RS_FRAME_IGNORE);
    assert(s.target==75);
    assert(nx_rs_frame_latch(&s,2,50,1,0)==NX_RS_FRAME_RESTORE);
    assert(s.epoch==1 && s.target==75);
    assert(nx_rs_frame_latch(&s,2,50,1,1)==NX_RS_FRAME_ACCEPT);
    assert(s.epoch==2 && s.target==50);
    assert(nx_rs_frame_latch(&s,3,50,1,0)==NX_RS_FRAME_ACCEPT);
    assert(nx_rs_frame_latch(&s,4,100,1,0)==NX_RS_FRAME_RESTORE);
    assert(nx_rs_frame_latch(&s,4,100,0,0)==NX_RS_FRAME_ACCEPT);
    assert(s.epoch==4 && s.target==100);
    assert(nx_rs_frame_latch(&s,5,49,0,0)==NX_RS_FRAME_IGNORE);
    assert(nx_rs_frame_latch(&s,5,101,0,0)==NX_RS_FRAME_IGNORE);
    assert(nx_rs_frame_latch(&s,3,75,0,0)==NX_RS_FRAME_IGNORE);
    assert(s.epoch==4 && s.target==100);
    return 0;
}
