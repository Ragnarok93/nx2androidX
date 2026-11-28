#pragma once
#include <stdint.h>
#include <math.h>
#include <string.h>

// Present-owner ledger. Replay-integrated passes are already in replay_ms;
// charge only a separately submitted presentation reconstruction here.
enum { DM_SOURCE_BUDGET_SLOTS=64 };
typedef struct {
    uint64_t ticket,workload;
    double period_ms,recon_ms;
    unsigned mode,w,h;
    int separate_recon,recon_known;
} dm_source_budget_row;
typedef struct {dm_source_budget_row rows[DM_SOURCE_BUDGET_SLOTS];} dm_source_budget;
static inline void dm_source_budget_clear(dm_source_budget *b) {memset(b,0,sizeof *b);}
static inline void dm_source_budget_begin(dm_source_budget *b,uint64_t t,uint64_t workload,
        double period_ms,int separate_recon,unsigned mode,unsigned w,unsigned h) {
    if(!t)return;
    b->rows[t%DM_SOURCE_BUDGET_SLOTS]=(dm_source_budget_row){
        .ticket=t,.workload=workload,.period_ms=period_ms,.mode=mode,.w=w,.h=h,
        .separate_recon=separate_recon,.recon_known=!separate_recon};
}
static inline int dm_source_budget_recon(dm_source_budget *b,uint64_t t,unsigned mode,
        unsigned w,unsigned h,double ms) {
    if(!t||!isfinite(ms)||ms<0)return 0;
    dm_source_budget_row *r=&b->rows[t%DM_SOURCE_BUDGET_SLOTS];
    if(r->ticket!=t||!r->separate_recon||r->recon_known||r->mode!=mode||r->w!=w||r->h!=h)return 0;
    r->recon_ms=ms;r->recon_known=1;return 1;
}
static inline double dm_source_budget_residual(const dm_source_budget *b,uint64_t t,
        uint64_t workload,double replay_ms) {
    if(!t||!(replay_ms>=0)||!isfinite(replay_ms))return NAN;
    const dm_source_budget_row *r=&b->rows[t%DM_SOURCE_BUDGET_SLOTS];
    if(r->ticket!=t||r->workload!=workload||!r->recon_known||!(r->period_ms>0)||!isfinite(r->period_ms))return NAN;
    return r->period_ms-replay_ms-r->recon_ms-1.0;
}
