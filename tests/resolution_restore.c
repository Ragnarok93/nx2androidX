#include <assert.h>
#include "gfx_vk_resolution_restore.h"
int main(void) {
    assert(nx_rs_restore_admit(4194304,4194304));
    assert(nx_rs_restore_admit(4194304,2097152));
    assert(!nx_rs_restore_admit(4194304,4194305));
    assert(!nx_rs_restore_admit(0,4194304));
    assert(!nx_rs_restore_admit(4194304,0));
    assert(!nx_rs_restore_admit(4194304,UINT64_MAX));
    return 0;
}
