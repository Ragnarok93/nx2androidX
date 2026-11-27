"""Compile the production surface row serializer and explicit source identity."""
import csv
import io
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

class PresentationMarker(unittest.TestCase):
    def test_exact_present_ticket_and_raw_crop(self):
        src=(ROOT/'nxnative/dm/android/gfx_vk_surface_capture.inc').read_text()
        # Extract the actual binding type and serializer, not a reimplementation.
        fields=src[src.index('typedef struct {'):src.index('#include "gfx_vk_scene_observe.inc"')]
        helper=src[src.index('static void surface_present_identity('):src.index('static void surface_usage(')]
        header=next(l for l in src.splitlines() if 'fputs("role\\tpresent' in l)
        harness=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef union {uint32_t words[4];float floats[4];} VkClearValue;
enum {SC_ROWS=32};
static struct {FILE *usage;unsigned rows,dropped;} g_sc;
'''+fields+helper+r'''
int main(void) {
    g_sc.usage=stdout;
'''+header+r'''
    surface_binding_t b={.present=40,.submission=9,.w=1920,.h=1080,.samples=1,
        .aspect=1,.mips=1,.layers=1,.image=7,.write_sequence=8};
    surface_write_row("color",&b,-1,0,NULL,"metadata");
    const int crop[4]={11,22,1777,999};
    surface_present_identity(&b,crop,77);
    surface_write_row("presentation-source",&b,-1,-1,NULL,"full-image");
    surface_present_identity(&b,NULL,78);
    surface_write_row("presentation-source",&b,-1,-1,NULL,"full-image-unavailable");
    assert(g_sc.rows==3 && !g_sc.dropped);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);(p/'case.c').write_text(harness)
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p/'case.c'),'-o',str(p/'case')],check=True)
            result=subprocess.run([str(p/'case')],check=True,capture_output=True,text=True)
        rows=list(csv.DictReader(io.StringIO(result.stdout),delimiter='\t'))
        self.assertEqual(len(rows),3)
        self.assertTrue(all(None not in r and None not in r.values() for r in rows))
        self.assertEqual([r['real_ticket'] for r in rows],['0','77','78'])
        self.assertEqual([r['present'] for r in rows],['40']*3)
        self.assertEqual(rows[1]['pass'],'-202')
        keys=['present_crop_'+k for k in ('x','y','w','h')]
        self.assertEqual([rows[1][k] for k in keys],['11','22','1777','999'])
        self.assertEqual([rows[2][k] for k in keys],['0','0','1920','1080'])

if __name__=='__main__': unittest.main()
