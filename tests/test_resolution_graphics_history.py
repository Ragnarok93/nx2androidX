"""Graphics prewarm records survive runtime upgrades and torn appends."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class GraphicsHistory(unittest.TestCase):
    def test_migrate_append_and_reopen(self):
        source = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include "gfx_vk_graphics_history.h"
int main(int argc, char **argv) {
    assert(argc == 2);
    char legacy[1024], stable[1024];
    snprintf(legacy,sizeof legacy,"%s/legacy.bin",argv[1]);
    snprintf(stable,sizeof stable,"%s/stable.bin",argv[1]);
    const uint32_t magic = 0x32455050u;
    uint32_t hdr[] = {magic, sizeof(uint64_t)};
    uint64_t old[] = {10,20,30}, extra = 40;
    FILE *f = fopen(legacy,"wb"); assert(f);
    assert(fwrite(hdr,sizeof hdr,1,f)==1);
    assert(fwrite(old,sizeof *old,3,f)==3);
    assert(fputc(1,f)!=EOF && fclose(f)==0); // torn legacy tail
    void *records = NULL; size_t n = 0;
    f = vk_graphics_history_open(stable,legacy,magic,sizeof(uint64_t),&records,&n);
    assert(f && n == 3 && ((uint64_t *)records)[2]==30); free(records);
    assert(fwrite(&extra,sizeof extra,1,f)==1 && fclose(f)==0);
    // Replacing game data removes the legacy path; the private cache survives.
    assert(unlink(legacy)==0);
    f=fopen(stable,"ab");assert(f && fputc(2,f)!=EOF && fclose(f)==0);
    f = vk_graphics_history_open(stable,legacy,magic,sizeof(uint64_t),&records,&n);
    assert(f && n == 4 && ((uint64_t *)records)[3]==40); free(records);
    assert(fclose(f)==0);
    struct stat st; assert(stat(stable,&st)==0 && st.st_size==sizeof hdr+4*sizeof(uint64_t));
    // Incompatible record formats must not be loaded.
    f = vk_graphics_history_open(stable,legacy,magic+1,sizeof(uint64_t),&records,&n);
    assert(f && !records && n==0 && fclose(f)==0);
    return 0;
}'''
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / 'case.c'
            p.write_text(source)
            exe = p.with_suffix('')
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-I', str(ROOT / 'nxnative/dm/android'), str(p), '-o', str(exe)], check=True)
            subprocess.run([str(exe), tmp], check=True)
