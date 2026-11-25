#include <assert.h>
#include "gfx_vk_resolution_budget.h"
int main(void) {
    nx_rs_budget b = {0};
    assert(nx_rs_reserve(&b, 48, 64));
    assert(!nx_rs_reserve(&b, 17, 64));
    assert(atomic_load(&b.bytes) == 48);
    assert(nx_rs_reserve(&b, 16, 64));
    assert(!nx_rs_reserve(&b, 1, 64));
    assert(!nx_rs_reserve(&b, UINT64_MAX, 64));
    nx_rs_release(&b, 48);
    assert(nx_rs_reserve(&b, 48, 64));
    nx_rs_release(&b, 16);
    nx_rs_release(&b, 48);
    assert(atomic_load(&b.bytes) == 0);
    assert(!nx_rs_reserve(&b, 0, 64));
    assert(nx_rs_reserve(&b, UINT64_MAX, UINT64_MAX));
    assert(!nx_rs_reserve(&b, 1, UINT64_MAX));
    nx_rs_release(&b, UINT64_MAX);
    return 0;
}
