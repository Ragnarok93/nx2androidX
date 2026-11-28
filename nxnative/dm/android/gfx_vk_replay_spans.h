#pragma once
#include <stdint.h>
#include <math.h>

typedef struct {unsigned first,last;} vk_replay_span;
// Group timestamp spans are ordered by queue submission. Union overlapping
// estimates rather than charging gaps or counting pipelined intervals twice.
static inline int vk_replay_spans_ns(const uint64_t *stamp,unsigned n,
        const vk_replay_span *spans,unsigned count,unsigned bits,double period,uint64_t *ns) {
    if(!stamp||!spans||!count||!ns||!bits||bits>64||!(period>0)||!isfinite(period)||spans[0].first>=n)return 0;
    uint64_t mask=bits==64?UINT64_MAX:(UINT64_C(1)<<bits)-1;
    uint64_t half=UINT64_C(1)<<(bits-1),anchor=stamp[spans[0].first];
    uint64_t previous=0,end=0,ticks=0;
    for(unsigned i=0;i<count;i++) {
        if(spans[i].first>=n||spans[i].last>=n)return 0;
        uint64_t start=(stamp[spans[i].first]-anchor)&mask;
        uint64_t duration=(stamp[spans[i].last]-stamp[spans[i].first])&mask;
        if(start<previous||start>=half||duration>=half-start)return 0;
        uint64_t stop=start+duration,from=start>end?start:end;
        if(stop>from)ticks+=stop-from;
        if(stop>end)end=stop;
        previous=start;
    }
    long double value=(long double)ticks*period;
    if(!(value>=0)||value>=18446744073709551616.0L)return 0;
    *ns=(uint64_t)value;return 1;
}
