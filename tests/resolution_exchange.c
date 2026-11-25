#include <assert.h>
#include "ownership_exchange.h"
int main(void) {
    nx_rs_backing_state a[3]={{1,1280,720,1280,720,4194304},{2,1280,720,1280,720,4194304},{0}};
    unsigned changed=99;
    assert(nx_rs_exchange_plan(a,3,1280,720,&changed) && changed==0);
    assert(nx_rs_exchange_plan(a,3,960,540,&changed) && changed==3);
    a[1].owner=1;
    assert(!nx_rs_exchange_plan(a,3,960,540,&changed));
    a[1].owner=2; a[1].lh=721;
    assert(!nx_rs_exchange_plan(a,3,960,540,&changed));
    a[1].lh=720; a[1].pw=960; a[1].ph=540;
    assert(nx_rs_exchange_plan(a,3,960,540,&changed) && changed==1);
    assert(!nx_rs_exchange_plan(a,3,959,540,&changed));
    assert(nx_rs_exchange_benefit(&a[0],960,540,2359296));
    assert(!nx_rs_exchange_benefit(&a[0],960,540,4194304));
    assert(nx_rs_exchange_benefit(&a[1],1280,720,4194304));
    return 0;
}
