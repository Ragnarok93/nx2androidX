"""Fence-owned resource retirement: FIFO ordering and no early free."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

class RetireQueue(unittest.TestCase):
    def test_open_and_idle_fences(self):
        source = r'''
#include <assert.h>
#include "gfx_vk_retire_queue.h"
int main(void) {
    vk_retire_queue q = {0};
    vk_retire_entry view = {.kind=3}, buffer = {.kind=0}, next = {.kind=2};
    vk_retire_begin(&q, 7);
    vk_retire_push(&q, &view);
    // Submission 7 is open. Even a completed submission 6 cannot retire it.
    assert(vk_retire_pop(&q, 6) == NULL);
    // Closing is implicit: idle frees inherit the last submitted fence.
    vk_retire_push(&q, &buffer);
    vk_retire_begin(&q, 8);
    vk_retire_push(&q, &next);
    assert(vk_retire_pop(&q, 7) == &view);
    assert(vk_retire_pop(&q, 7) == &buffer);
    assert(vk_retire_pop(&q, 7) == NULL);
    assert(vk_retire_pop(&q, 8) == &next);
    assert(!q.head && !q.tail);
    vk_retire_push(&q, &view);
    assert(vk_retire_pop(&q, 8) == &view);
    assert(!q.head && !q.tail);
    vk_retire_queue initial = {0};
    vk_retire_push(&initial, &view);
    assert(vk_retire_pop(&initial, 0) == &view);
    return 0;
}'''
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / 'case.c'
            p.write_text(source)
            exe = p.with_suffix('')
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-I', str(ROOT / 'nxnative/dm/android'), str(p), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
