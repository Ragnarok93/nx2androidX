#include <assert.h>
#include "gfx_vk_resolution_scope.h"
#ifdef OLD
static int nx_rs_scope_terminal_compute(const nx_rs_scope *s,unsigned owner,unsigned reads) {
    (void)s;(void)owner;(void)reads;return 0;
}
#endif
int main(void) {
    nx_rs_scope s;
    assert(nx_rs_scope_init_compute(&s,1));
    assert(!nx_rs_scope_terminal_compute(&s,1,0));
    assert(nx_rs_scope_compute_write(&s,1));
    assert(!nx_rs_scope_terminal_compute(&s,1,0));
    assert(nx_rs_scope_sample(&s,1));
    assert(nx_rs_scope_terminal_compute(&s,1,0));
    assert(!nx_rs_scope_terminal_compute(&s,1,1));
    assert(!nx_rs_scope_terminal_compute(&s,2,0));
    assert(!nx_rs_scope_terminal_compute(&s,0,0));
    s.exported=1;
    assert(nx_rs_scope_terminal_compute(&s,1,0));
    s.discarded=1;
    assert(!nx_rs_scope_terminal_compute(&s,1,0));
    assert(nx_rs_scope_init(&s,3,1));
    assert(nx_rs_scope_clear(&s,1));
    assert(nx_rs_scope_draw(&s,0));
    assert(nx_rs_scope_sample(&s,1));
    assert(!nx_rs_scope_terminal_compute(&s,1,0));
    assert(!nx_rs_scope_terminal_compute(&s,3,0));
    return 0;
}
