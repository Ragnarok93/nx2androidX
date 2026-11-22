// Integer-only dynamic-resolution policy. The caller samples real GPU work
// on source frames, not generated LSFG presents or the final blit. A sustained
// over-budget GPU p90 lowers internal render scale; raising it is slower.
// Guarded by a verified engine/render-scale hook elsewhere.
#ifndef NX_DYNRES_POLICY_H
#define NX_DYNRES_POLICY_H
typedef struct { int scale, floor, n, bad, good, last_p90_us; int samples[30]; } nx_dynres_policy;
static inline void nx_dynres_reset(nx_dynres_policy *p, int floor) {
    p->scale = 100; p->floor = floor; p->n = p->bad = p->good = p->last_p90_us = 0;
}
static inline int nx_dynres_sample(nx_dynres_policy *p, int gpu_us, int budget_us) {
    if (gpu_us <= 0 || gpu_us > 500000 || budget_us <= 0) return 0;
    p->samples[p->n++] = gpu_us;
    if (p->n < 30) return 0;
    p->n = 0;
    int ordered[30];
    for (int i = 0; i < 30; i++) {
        int v = p->samples[i], j = i;
        while (j > 0 && ordered[j - 1] > v) { ordered[j] = ordered[j - 1]; j--; }
        ordered[j] = v;
    }
    p->last_p90_us = ordered[26];
    int before = p->scale;
    if (p->last_p90_us > budget_us) {
        p->bad++; p->good = 0;
        if (p->bad >= 2) {
            p->scale -= 5; if (p->scale < p->floor) p->scale = p->floor;
            p->bad = 0;
        }
    } else if (p->last_p90_us < budget_us * 70 / 100) {
        p->good++; p->bad = 0;
        if (p->good >= 4) {
            p->scale += 5; if (p->scale > 100) p->scale = 100;
            p->good = 0;
        }
    } else { p->good = p->bad = 0; }
    return before != p->scale;
}
#endif
