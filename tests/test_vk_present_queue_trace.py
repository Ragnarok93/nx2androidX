"""Check the production bounded queue timing serialization, not GPU timing."""
import csv
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class PresentQueueTrace(unittest.TestCase):
    def test_queue_timestamps_and_column_order(self):
        src = r'''
#include <assert.h>
#include "gfx_vk_present_queue_trace.h"
int main(void) {
    assert(dm_present_queue_age(100,80,2)==20);
    assert(!dm_present_queue_age(100,0,2));
    assert(!dm_present_queue_age(80,100,2));
    assert(!dm_present_queue_age(100,80,0));
    dm_present_queue_trace row={.source_ns=100,.entry_ns=130,.dequeue_ns=160,
        .work_begin_ns=165,.work_end_ns=250,.retire_ns=260,
        .oldest_entry_age_ns=50,.oldest_dequeue_age_ns=60,
        .entry_occupied=4,.dequeue_occupied=3,.has_image=1};
    printf("ticket" DM_PRESENT_QUEUE_COLUMNS "\n1");
    dm_present_queue_write(stdout,&row);
    printf("\n2");
    dm_present_queue_write(stdout,&(dm_present_queue_trace){0});
    printf("\n3");
    dm_present_queue_write(stdout,&(dm_present_queue_trace){.source_ns=300,
        .work_begin_ns=302,.work_end_ns=320,.retire_ns=321,.synchronous=1,.has_image=1});
    puts("");
}
'''
        with tempfile.TemporaryDirectory() as d:
            c = Path(d) / 'trace.c'
            c.write_text(src)
            exe = c.with_suffix('')
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'nxnative/dm/android'), str(c), '-o', str(exe)], check=True)
            output = subprocess.check_output([str(exe)], text=True)
        a, b, c = csv.DictReader(output.splitlines(), delimiter='\t')
        for row in (a, b, c):
            self.assertNotIn(None, row)
            self.assertNotIn(None, row.values())
        self.assertEqual(int(a['queue_entry_ns'])-int(a['queue_source_ns']), 30)
        self.assertEqual(int(a['queue_dequeue_ns'])-int(a['queue_entry_ns']), 30)
        self.assertEqual(int(a['queue_work_end_ns'])-int(a['queue_work_begin_ns']), 85)
        self.assertEqual(a['queue_cpu_retire_ns'], '260')
        self.assertEqual(a['queue_entry_occupied'], '4')
        self.assertEqual(a['queue_dequeue_occupied'], '3')
        self.assertEqual(a['queue_oldest_entry_age_ns'], '50')
        self.assertEqual(a['queue_oldest_dequeue_age_ns'], '60')
        self.assertTrue(all(v == '0' for k, v in b.items() if k != 'ticket'))
        self.assertEqual(c['queue_sync'], '1')
        self.assertEqual(c['queue_entry_ns'], '0')
        self.assertEqual(c['queue_dequeue_ns'], '0')
