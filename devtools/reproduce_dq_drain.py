#!/usr/bin/env python3
"""Compile actual queue/drain/recorder code with a gated mock Vulkan recorder.

Run from checkout: python3 devtools/reproduce_dq_drain.py
Use --expect-missed-notification only on the known-broken baseline.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

PREFIX = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
typedef void *VkCommandBuffer;
enum { OP_WRAP, FIRST, SECOND };
#define die(msg) do { fprintf(stderr, "%s\n", msg); abort(); } while (0)
#define VKLOG(...) fprintf(stderr, __VA_ARGS__), fputc('\n', stderr)
static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static int entered[3], released[3], waiting[3], timeouts[3];
static _Atomic int done[3];
static _Thread_local int waiter, producer;
static int producer_yielded, producer_reserved, producer_release;
static _Atomic int producer_done;
static int observed_yield(void) {
    if (producer && !producer_yielded) {
        pthread_mutex_lock(&gate);
        producer_yielded = 1;
        pthread_cond_broadcast(&changed);
        pthread_mutex_unlock(&gate);
    }
    return sched_yield();
}
#define sched_yield observed_yield
static int observed_wait(pthread_cond_t *cv, pthread_mutex_t *mu,
                         const struct timespec *deadline) {
    pthread_mutex_lock(&gate);
    waiting[waiter]++;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&gate);
    int rc = pthread_cond_timedwait(cv, mu, deadline);
    if (rc == ETIMEDOUT) {
        pthread_mutex_lock(&gate);
        timeouts[waiter]++;
        pthread_cond_broadcast(&changed);
        pthread_mutex_unlock(&gate);
    }
    return rc;
}
#define pthread_cond_timedwait observed_wait
'''
MOCK = r'''
static void run(const rec_hdr *h) {
    if (h->op == OP_WRAP) return;
    assert(h->op == FIRST || h->op == SECOND);
    pthread_mutex_lock(&gate);
    entered[h->op] = 1;
    pthread_cond_broadcast(&changed);
    while (!released[h->op]) pthread_cond_wait(&changed, &gate);
    pthread_mutex_unlock(&gate);
}
'''
DRIVER = r'''
#undef pthread_cond_timedwait
static void until(int *value, int target) {
    pthread_mutex_lock(&gate);
    while (*value < target) pthread_cond_wait(&changed, &gate);
    pthread_mutex_unlock(&gate);
}
static void release_record(int id) {
    pthread_mutex_lock(&gate);
    released[id] = 1;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&gate);
}
static void *drain_thread(void *arg) {
    waiter = (int)(intptr_t)arg;
    vk_dq_drain();
    atomic_store(&done[waiter], 1);
    return NULL;
}
static void enqueue_size(int op, size_t bytes) {
    void *payload = rec_begin(op, NULL, bytes);
    assert(payload);
    rec_end(payload);
}
static void enqueue(int op) { enqueue_size(op, 0); }
static void spurious_wake(void) {
    pthread_mutex_lock(&g_mu);
    pthread_cond_broadcast(&g_dcv);
    pthread_mutex_unlock(&g_mu);
}
static void *producer_thread(void *arg) {
    int saturation = (int)(intptr_t)arg;
    producer = 1;
    void *payload = rec_begin(saturation ? SECOND : FIRST, NULL,
                              saturation ? RING/4-sizeof(rec_hdr) : 0);
    assert(payload);
    if (!saturation) {
        pthread_mutex_lock(&gate);
        producer_reserved = 1;
        pthread_cond_broadcast(&changed);
        while (!producer_release) pthread_cond_wait(&changed, &gate);
        pthread_mutex_unlock(&gate);
    }
    rec_end(payload);
    atomic_store(&producer_done, 1);
    return NULL;
}
int main(int argc, char **argv) {
    int baseline = argc > 1 && !strcmp(argv[1], "baseline");
    int equal = argc > 1 && !strcmp(argv[1], "equal");
    int wrap = argc > 1 && !strcmp(argv[1], "wrap");
    int saturation = argc > 1 && !strcmp(argv[1], "saturation");
    int suspended = argc > 1 && !strcmp(argv[1], "suspended");
    int timeout = argc > 1 && !strcmp(argv[1], "timeout");
    alarm(8);
    assert(pthread_spin_init(&g_pl, PTHREAD_PROCESS_PRIVATE) == 0);
    g_ring = calloc(1, RING);
    assert(g_ring);
    #ifdef DQ_TEST_MONOTONIC_INIT
    assert(dq_drain_cv_init() == 0);
    #endif
    g_on = 1;
    if (wrap) {
        g_w = (UINT64_C(5)<<30) + RING - sizeof(rec_hdr);
        atomic_store(&g_pub, g_w);
        atomic_store(&g_rd, g_w);
    }
    pthread_t recorder, a, b;
    assert(pthread_create(&recorder, NULL, recorder_main, NULL) == 0);
    if (saturation || suspended) {
        pthread_t producer_thread_id;
        if (saturation) {
            enqueue_size(FIRST, RING/4-sizeof(rec_hdr));
            until(&entered[FIRST], 1);
            for (int i=0; i<3; ++i) enqueue_size(SECOND, RING/4-sizeof(rec_hdr));
        }
        assert(pthread_create(&producer_thread_id, NULL, producer_thread,
                              (void *)(intptr_t)saturation) == 0);
        if (saturation) {
            until(&producer_yielded, 1);
            assert(!atomic_load(&producer_done) && atomic_load(&g_pub) == RING);
            release_record(FIRST);
            assert(pthread_join(producer_thread_id, NULL) == 0);
            until(&entered[SECOND], 1);
        } else {
            until(&producer_reserved, 1);
            assert(atomic_load(&g_pub) == 0);
        }
        assert(pthread_create(&a, NULL, drain_thread, (void *)(intptr_t)1) == 0);
        if (suspended) {
            pthread_mutex_lock(&gate);
            producer_release = 1;
            pthread_cond_broadcast(&changed);
            pthread_mutex_unlock(&gate);
            assert(pthread_join(producer_thread_id, NULL) == 0);
            until(&entered[FIRST], 1);
        }
        until(&waiting[1], 1);
        assert(!atomic_load(&done[1]));
        release_record(saturation ? SECOND : FIRST);
        assert(pthread_join(a, NULL) == 0);
        assert(atomic_load(&g_rd) == atomic_load(&g_pub));
        assert(atomic_load(&g_rd) == (saturation ? 5*(uint64_t)RING/4 : sizeof(rec_hdr)));
        printf("completed=%llu timeouts=%d\n", (unsigned long long)atomic_load(&g_rd), timeouts[1]);
        return timeouts[1] != 0;
    }
    enqueue_size(FIRST, wrap ? 16 : 0);
    until(&entered[FIRST], 1);
    assert(pthread_create(&a, NULL, drain_thread, (void *)(intptr_t)1) == 0);
    until(&waiting[1], 1);
    if (timeout) {
        until(&timeouts[1], 1);
        assert(atomic_load(&g_rd) < atomic_load(&g_pub));
        release_record(FIRST);
        assert(pthread_join(a, NULL) == 0);
        assert(atomic_load(&g_rd) == atomic_load(&g_pub));
        return timeouts[1] != 1;
    }
    if (!equal) enqueue(SECOND);
    assert(pthread_create(&b, NULL, drain_thread, (void *)(intptr_t)2) == 0);
    until(&waiting[2], 1);
    if (equal) {
        spurious_wake();
        until(&waiting[1], 2);
        until(&waiting[2], 2);
        assert(!atomic_load(&done[1]) && !atomic_load(&done[2]));
    }
    release_record(FIRST);
    assert(pthread_join(a, NULL) == 0);
    if (!equal) {
        until(&entered[SECOND], 1);
        until(&waiting[2], 2);
        assert(!atomic_load(&done[2]));
        release_record(SECOND);
    }
    assert(pthread_join(b, NULL) == 0);
    assert(atomic_load(&g_rd) == atomic_load(&g_pub));
    printf("completed=%llu first_timeouts=%d second_timeouts=%d\n",
           (unsigned long long)atomic_load(&g_rd), timeouts[1], timeouts[2]);
    if (wrap) assert(atomic_load(&g_rd) > (UINT64_C(5)<<30));
    if (baseline) return !(timeouts[1] == 0 && timeouts[2] > 0);
    return timeouts[1] != 0 || timeouts[2] != 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[1]
                        / 'nxnative/dm/android/gfx_vk_dq.c')
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--expect-missed-notification', action='store_true')
    mode.add_argument('--stress', action='store_true', help='equal targets, spurious wakes, large-cursor wrap, saturation, suspended publication')
    args = parser.parse_args()
    src = args.source.read_text()
    queue = src[src.index('enum { RING ='):src.index('// copying helpers:')]
    recorder = src[src.index('static void *recorder_main('):src.index('// ---- the wrappers')]
    with tempfile.TemporaryDirectory() as tmp:
        c = Path(tmp) / 'drain.c'
        exe = Path(tmp) / 'drain'
        c.write_text(('#define DQ_TEST_MONOTONIC_INIT\n' if 'static int dq_drain_cv_init(' in queue else '') + PREFIX + queue + MOCK + recorder + DRIVER)
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-O2', '-pthread',
                        str(c), '-o', str(exe)], check=True)
        cases = ['baseline'] if args.expect_missed_notification else ['staggered', 'equal', 'wrap', 'saturation', 'suspended', 'timeout'] if args.stress else ['staggered']
        for case in cases:
            print(case, flush=True)
            result = subprocess.run([str(exe), case], timeout=10)
            if result.returncode:
                return result.returncode
        return 0


if __name__ == '__main__':
    raise SystemExit(main())
