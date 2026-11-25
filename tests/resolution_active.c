#include <assert.h>
#include "gfx_vk_resolution_active.h"
int main(void) {
    nx_rs_active a={0};
    assert(nx_rs_active_update(&a,1,1));
    assert(a.n==1 && a.owner[0]==1);
    assert(nx_rs_active_update(&a,1,1) && a.n==1);
    assert(nx_rs_active_update(&a,2,1) && a.n==2);
    assert(nx_rs_active_update(&a,1,0) && a.n==1 && a.owner[0]==2);
    assert(nx_rs_active_update(&a,1,0) && a.n==1);
    for (uintptr_t i=3;i<=NX_RS_ACTIVE_MAX+1;i++) assert(nx_rs_active_update(&a,i,1));
    assert(a.n==NX_RS_ACTIVE_MAX);
    nx_rs_active staged=a;
    assert(!nx_rs_active_update(&staged,9999,1));
    assert(staged.n==a.n && staged.owner[0]==a.owner[0]);
    assert(nx_rs_active_update(&staged,2,0));
    assert(nx_rs_active_update(&staged,9999,1));
    assert(a.owner[0]==2 && a.n==NX_RS_ACTIVE_MAX);
    assert(!nx_rs_active_update(&a,0,1));
    return 0;
}
