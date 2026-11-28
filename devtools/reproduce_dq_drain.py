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
#include <time.h>
#include <unistd.h>
typedef void *VkCommandBuffer;
enum { OP_WRAP, FIRST, SECOND };
#define VKLOG(...) fprintf(stderr, __VA_ARGS__), fputc('\n', stderr)
static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static int entered[3], released[3], waiting[3], timeouts[3];
static _Thread_local int waiter;
static int observed_wait(pthread_cond_t *cv, pthread_mutex_t *mu,
                         const struct timespec *deadline) {
    pthread_mutex_lock(&gate);
    waiting[waiter]++;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&gate);
    int rc = pthread_cond_timedwait(cv, mu, deadline);
    if (rc == ETIMEDOUT) timeouts[waiter]++;
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
    return NULL;
}
static void enqueue(int op) {
    void *payload = rec_begin(op, NULL, 0);
    assert(payload);
    rec_end(payload);
}
int main(int argc, char **argv) {
    (void)argv;
    alarm(8);
    assert(pthread_spin_init(&g_pl, PTHREAD_PROCESS_PRIVATE) == 0);
    g_ring = calloc(1, RING);
    assert(g_ring);
    g_on = 1;
    pthread_t recorder, a, b;
    assert(pthread_create(&recorder, NULL, recorder_main, NULL) == 0);
    enqueue(FIRST);
    until(&entered[FIRST], 1);
    assert(pthread_create(&a, NULL, drain_thread, (void *)(intptr_t)1) == 0);
    until(&waiting[1], 1);
    enqueue(SECOND);
    assert(pthread_create(&b, NULL, drain_thread, (void *)(intptr_t)2) == 0);
    until(&waiting[2], 1);
    release_record(FIRST);
    assert(pthread_join(a, NULL) == 0);
    until(&entered[SECOND], 1);
    until(&waiting[2], 2);
    release_record(SECOND);
    assert(pthread_join(b, NULL) == 0);
    assert(atomic_load(&g_rd) == atomic_load(&g_pub));
    printf("completed=%llu first_timeouts=%d second_timeouts=%d\n",
           (unsigned long long)atomic_load(&g_rd), timeouts[1], timeouts[2]);
    if (argc == 2) return !(timeouts[1] == 0 && timeouts[2] > 0);
    return timeouts[1] != 0 || timeouts[2] != 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[1]
                        / 'nxnative/dm/android/gfx_vk_dq.c')
    parser.add_argument('--expect-missed-notification', action='store_true')
    args = parser.parse_args()
    src = args.source.read_text()
    queue = src[src.index('enum { RING ='):src.index('// copying helpers:')]
    recorder = src[src.index('static void *recorder_main('):src.index('// ---- the wrappers')]
    with tempfile.TemporaryDirectory() as tmp:
        c = Path(tmp) / 'drain.c'
        exe = Path(tmp) / 'drain'
        c.write_text(PREFIX + queue + MOCK + recorder + DRIVER)
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-O2', '-pthread',
                        str(c), '-o', str(exe)], check=True)
        cmd = [str(exe)] + (['baseline'] if args.expect_missed_notification else [])
        return subprocess.run(cmd, timeout=10).returncode


if __name__ == '__main__':
    raise SystemExit(main())
