#include <assert.h>
#include "gfx_vk_resolution_storage_private.h"
int main(void) {
    assert(nx_rs_private_storage_extent(32,32,16,16));
    assert(nx_rs_private_storage_extent(32,32,32,32));
    assert(nx_rs_private_storage_extent(32,16,16,8));
    assert(!nx_rs_private_storage_extent(32,32,16,8));
    assert(!nx_rs_private_storage_extent(32,32,17,17));
    assert(!nx_rs_private_storage_extent(32,32,0,0));
    assert(!nx_rs_private_storage_extent(32,32,64,64));
    assert(!nx_rs_private_storage_extent(64,64,32,32));
    assert(!nx_rs_private_storage_extent(UINT32_MAX,UINT32_MAX,16,16));
    return 0;
}
